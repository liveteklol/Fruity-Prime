#include "DebugLog.hpp"
#include "NativeRuntime/System/AtomicSharedPtr.hpp"
#include "NativeRuntime/System/Heartbeat.hpp"
#include "NativeRuntime/System/Runtime.hpp"

#include "../Program.hpp"
#include "Branding.hpp"
#include "Launcher/Portable/GameFiles.hpp"
#include "Launcher/Portable/LauncherPrefs.hpp"
#include "Network/NetLag.hpp"
#include "Network/NetProtocol.hpp"
#include "RenderOptions.hpp"
#include "Update/BuildVersion.hpp"
#include "../NativeRuntime/System/Console.hpp"
#include "../NativeRuntime/System/Encoding.hpp"
#include "../NativeRuntime/System/ExceptionText.hpp"
#include "../NativeRuntime/System/IO.hpp"
#include "../NativeRuntime/System/Runtime.hpp"
#include "NativeRuntime/System/Globalization.hpp"
#include "../NativeRuntime/System/Sort.hpp"
#include "../NativeRuntime/System/DateTime.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <climits>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <iostream>
#include <limits>
#include <locale>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <typeinfo>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#elif defined(__APPLE__)
#include <execinfo.h>
#include <fcntl.h>
#include <crt_externs.h>
#include <mach-o/dyld.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>
#else
#if defined(__ANDROID__)
#include <dlfcn.h>
#include <unwind.h>
#else
#include <execinfo.h>
#endif
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>
#if defined(__linux__) || defined(__ANDROID__)
#include <sched.h>
#endif
#endif

#if defined(__GNUG__)
#include <cxxabi.h>
#endif

using ::MphRead::NativeRuntime::AppContextBaseDirectory;
using ::MphRead::NativeRuntime::EnvironmentGetVariable;
using ::MphRead::NativeRuntime::EnvironmentProcessPath;
using ::MphRead::NativeRuntime::ExceptionTypeName;
using ::MphRead::NativeRuntime::PathCombine;
using ::MphRead::NativeRuntime::PathGetDirectoryName;
using ::MphRead::NativeRuntime::WideToUtf8;
using ::MphRead::NativeRuntime::Wtf8ToWide;

using ::MphRead::NativeRuntime::ManagedSort;
namespace
{

    constexpr std::int32_t KeepFiles = 8;

    [[nodiscard]] std::filesystem::path PathFromManagedString(std::string_view path)
    {
#if defined(_WIN32)
        return std::filesystem::path(Wtf8ToWide(path));
#else
        if (path.find('\0') != std::string_view::npos)
        {
            throw std::invalid_argument("Path contains a null character.");
        }
        return std::filesystem::path(std::string(path));
#endif
    }

    class FileSink final
    {
    public:
        explicit FileSink(std::string_view path)
        {
#if defined(_WIN32)
            const std::wstring wide = Wtf8ToWide(path);
            _handle = CreateFileW(wide.c_str(), GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS,
                FILE_ATTRIBUTE_NORMAL, nullptr);
            if (_handle == INVALID_HANDLE_VALUE)
            {
                throw std::system_error(static_cast<int>(GetLastError()),
                    std::system_category());
            }
#else
            if (path.find('\0') != std::string_view::npos)
            {
                throw std::invalid_argument("Path contains a null character.");
            }
            const std::string native(path);
            int flags = O_WRONLY | O_CREAT | O_TRUNC;
#ifdef O_CLOEXEC
            flags |= O_CLOEXEC;
#endif
            _fd = ::open(native.c_str(), flags, 0666);
            if (_fd == -1)
            {
                throw std::system_error(errno, std::generic_category());
            }
#endif
        }

        FileSink(const FileSink&) = delete;
        FileSink& operator=(const FileSink&) = delete;

        ~FileSink()
        {
            CloseNoThrow();
        }

        void Write(std::string_view bytes)
        {
#if defined(_WIN32)
            std::size_t offset = 0;
            while (offset < bytes.size())
            {
                const std::size_t remaining = bytes.size() - offset;
                const DWORD chunk = remaining > static_cast<std::size_t>(MAXDWORD)
                    ? MAXDWORD
                    : static_cast<DWORD>(remaining);
                DWORD written = 0;
                if (!WriteFile(_handle, bytes.data() + offset, chunk, &written, nullptr))
                {
                    throw std::system_error(static_cast<int>(GetLastError()),
                        std::system_category());
                }
                if (written == 0)
                {
                    throw std::ios_base::failure("Could not write log file.");
                }
                offset += written;
            }
#else
            std::size_t offset = 0;
            while (offset < bytes.size())
            {
                const ssize_t written = ::write(_fd, bytes.data() + offset,
                    bytes.size() - offset);
                if (written == -1)
                {
                    if (errno == EINTR)
                    {
                        continue;
                    }
                    throw std::system_error(errno, std::generic_category());
                }
                if (written == 0)
                {
                    throw std::ios_base::failure("Could not write log file.");
                }
                offset += static_cast<std::size_t>(written);
            }
#endif
        }

        void Flush()
        {
            // FileStream.Flush() is needed by StreamWriter to empty the
            // managed buffers; this sink writes directly to the OS on every
            // call, so there is no corresponding user-space buffer here.
        }

        void Dispose() noexcept
        {
            CloseNoThrow();
        }

#if defined(_WIN32)
        [[nodiscard]] HANDLE Handle() const noexcept
        {
            return _handle;
        }
#else
        [[nodiscard]] int Descriptor() const noexcept
        {
            return _fd;
        }
#endif

    private:
        void CloseNoThrow() noexcept
        {
#if defined(_WIN32)
            if (_handle != INVALID_HANDLE_VALUE)
            {
                (void)CloseHandle(_handle);
                _handle = INVALID_HANDLE_VALUE;
            }
#else
            if (_fd != -1)
            {
                int result = 0;
                do
                {
                    result = ::close(_fd);
                }
                while (result == -1 && errno == EINTR);
                _fd = -1;
            }
#endif
        }

#if defined(_WIN32)
        HANDLE _handle = INVALID_HANDLE_VALUE;
#else
        int _fd = -1;
#endif
    };

    class Utf8Writer final
    {
    public:
        explicit Utf8Writer(std::string_view path)
            : _stream(std::make_shared<FileSink>(path))
        {
            // StreamWriter(stream, Encoding.UTF8) followed by AutoFlush=true
            // flushes the Encoding.UTF8 preamble even before the first line.
            static constexpr char Utf8Preamble[] = "\xEF\xBB\xBF";
            _stream->Write(std::string_view(Utf8Preamble, 3));
        }

        void Write(std::string_view value)
        {
            _stream->Write(value);
            _stream->Flush();
        }

        void Write(char value)
        {
            _stream->Write(std::string_view(&value, 1));
            _stream->Flush();
        }

        void WriteLine(std::string_view value)
        {
            _stream->Write(value);
            _stream->Write(::MphRead::NativeRuntime::EnvironmentNewLine());
            _stream->Flush();
        }

        void WriteNullLine()
        {
            _stream->Write(::MphRead::NativeRuntime::EnvironmentNewLine());
            _stream->Flush();
        }

        void Flush()
        {
            _stream->Flush();
        }

        void Dispose() noexcept
        {
            _stream->Dispose();
        }

    private:
        std::shared_ptr<FileSink> _stream;
    };

    struct State;
    State& GetState();

    class TeeBuffer final : public std::streambuf
    {
    public:
        explicit TeeBuffer(std::streambuf* console) : _console(console) {}

    protected:
        int_type overflow(int_type value) override;
        std::streamsize xsputn(const char* data, std::streamsize count) override;
        int sync() override;

    private:
        std::streambuf* _console;
    };

    struct State final
    {
        ::MphRead::NativeRuntime::AtomicSharedPtr<Utf8Writer> Writer{};
        ::MphRead::NativeRuntime::AtomicSharedPtr<const std::string> Path{};
        ::MphRead::NativeRuntime::AtomicSharedPtr<const std::string> NativePath{};
        ::MphRead::NativeRuntime::AtomicSharedPtr<FileSink> NativeStream{};
        std::recursive_mutex Lock;
        std::atomic<bool> Hooked{false};
        std::atomic<bool> Forced{false};
        std::streambuf* ConsoleWas = nullptr;
        std::unique_ptr<TeeBuffer> Tee{};
        std::mutex ConsoleStateLock;
        std::atomic<std::uint32_t> HookSubscriptions{0};
        std::once_flag ProcessExitHookOnce;
        std::once_flag TerminateHookOnce;
        std::terminate_handler PreviousTerminate = nullptr;
    };

    State& GetState()
    {
        // Static managed state lives to process exit. Intentionally leak the
        // native carrier so atexit/terminate hooks can still use it after the
        // normal C++ static-destruction phase has begun.
        static State* state = new State();
        return *state;
    }

    TeeBuffer::int_type TeeBuffer::overflow(int_type value)
    {
        if (traits_type::eq_int_type(value, traits_type::eof()))
        {
            return traits_type::not_eof(value);
        }

        const char character = traits_type::to_char_type(value);
        if (traits_type::eq_int_type(_console->sputc(character), traits_type::eof()))
        {
            return traits_type::eof();
        }

        State& state = GetState();
        std::lock_guard<std::recursive_mutex> guard(state.Lock);
        if (std::shared_ptr<Utf8Writer> writer = state.Writer.load())
        {
            writer->Write(character);
        }
        return value;
    }

    std::streamsize TeeBuffer::xsputn(const char* data, std::streamsize count)
    {
        const std::streamsize written = _console->sputn(data, count);
        if (written != count)
        {
            return written;
        }

        State& state = GetState();
        std::lock_guard<std::recursive_mutex> guard(state.Lock);
        if (std::shared_ptr<Utf8Writer> writer = state.Writer.load())
        {
            writer->Write(std::string_view(data, static_cast<std::size_t>(count)));
        }
        return count;
    }

    int TeeBuffer::sync()
    {
        if (_console->pubsync() != 0)
        {
            return -1;
        }
        State& state = GetState();
        std::lock_guard<std::recursive_mutex> guard(state.Lock);
        if (std::shared_ptr<Utf8Writer> writer = state.Writer.load())
        {
            writer->Flush();
        }
        return 0;
    }

    [[nodiscard]] std::string ReplaceSpaces(std::string_view value)
    {
        std::string result;
        result.reserve(value.size());
        for (char character : value)
        {
            if (character != ' ')
            {
                result.push_back(character);
            }
        }
        return result;
    }

    [[nodiscard]] bool EndsWithAscii(std::string_view value,
        std::string_view suffix) noexcept
    {
        return value.size() >= suffix.size()
            && value.substr(value.size() - suffix.size()) == suffix;
    }

#if defined(_WIN32)
#endif

    [[nodiscard]] bool MatchesPattern(std::string_view name,
        std::string_view pattern) noexcept
    {
        std::string_view suffix;
        if (pattern == "*.log")
        {
            suffix = ".log";
        }
        else if (pattern == "*-native.txt")
        {
            suffix = "-native.txt";
        }
        else
        {
            return false;
        }
#if defined(_WIN32)
        return ::MphRead::NativeRuntime::StringEndsWithOrdinalIgnoreCase(name, suffix);
#else
        return EndsWithAscii(name, suffix);
#endif
    }

    void PrunePattern(const std::string& directory, std::string_view pattern)
    {
        struct Entry final
        {
            std::filesystem::path Path;
            std::filesystem::file_time_type LastWrite;
        };

        std::vector<Entry> files;
        for (const std::filesystem::directory_entry& entry
            : std::filesystem::directory_iterator(PathFromManagedString(directory)))
        {
            std::error_code typeError;
            if (!entry.is_regular_file(typeError) && !entry.is_symlink(typeError))
            {
                continue;
            }
#if defined(_WIN32)
            const std::string name = WideToUtf8(entry.path().filename().native());
#else
            const std::string name = entry.path().filename().string();
#endif
            if (!MatchesPattern(name, pattern))
            {
                continue;
            }
            files.push_back(Entry{entry.path(), entry.last_write_time()});
        }

        ManagedSort(files, [](const Entry& left, const Entry& right)
        {
            return right.LastWrite < left.LastWrite ? -1 : (left.LastWrite < right.LastWrite ? 1 : 0);
        });

        for (std::size_t index = static_cast<std::size_t>(KeepFiles - 1);
            index < files.size(); ++index)
        {
            std::error_code removeError;
            const bool removed = std::filesystem::remove(files[index].Path, removeError);
            (void)removed;
            if (removeError)
            {
                throw std::filesystem::filesystem_error(
                    "Could not delete old log.", files[index].Path, removeError);
            }
        }
    }

    void Prune(const std::string& directory)
    {
        try
        {
            PrunePattern(directory, "*.log");
            PrunePattern(directory, "*-native.txt");
        }
        catch (const std::exception&)
        {
            // A directory that cannot be tidied can still be writable.
        }
    }

    [[nodiscard]] std::string ChangeExtensionToNull(std::string_view path)
    {
        const std::size_t separator = path.find_last_of("/\\");
        const std::size_t period = path.find_last_of('.');
        if (period == std::string_view::npos
            || (separator != std::string_view::npos && period < separator))
        {
            return std::string(path);
        }
        return std::string(path.substr(0, period));
    }

    [[nodiscard]] bool Redirect(const std::shared_ptr<FileSink>& stream) noexcept
    {
#if defined(_WIN32)
        return SetStdHandle(static_cast<DWORD>(-12), stream->Handle()) != FALSE;
#elif defined(__ANDROID__)
        (void)stream;
        return false;
#else
        return ::dup2(stream->Descriptor(), 2) != -1;
#endif
    }

    [[nodiscard]] std::string BooleanText(bool value)
    {
        return value ? "True" : "False";
    }

    [[nodiscard]] std::string WindowModeText()
    {
        const auto raw = static_cast<std::int32_t>(
            MphRead::Mods::Launcher::LauncherPrefs::WindowMode());
        if (raw == 0)
        {
            return "Windowed";
        }
        if (raw == 1)
        {
            return "BorderlessFullscreen";
        }
        if (raw == 2)
        {
            return "ExclusiveFullscreen";
        }
        return std::to_string(raw);
    }

    // System.Runtime.InteropServices.RuntimeInformation, reproduced once in
    // NativeRuntime/System/Runtime.
    [[nodiscard]] std::string ProcessArchitecture()
    {
        return ::MphRead::NativeRuntime::RuntimeInformationProcessArchitecture();
    }

    [[nodiscard]] std::string OsArchitecture()
    {
        return ::MphRead::NativeRuntime::RuntimeInformationOSArchitecture();
    }

    [[nodiscard]] std::string RuntimeArchitecture()
    {
        return OsArchitecture() + "/" + ProcessArchitecture();
    }

    [[nodiscard]] std::string OsVersion()
    {
#if defined(_WIN32)
        using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        if (ntdll != nullptr)
        {
            auto rtlGetVersion = reinterpret_cast<RtlGetVersionFn>(
                GetProcAddress(ntdll, "RtlGetVersion"));
            if (rtlGetVersion != nullptr)
            {
                RTL_OSVERSIONINFOW info{};
                info.dwOSVersionInfoSize = sizeof(info);
                if (rtlGetVersion(&info) == 0)
                {
                    return "Microsoft Windows NT " + std::to_string(info.dwMajorVersion)
                        + "." + std::to_string(info.dwMinorVersion)
                        + "." + std::to_string(info.dwBuildNumber) + ".0";
                }
            }
        }
        return "Microsoft Windows NT";
#else
        struct utsname info{};
        if (::uname(&info) == 0)
        {
#if defined(__APPLE__)
            return std::string("Unix ") + info.release;
#else
            return std::string("Unix ") + info.release;
#endif
        }
        return "Unix";
#endif
    }

    [[nodiscard]] std::string CultureName()
    {
#if defined(_WIN32)
        std::array<wchar_t, LOCALE_NAME_MAX_LENGTH> name{};
        const int length = GetUserDefaultLocaleName(name.data(),
            static_cast<int>(name.size()));
        if (length > 1)
        {
            return WideToUtf8(std::wstring_view(name.data(),
                static_cast<std::size_t>(length - 1)));
        }
        return {};
#else
        const char* locale = std::getenv("LC_ALL");
        if (locale == nullptr || *locale == '\0')
        {
            locale = std::getenv("LC_CTYPE");
        }
        if (locale == nullptr || *locale == '\0')
        {
            locale = std::getenv("LANG");
        }
        if (locale == nullptr || *locale == '\0')
        {
            return {};
        }
        std::string value(locale);
        const std::size_t at = value.find('@');
        if (at != std::string::npos)
        {
            value.resize(at);
        }
        const std::size_t period = value.find('.');
        if (period != std::string::npos)
        {
            value.resize(period);
        }
        if (value == "C" || value == "POSIX")
        {
            return {};
        }
        std::replace(value.begin(), value.end(), '_', '-');
        return value;
#endif
    }

    [[nodiscard]] std::vector<std::string> CommandLineArgs()
    {
#if defined(_WIN32)
        using CommandLineToArgvWFn = LPWSTR*(WINAPI*)(LPCWSTR, int*);
        HMODULE shell = LoadLibraryW(L"shell32.dll");
        if (shell != nullptr)
        {
            auto convert = reinterpret_cast<CommandLineToArgvWFn>(
                GetProcAddress(shell, "CommandLineToArgvW"));
            if (convert != nullptr)
            {
                int count = 0;
                LPWSTR* values = convert(GetCommandLineW(), &count);
                if (values != nullptr)
                {
                    std::vector<std::string> result;
                    result.reserve(static_cast<std::size_t>(count));
                    for (int index = 0; index < count; ++index)
                    {
                        result.push_back(WideToUtf8(values[index]));
                    }
                    LocalFree(values);
                    FreeLibrary(shell);
                    return result;
                }
            }
            FreeLibrary(shell);
        }
#elif defined(__APPLE__)
        int* argc = _NSGetArgc();
        char*** argv = _NSGetArgv();
        if (argc != nullptr && argv != nullptr && *argv != nullptr)
        {
            std::vector<std::string> result;
            result.reserve(static_cast<std::size_t>(*argc));
            for (int index = 0; index < *argc; ++index)
            {
                result.emplace_back((*argv)[index] == nullptr ? "" : (*argv)[index]);
            }
            return result;
        }
#elif defined(__linux__) || defined(__ANDROID__)
        std::ifstream input("/proc/self/cmdline", std::ios::binary);
        if (input)
        {
            const std::string data((std::istreambuf_iterator<char>(input)),
                std::istreambuf_iterator<char>());
            std::vector<std::string> result;
            std::size_t start = 0;
            while (start < data.size())
            {
                const std::size_t end = data.find('\0', start);
                if (end == std::string::npos)
                {
                    result.push_back(data.substr(start));
                    break;
                }
                result.push_back(data.substr(start, end - start));
                start = end + 1;
            }
            if (!result.empty())
            {
                return result;
            }
        }
#endif
        if (const std::optional<std::string> path = EnvironmentProcessPath())
        {
            return {*path};
        }
        return {std::string(MphRead::Mods::Branding::FileName)};
    }

    [[nodiscard]] std::string JoinCommandLineArgs()
    {
        const std::vector<std::string> args = CommandLineArgs();
        std::string result;
        for (std::size_t index = 0; index < args.size(); ++index)
        {
            if (index != 0)
            {
                result.push_back(' ');
            }
            result.append(args[index]);
        }
        return result;
    }

    [[nodiscard]] std::string EnvironmentValue(std::string_view name)
    {
        const std::optional<std::string> value = EnvironmentGetVariable(std::string(name));
        return !value.has_value() || value->empty() ? "(unset)" : *value;
    }

    [[nodiscard]] bool EnvironmentValueIsNullOrEmpty(std::string_view name)
    {
        const std::optional<std::string> value = EnvironmentGetVariable(std::string(name));
        return !value.has_value() || value->empty();
    }

    [[nodiscard]] std::string ProgramVersionText()
    {
        return MphRead::Program::Version.ToString();
    }

    [[nodiscard]] std::exception_ptr InnerException(const std::exception& exception) noexcept
    {
        const auto* nested = dynamic_cast<const std::nested_exception*>(&exception);
        return nested == nullptr ? std::exception_ptr{} : nested->nested_ptr();
    }

#if defined(_WIN32)
    [[nodiscard]] std::string DescribeAddress(const void* address);

    // Where the exception being logged was thrown. The first-chance handler
    // below sees every C++ throw (the runtime raises it as a Windows
    // exception) and records the stack here; the catch that logs it runs
    // much later, after the stack that mattered has unwound. Trivially
    // destructible on purpose: a thread_local with a destructor is not safe
    // at thread exit under MinGW.
    thread_local std::array<void*, 48> ThrowFrames{};
    thread_local USHORT ThrowFrameCount = 0;
    // The exception object last recorded: a plain `throw;` raises the same
    // object again and must not replace where it was first thrown.
    thread_local const void* ThrowObject = nullptr;
    // Set while DebugLog::Exception rethrows a stored exception to read it,
    // so that the rethrow does not replace the record it is about to print.
    thread_local bool ThrowRecordPinned = false;
    // Set while a fault handler is logging: a fault inside the logging is not
    // logged again.
    thread_local bool InFaultHandler = false;
#endif

#if defined(__ANDROID__)
    struct AndroidStackCapture final
    {
        std::array<void*, 64> Frames{};
        std::size_t Count = 0;
    };

    [[nodiscard]] _Unwind_Reason_Code CaptureAndroidFrame(struct _Unwind_Context* context,
        void* state) noexcept
    {
        auto& capture = *static_cast<AndroidStackCapture*>(state);
        if (capture.Count == capture.Frames.size())
        {
            return _URC_END_OF_STACK;
        }
        const _Unwind_Word instruction = _Unwind_GetIP(context);
        if (instruction != 0)
        {
            capture.Frames[capture.Count++] = reinterpret_cast<void*>(
                static_cast<std::uintptr_t>(instruction));
        }
        return _URC_NO_REASON;
    }

    [[nodiscard]] std::vector<void*> CaptureAndroidStack(std::size_t skip)
    {
        // _Unwind_Backtrace includes this helper as its first frame. The
        // callers also skip the same wrapper frames as their POSIX paths.
        AndroidStackCapture capture{};
        (void)_Unwind_Backtrace(CaptureAndroidFrame, &capture);
        const std::size_t first = std::min(skip, capture.Count);
        return std::vector<void*>(capture.Frames.begin() + static_cast<std::ptrdiff_t>(first),
            capture.Frames.begin() + static_cast<std::ptrdiff_t>(capture.Count));
    }

    [[nodiscard]] std::string DescribeAndroidAddress(const void* address)
    {
        Dl_info info{};
        if (::dladdr(address, &info) != 0)
        {
            if (info.dli_sname != nullptr && *info.dli_sname != '\0')
            {
                std::string name(info.dli_sname);
#if defined(__GNUG__)
                int status = -1;
                std::unique_ptr<char, decltype(&std::free)> demangled(
                    abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &status), &std::free);
                if (demangled != nullptr)
                {
                    name = demangled.get();
                }
#endif
                if (info.dli_saddr != nullptr)
                {
                    const auto frame = reinterpret_cast<std::uintptr_t>(address);
                    const auto symbol = reinterpret_cast<std::uintptr_t>(info.dli_saddr);
                    if (frame > symbol)
                    {
                        std::ostringstream suffix;
                        suffix.imbue(std::locale::classic());
                        suffix << "+0x" << std::hex << (frame - symbol);
                        name.append(suffix.str());
                    }
                }
                return name;
            }
            if (info.dli_fname != nullptr && info.dli_fbase != nullptr)
            {
                std::ostringstream module;
                module.imbue(std::locale::classic());
                module << info.dli_fname << "+0x" << std::hex
                    << (reinterpret_cast<std::uintptr_t>(address)
                        - reinterpret_cast<std::uintptr_t>(info.dli_fbase));
                return module.str();
            }
        }
        std::ostringstream fallback;
        fallback.imbue(std::locale::classic());
        fallback << "0x" << std::hex << reinterpret_cast<std::uintptr_t>(address);
        return fallback.str();
    }
#endif

    [[nodiscard]] std::optional<std::string> NativeStackTrace()
    {
#if defined(_WIN32)
        std::array<void*, 64> frames{};
        USHORT count = 0;
        USHORT first = 2;
        bool thrown = false;
        if (ThrowFrameCount != 0)
        {
            count = ThrowFrameCount;
            std::copy_n(ThrowFrames.begin(), count, frames.begin());
            ThrowFrameCount = 0;
            first = 0;
            thrown = true;
        }
        else
        {
            count = CaptureStackBackTrace(0,
                static_cast<DWORD>(frames.size()), frames.data(), nullptr);
        }
        if (count == 0)
        {
            return std::nullopt;
        }
        std::ostringstream result;
        result.imbue(std::locale::classic());
        if (thrown)
        {
            result << "   (thrown from)";
        }
        for (USHORT index = first; index < count; ++index)
        {
            if (index != first || thrown)
            {
                result << ::MphRead::NativeRuntime::EnvironmentNewLine();
            }
            result << "   at " << DescribeAddress(frames[index]);
        }
        const std::string text = result.str();
        return text.empty() ? std::nullopt
            : std::optional<std::string>(text);
#elif defined(__ANDROID__)
        std::string result;
        // Skip this helper, NativeStackTrace, and ExceptionStackTrace, as
        // the POSIX backtrace path skips its first two frames.
        for (const void* frame : CaptureAndroidStack(3))
        {
            if (!result.empty())
            {
                result.append(::MphRead::NativeRuntime::EnvironmentNewLine());
            }
            result.append("   at ");
            result.append(DescribeAndroidAddress(frame));
        }
        return result.empty() ? std::nullopt
            : std::optional<std::string>(std::move(result));
#else
        std::array<void*, 64> frames{};
        const int count = ::backtrace(frames.data(),
            static_cast<int>(frames.size()));
        if (count <= 0)
        {
            return std::nullopt;
        }
        char** symbols = ::backtrace_symbols(frames.data(), count);
        if (symbols == nullptr)
        {
            return std::nullopt;
        }
        std::unique_ptr<char*, decltype(&std::free)> owned(symbols, &std::free);
        std::string result;
        for (int index = 2; index < count; ++index)
        {
            if (!result.empty())
            {
                result.append(::MphRead::NativeRuntime::EnvironmentNewLine());
            }
            result.append("   at ");
            result.append(symbols[index] == nullptr ? "(unknown)" : symbols[index]);
        }
        return result.empty() ? std::nullopt
            : std::optional<std::string>(std::move(result));
#endif
    }

    void FlushWriterNoThrow() noexcept
    {
        try
        {
            State& state = GetState();
            std::lock_guard<std::recursive_mutex> guard(state.Lock);
            if (std::shared_ptr<Utf8Writer> writer = state.Writer.load())
            {
                writer->Flush();
            }
        }
        catch (...)
        {
        }
    }

    void ProcessExitHandler() noexcept
    {
        const std::uint32_t subscriptions = GetState().HookSubscriptions.load();
        for (std::uint32_t index = 0; index < subscriptions; ++index)
        {
            try
            {
                MphRead::Mods::DebugLog::Line("exit", "process exiting");
                FlushWriterNoThrow();
            }
            catch (...)
            {
            }
        }
    }

    [[noreturn]] void TerminateHandler() noexcept
    {
        State& state = GetState();
        const std::exception_ptr exception = std::current_exception();
        const std::uint32_t subscriptions = state.HookSubscriptions.load();
        for (std::uint32_t index = 0; index < subscriptions; ++index)
        {
            try
            {
                MphRead::Mods::DebugLog::Line("crash",
                    "the process is going down with an exception (terminating=True)");
                MphRead::Mods::DebugLog::Exception("crash", exception);
                FlushWriterNoThrow();
            }
            catch (...)
            {
            }
        }

        const std::terminate_handler previous = state.PreviousTerminate;
        if (previous != nullptr && previous != &TerminateHandler)
        {
            previous();
        }
        std::abort();
    }

#if defined(_WIN32)
    // AppDomain.UnhandledException in the C# build: a null dereference or a
    // bad index there is a managed exception, and the log gets its type and
    // its stack before the process goes. The same fault here is a hardware
    // exception that never reaches std::terminate, so without this the log
    // simply stops. Addresses are given as module+offset, and as the address
    // `addr2line -f -C -e FruityPrime.exe` expects (the image's preferred base
    // plus the offset), since the loader puts the image somewhere else.
    LPTOP_LEVEL_EXCEPTION_FILTER PreviousFaultFilter = nullptr;

    // The nearest function symbol at or below an RVA of this executable, from
    // the COFF symbol table a MinGW link leaves in the image unless it is
    // stripped. Read once, on the first fault.
    [[nodiscard]] std::string ExecutableSymbol(std::uintptr_t rva)
    {
        struct Function
        {
            std::uint32_t Rva;
            std::string Name;
        };
        // Never destroyed: abort() and the fault handlers can ask for a name
        // after static destructors have run at exit.
        static std::vector<Function>& functions = *new std::vector<Function>();
        static bool loaded = false;
        if (!loaded)
        {
            loaded = true;
            std::array<char, MAX_PATH> path{};
            const DWORD length = ::GetModuleFileNameA(nullptr, path.data(), static_cast<DWORD>(path.size()));
            std::ifstream file(std::string(path.data(), length), std::ios::binary);
            IMAGE_DOS_HEADER dos{};
            IMAGE_NT_HEADERS nt{};
            if (!file.read(reinterpret_cast<char*>(&dos), sizeof(dos)) || !file.seekg(dos.e_lfanew)
                || !file.read(reinterpret_cast<char*>(&nt), sizeof(nt)) || nt.Signature != IMAGE_NT_SIGNATURE
                || nt.FileHeader.PointerToSymbolTable == 0 || nt.FileHeader.NumberOfSymbols == 0)
            {
                return {};
            }
            std::vector<IMAGE_SECTION_HEADER> sections(nt.FileHeader.NumberOfSections);
            file.seekg(dos.e_lfanew + 4 + static_cast<std::streamoff>(sizeof(IMAGE_FILE_HEADER))
                + nt.FileHeader.SizeOfOptionalHeader);
            file.read(reinterpret_cast<char*>(sections.data()),
                static_cast<std::streamsize>(sections.size() * sizeof(IMAGE_SECTION_HEADER)));
            const std::size_t count = nt.FileHeader.NumberOfSymbols;
            std::vector<std::uint8_t> table(count * 18);
            file.seekg(nt.FileHeader.PointerToSymbolTable);
            file.read(reinterpret_cast<char*>(table.data()), static_cast<std::streamsize>(table.size()));
            std::uint32_t stringsSize = 0;
            file.read(reinterpret_cast<char*>(&stringsSize), sizeof(stringsSize));
            std::vector<char> strings(stringsSize > 4 ? stringsSize : 4);
            file.read(strings.data() + 4, static_cast<std::streamsize>(strings.size() - 4));
            if (!file)
            {
                return {};
            }
            for (std::size_t index = 0; index < count; ++index)
            {
                const std::uint8_t* entry = table.data() + index * 18;
                std::uint32_t value = 0;
                std::int16_t section = 0;
                std::uint16_t type = 0;
                std::memcpy(&value, entry + 8, 4);
                std::memcpy(&section, entry + 12, 2);
                std::memcpy(&type, entry + 14, 2);
                const std::uint8_t aux = entry[17];
                if (section > 0 && static_cast<std::size_t>(section) <= sections.size() && (type & 0xF0) == 0x20)
                {
                    std::string name;
                    std::uint32_t zero = 0;
                    std::memcpy(&zero, entry, 4);
                    if (zero == 0)
                    {
                        std::uint32_t offset = 0;
                        std::memcpy(&offset, entry + 4, 4);
                        if (offset < strings.size())
                        {
                            name = std::string(strings.data() + offset);
                        }
                    }
                    else
                    {
                        name = std::string(reinterpret_cast<const char*>(entry),
                            strnlen(reinterpret_cast<const char*>(entry), 8));
                    }
                    functions.push_back({sections[static_cast<std::size_t>(section - 1)].VirtualAddress + value,
                        std::move(name)});
                }
                index += aux;
            }
            std::sort(functions.begin(), functions.end(),
                [](const Function& left, const Function& right) { return left.Rva < right.Rva; });
        }
        auto after = std::upper_bound(functions.begin(), functions.end(), rva,
            [](std::uintptr_t value, const Function& function) { return value < function.Rva; });
        if (after == functions.begin())
        {
            return {};
        }
        const Function& function = *std::prev(after);
        std::string name = function.Name;
#if defined(__GNUG__)
        int status = 0;
        std::unique_ptr<char, decltype(&std::free)> demangled(
            abi::__cxa_demangle(name.c_str(), nullptr, nullptr, &status), &std::free);
        if (status == 0 && demangled)
        {
            name = demangled.get();
        }
#endif
        std::ostringstream text;
        text.imbue(std::locale::classic());
        text << name << "+0x" << std::hex << std::uppercase << (rva - function.Rva);
        return text.str();
    }

    [[nodiscard]] std::string DescribeAddress(const void* address)
    {
        std::ostringstream text;
        text.imbue(std::locale::classic());
        HMODULE module = nullptr;
        if (::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                static_cast<LPCWSTR>(address), &module) && module != nullptr)
        {
            std::array<char, MAX_PATH> path{};
            const DWORD length = ::GetModuleFileNameA(module, path.data(),
                static_cast<DWORD>(path.size()));
            std::string name(path.data(), length);
            const std::size_t slash = name.find_last_of("\\/");
            if (slash != std::string::npos)
            {
                name = name.substr(slash + 1);
            }
            const auto base = reinterpret_cast<std::uintptr_t>(module);
            const std::uintptr_t offset = reinterpret_cast<std::uintptr_t>(address) - base;
            text << name << "+0x" << std::hex << std::uppercase << offset;
            // The loader rewrites ImageBase in the mapped header when it
            // relocates the image, so the preferred base is read off disk.
            std::ifstream file(std::string(path.data(), length), std::ios::binary);
            IMAGE_DOS_HEADER dos{};
            IMAGE_NT_HEADERS nt{};
            if (file.read(reinterpret_cast<char*>(&dos), sizeof(dos))
                && file.seekg(dos.e_lfanew)
                && file.read(reinterpret_cast<char*>(&nt), sizeof(nt))
                && nt.Signature == IMAGE_NT_SIGNATURE)
            {
                text << " (addr2line 0x"
                    << (static_cast<std::uintptr_t>(nt.OptionalHeader.ImageBase) + offset) << ")";
            }
            if (module == ::GetModuleHandleW(nullptr))
            {
                const std::string symbol = ExecutableSymbol(offset);
                if (!symbol.empty())
                {
                    text << " " << symbol;
                }
            }
        }
        else
        {
            text << "0x" << std::hex << std::uppercase << reinterpret_cast<std::uintptr_t>(address);
        }
        return text.str();
    }

    LONG WINAPI NativeFaultFilter(EXCEPTION_POINTERS* pointers)
    {
        try
        {
            const EXCEPTION_RECORD& record = *pointers->ExceptionRecord;
            std::ostringstream head;
            head.imbue(std::locale::classic());
            head << "the process is going down with a native fault 0x" << std::hex << std::uppercase
                << static_cast<std::uint32_t>(record.ExceptionCode) << std::dec
                << " on thread " << ::GetCurrentThreadId()
                << " at " << DescribeAddress(record.ExceptionAddress);
            if (record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record.NumberParameters >= 2)
            {
                head << (record.ExceptionInformation[0] == 0 ? ", reading 0x"
                    : record.ExceptionInformation[0] == 1 ? ", writing 0x" : ", executing 0x")
                    << std::hex << std::uppercase << record.ExceptionInformation[1];
            }
            MphRead::Mods::DebugLog::Line("crash", head.str());
            std::array<void*, 62> frames{};
            const USHORT count = ::CaptureStackBackTrace(0,
                static_cast<DWORD>(frames.size()), frames.data(), nullptr);
            for (USHORT index = 0; index < count; ++index)
            {
                MphRead::Mods::DebugLog::Line("crash", "   at " + DescribeAddress(frames[index]));
            }
            FlushWriterNoThrow();
        }
        catch (...)
        {
        }
        return PreviousFaultFilter != nullptr ? PreviousFaultFilter(pointers) : EXCEPTION_CONTINUE_SEARCH;
    }

    // The faults the unhandled-exception filter above never hears about.
    //
    // A heap the process has corrupted, a stack cookie that no longer
    // matches, and a stack that has run out all end the process from inside
    // ntdll, or are caught by a handler further up that is not ours, and the
    // log simply stops. A vectored handler sees every exception first, before
    // any frame-based handler has had a say, so these are written here as
    // first-chance lines -- at most a few, since a driver may probe memory
    // and handle the fault itself -- and the search then carries on unchanged.
    std::atomic<int> FirstChanceFaultsLogged{0};
    constexpr int FirstChanceFaultLimit = 16;

    [[nodiscard]] bool IsFatalCode(DWORD code) noexcept
    {
        switch (code)
        {
        case EXCEPTION_ACCESS_VIOLATION:
        case EXCEPTION_ILLEGAL_INSTRUCTION:
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
        case EXCEPTION_PRIV_INSTRUCTION:
        case EXCEPTION_STACK_OVERFLOW:
        case EXCEPTION_IN_PAGE_ERROR:
        case 0xC0000374U: // STATUS_HEAP_CORRUPTION
        case 0xC0000409U: // STATUS_STACK_BUFFER_OVERRUN
        case 0xC0000420U: // STATUS_ASSERTION_FAILURE
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] bool ThrownByRethrowException(void* const* frames, USHORT count) noexcept
    {
#if defined(_MSC_VER)
        // MSVC's std::rethrow_exception is msvcp140's __ExceptionPtrRethrow:
        // no function in this image to compare against, so look for a frame
        // inside that DLL instead.
        static const HMODULE msvcp = ::GetModuleHandleW(L"msvcp140.dll");
        if (msvcp != nullptr)
        {
            for (USHORT index = 0; index < count && index < 12; ++index)
            {
                HMODULE module = nullptr;
                if (::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                        | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                        static_cast<LPCWSTR>(frames[index]), &module)
                    && module == msvcp)
                {
                    return true;
                }
            }
        }
#endif
#if defined(_M_X64) || defined(__x86_64__)
        static const DWORD64 rethrowStart = []() -> DWORD64
        {
            void (*rethrow)(std::exception_ptr) = &std::rethrow_exception;
            DWORD64 imageBase = 0;
            const PRUNTIME_FUNCTION function = ::RtlLookupFunctionEntry(
                reinterpret_cast<DWORD64>(rethrow), &imageBase, nullptr);
            return function == nullptr ? 0 : imageBase + function->BeginAddress;
        }();
        if (rethrowStart == 0)
        {
            return false;
        }
        for (USHORT index = 0; index < count && index < 12; ++index)
        {
            DWORD64 imageBase = 0;
            const PRUNTIME_FUNCTION function = ::RtlLookupFunctionEntry(
                reinterpret_cast<DWORD64>(frames[index]), &imageBase, nullptr);
            if (function != nullptr && imageBase + function->BeginAddress == rethrowStart)
            {
                return true;
            }
        }
#else
        (void)frames;
        (void)count;
#endif
        return false;
    }

    LONG CALLBACK FirstChanceFaultHandler(EXCEPTION_POINTERS* pointers)
    {
        const EXCEPTION_RECORD& record = *pointers->ExceptionRecord;
        if (record.ExceptionCode == 0x20474343U || record.ExceptionCode == 0xE06D7363U)
        {
            // A C++ throw (GCC's SEH unwinder, or MSVC's): remember where.
            const std::size_t objectIndex = record.ExceptionCode == 0xE06D7363U ? 1 : 0;
            const void* object = record.NumberParameters > objectIndex
                ? reinterpret_cast<const void*>(record.ExceptionInformation[objectIndex]) : nullptr;
            if (!ThrowRecordPinned && (object == nullptr || object != ThrowObject))
            {
                std::array<void*, 48> frames{};
                const USHORT count = ::CaptureStackBackTrace(1,
                    static_cast<DWORD>(frames.size()), frames.data(), nullptr);
                // std::rethrow_exception is how a stored exception is read,
                // by the log and by whatever turns it into a message: a new
                // throw of an old exception, which must not replace where
                // that exception came from.
                if (!ThrownByRethrowException(frames.data(), count))
                {
                    ThrowObject = object;
                    ThrowFrames = frames;
                    ThrowFrameCount = count;
                }
            }
            return EXCEPTION_CONTINUE_SEARCH;
        }
        if (!IsFatalCode(record.ExceptionCode)
            || FirstChanceFaultsLogged.fetch_add(1) >= FirstChanceFaultLimit)
        {
            return EXCEPTION_CONTINUE_SEARCH;
        }
        // First a line that needs no heap, since the heap may be what broke.
        // Standard error is the -native.txt file beside the log.
        {
            char line[160];
            const auto executable = reinterpret_cast<std::uintptr_t>(::GetModuleHandleW(nullptr));
            const auto address = reinterpret_cast<std::uintptr_t>(record.ExceptionAddress);
            const int length = std::snprintf(line, sizeof(line),
                "[crash] first chance 0x%08lX on thread %lu at 0x%llX (FruityPrime.exe+0x%llX)\r\n",
                static_cast<unsigned long>(record.ExceptionCode),
                static_cast<unsigned long>(::GetCurrentThreadId()),
                static_cast<unsigned long long>(address),
                static_cast<unsigned long long>(address - executable));
            DWORD written = 0;
            if (length > 0)
            {
                ::WriteFile(::GetStdHandle(STD_ERROR_HANDLE), line,
                    static_cast<DWORD>(std::min<int>(length, static_cast<int>(sizeof(line) - 1))),
                    &written, nullptr);
            }
        }
        if (record.ExceptionCode == 0xC0000374U || record.ExceptionCode == EXCEPTION_STACK_OVERFLOW
            || InFaultHandler)
        {
            return EXCEPTION_CONTINUE_SEARCH;
        }
        InFaultHandler = true;
        struct Leave final
        {
            ~Leave()
            {
                InFaultHandler = false;
            }
        } leave;
        try
        {
            std::ostringstream head;
            head.imbue(std::locale::classic());
            head << "first chance 0x" << std::hex << std::uppercase
                << static_cast<std::uint32_t>(record.ExceptionCode) << std::dec
                << " on thread " << ::GetCurrentThreadId()
                << " at " << DescribeAddress(record.ExceptionAddress);
            if (record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record.NumberParameters >= 2)
            {
                head << (record.ExceptionInformation[0] == 0 ? ", reading 0x"
                    : record.ExceptionInformation[0] == 1 ? ", writing 0x" : ", executing 0x")
                    << std::hex << std::uppercase << record.ExceptionInformation[1];
            }
            MphRead::Mods::DebugLog::Line("crash", head.str());
            if (record.ExceptionCode != EXCEPTION_STACK_OVERFLOW)
            {
                std::array<void*, 32> frames{};
                const USHORT count = ::CaptureStackBackTrace(0,
                    static_cast<DWORD>(frames.size()), frames.data(), nullptr);
                for (USHORT index = 0; index < count; ++index)
                {
                    MphRead::Mods::DebugLog::Line("crash", "   at " + DescribeAddress(frames[index]));
                }
            }
            FlushWriterNoThrow();
        }
        catch (...)
        {
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }

    // abort() raises SIGABRT and then leaves with STATUS_FATAL_APP_EXIT,
    // through no exception anything above would see. Where it was called
    // from is written first; returning lets the process end as it would have.
    extern "C" void AbortSignalHandler(int)
    {
        if (InFaultHandler)
        {
            return;
        }
        InFaultHandler = true;
        try
        {
            MphRead::Mods::DebugLog::Line("crash", "the process is going down through abort() on thread "
                + std::to_string(::GetCurrentThreadId()));
            std::array<void*, 32> frames{};
            const USHORT count = ::CaptureStackBackTrace(0,
                static_cast<DWORD>(frames.size()), frames.data(), nullptr);
            for (USHORT index = 0; index < count; ++index)
            {
                MphRead::Mods::DebugLog::Line("crash", "   at " + DescribeAddress(frames[index]));
            }
            FlushWriterNoThrow();
        }
        catch (...)
        {
        }
    }

    // A watchdog must never suspend the render thread to inspect it.
    //
    // The window thread can be inside GLFW, OpenGL, the vendor ICD, the CRT,
    // or another subsystem while it owns locks. SuspendThread stops it at an
    // arbitrary instruction. If that happens while one of those locks is
    // owned, the diagnostic can turn a recoverable stall into a deadlock.
    // Keep this watchdog passive: the heartbeat still tells us that the loop
    // stopped turning over, while the GPU trace/log lines written before the
    // stall identify the last completed work without touching the stuck
    // thread.
    constexpr std::int64_t FreezeReportMilliseconds = 5000;
    constexpr std::int64_t FreezeResampleMilliseconds = 10000;

    void WriteFreezeNotice(DWORD threadId, std::int64_t stalledFor)
    {
        std::ostringstream head;
        head.imbue(std::locale::classic());
        head << "the window thread " << threadId << " has not come back for "
            << stalledFor << " ms; passive watchdog left the thread running";
        MphRead::Mods::DebugLog::Line("freeze", head.str());
        FlushWriterNoThrow();
    }

    void RunFreezeWatchdog()
    {
        std::int64_t reportedBeat = 0;
        std::int64_t lastSampleAt = 0;
        for (;;)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            try
            {
                if (!MphRead::Mods::DebugLog::Active())
                {
                    continue;
                }
                const std::int64_t beat = MphRead::NativeRuntime::LastFrameHeartbeat();
                if (beat == 0)
                {
                    continue;
                }
                const std::int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
                const std::int64_t stalled = now - beat;
                if (reportedBeat != 0 && reportedBeat != beat)
                {
                    std::ostringstream line;
                    line.imbue(std::locale::classic());
                    line << "the window thread came back after "
                        << (lastSampleAt - reportedBeat) << "+ ms";
                    MphRead::Mods::DebugLog::Line("freeze", line.str());
                    FlushWriterNoThrow();
                    reportedBeat = 0;
                }
                if (stalled < FreezeReportMilliseconds)
                {
                    continue;
                }
                const DWORD threadId = static_cast<DWORD>(MphRead::NativeRuntime::FrameHeartbeatThread());
                if (threadId == 0)
                {
                    continue;
                }
                if (reportedBeat != beat)
                {
                    reportedBeat = beat;
                    lastSampleAt = now;
                    WriteFreezeNotice(threadId, stalled);
                }
                else if (now - lastSampleAt >= FreezeResampleMilliseconds)
                {
                    lastSampleAt = now;
                    WriteFreezeNotice(threadId, stalled);
                }
            }
            catch (...)
            {
            }
        }
    }
#endif

    void Hook()
    {
        State& state = GetState();
        if (state.Hooked.load())
        {
            return;
        }
        state.Hooked.store(true);

        {
            std::lock_guard<std::mutex> consoleGuard(state.ConsoleStateLock);
            state.ConsoleWas = std::cout.rdbuf();
            state.Tee = std::make_unique<TeeBuffer>(state.ConsoleWas);
            std::cout.rdbuf(state.Tee.get());
        }

        state.HookSubscriptions.fetch_add(1);
        std::call_once(state.ProcessExitHookOnce, []
        {
            (void)std::atexit(&ProcessExitHandler);
        });
        std::call_once(state.TerminateHookOnce, [&state]
        {
            state.PreviousTerminate = std::set_terminate(&TerminateHandler);
#if defined(_WIN32)
            PreviousFaultFilter = ::SetUnhandledExceptionFilter(&NativeFaultFilter);
            // Do not install a vectored first-chance handler here. It runs
            // before a driver/CRT/OS component gets a chance to handle its own
            // exception. Logging an otherwise recoverable probe while that
            // component holds internal locks can allocate, resolve modules and
            // perform file I/O inside the exception path, turning a harmless
            // first-chance exception into a process or system-wide deadlock.
            // The unhandled-exception filter above still records faults that
            // actually escape their owner, which is the safe point to log them.
            std::signal(SIGABRT, &AbortSignalHandler);
            std::thread(&RunFreezeWatchdog).detach();
#endif
        });
    }

    void CaptureNativeErrors()
    {
        State& state = GetState();
        const std::shared_ptr<const std::string> logPath = state.Path.load();
        if (!logPath)
        {
            return;
        }

        try
        {
            const std::string path = ChangeExtensionToNull(*logPath) + "-native.txt";
            std::shared_ptr<FileSink> stream = std::make_shared<FileSink>(path);
            state.NativeStream.store(stream);
            if (!Redirect(stream))
            {
                stream->Dispose();
                state.NativeStream.store(nullptr);
                return;
            }

            state.NativePath.store(std::make_shared<const std::string>(path));
            MphRead::Mods::DebugLog::Line("crash",
                "native stderr is being captured to " + path);
            std::cout << "[debug] standard error is being written to " << path << std::endl;

            if (EnvironmentValueIsNullOrEmpty("DOTNET_DbgEnableMiniDump"))
            {
                const std::string directory = PathGetDirectoryName(*logPath).value_or(".");
                MphRead::Mods::DebugLog::Line("crash",
                    "no crash dump is configured. For a native stack from the "
                    "next crash, start the game with these three set (type 4 is "
                    "required -- a single-file app supports no other kind, and the "
                    "file is around 110 MB, which zips well):");
                MphRead::Mods::DebugLog::Line("crash",
                    "  DOTNET_DbgEnableMiniDump=1 DOTNET_DbgMiniDumpType=4 "
                    "DOTNET_DbgMiniDumpName=" + PathCombine(directory, "crash-%p.dmp"));
            }
        }
        catch (const std::exception& ex)
        {
            MphRead::Mods::DebugLog::Line("crash",
                "native stderr could not be captured: " + std::string(ex.what()));
        }
    }

    void WriteDisplay()
    {
#if defined(_WIN32) || defined(__APPLE__)
        return;
#else
        MphRead::Mods::DebugLog::Line("display",
            "session=" + EnvironmentValue("XDG_SESSION_TYPE")
            + " desktop=" + EnvironmentValue("XDG_CURRENT_DESKTOP"));
        MphRead::Mods::DebugLog::Line("display",
            "DISPLAY=" + EnvironmentValue("DISPLAY")
            + " WAYLAND_DISPLAY=" + EnvironmentValue("WAYLAND_DISPLAY"));
#endif
    }

    void WriteHeader()
    {
        using MphRead::Mods::Branding;
        using MphRead::Mods::DebugLog;
        using MphRead::Mods::Launcher::GameFiles;
        using MphRead::Mods::Launcher::LauncherPrefs;
        using MphRead::Mods::Network::NetConfig;
        using MphRead::Mods::Network::NetLag;
        using MphRead::Mods::RenderOptions;
        using MphRead::Mods::Update::BuildVersion;

        DebugLog::Line("build", std::string(Branding::Name) + " "
            + BuildVersion::Display() + ", data format " + ProgramVersionText());
        DebugLog::Line("build", "protocol " + std::to_string(NetConfig::ProtocolVersion)
            + ", log started " + ::MphRead::NativeRuntime::DateTimeToString(::MphRead::NativeRuntime::DateTimeNow(), "yyyy-MM-dd HH:mm:ss zzz"));
        DebugLog::Line("system", OsVersion() + " " + RuntimeArchitecture()
            + ", .NET native, " + std::to_string(::MphRead::NativeRuntime::EnvironmentProcessorCount()) + " cpu(s)");
        DebugLog::Line("system", "64-bit process=" + BooleanText(sizeof(void*) == 8)
            + ", culture=" + CultureName());
        DebugLog::Line("paths", "base=" + AppContextBaseDirectory());
        DebugLog::Line("paths", "prefs=" + LauncherPrefs::Directory());
        const std::optional<std::string> path = DebugLog::Path();
        DebugLog::Line("paths", "log=" + path.value_or(""));
        try
        {
            DebugLog::Line("paths", "game files ready=" + BooleanText(GameFiles::Ready()));
        }
        catch (const std::exception& ex)
        {
            DebugLog::Line("paths", "game files could not be checked: "
                + std::string(ex.what()));
        }
        DebugLog::Line("args", JoinCommandLineArgs());
        WriteDisplay();
        DebugLog::Line("render", std::string("cel=")
            + std::string(RenderOptions::OnOff(RenderOptions::CelShading()))
            + " fog=" + std::string(RenderOptions::OnOff(RenderOptions::Fog()))
            + " window=" + WindowModeText());
        if (NetLag::Active())
        {
            DebugLog::Line("net", "simulated line: " + NetLag::Describe().value_or(""));
        }
    }
}

namespace MphRead::Mods
{
    bool DebugLog::Active() noexcept
    {
        return static_cast<bool>(GetState().Writer.load());
    }

    std::optional<std::string> DebugLog::Path()
    {
        if (std::shared_ptr<const std::string> path = GetState().Path.load())
        {
            return *path;
        }
        return std::nullopt;
    }

    std::optional<std::string> DebugLog::NativePath()
    {
        if (std::shared_ptr<const std::string> path = GetState().NativePath.load())
        {
            return *path;
        }
        return std::nullopt;
    }

    void DebugLog::Force() noexcept
    {
        GetState().Forced.store(true);
    }

    void DebugLog::Attach()
    {
        State& state = GetState();
        if (state.Writer.load()
            || (!state.Forced.load() && !Launcher::LauncherPrefs::DebugLogs()))
        {
            return;
        }

        try
        {
            const std::string directory = PathCombine(
                Launcher::LauncherPrefs::Directory(), "logs");
            std::filesystem::create_directories(PathFromManagedString(directory));
            Prune(directory);
            // The process id is not decoration: a thumbnail batch starts
            // several workers inside one second, and a name good only to
            // the second had all of them truncating and writing over one
            // file at once -- which reads as a corrupted log rather than
            // as several.
            const std::string name = ReplaceSpaces(Branding::Name) + "-"
                + ::MphRead::NativeRuntime::DateTimeToString(::MphRead::NativeRuntime::DateTimeNow(), "yyyyMMdd-HHmmss") + "-"
                + std::to_string(::MphRead::NativeRuntime::EnvironmentProcessId()) + ".log";
            const std::string path = PathCombine(directory, name);
            state.Path.store(std::make_shared<const std::string>(path));
            state.Writer.store(std::make_shared<Utf8Writer>(path));
        }
        catch (const std::exception& ex)
        {
            state.Writer.store(nullptr);
            std::cout << "[debug] could not open a log: " << ex.what() << std::endl;
            return;
        }

        Hook();
        CaptureNativeErrors();
        WriteHeader();
    }

    void DebugLog::Detach()
    {
        State& state = GetState();
        std::lock_guard<std::recursive_mutex> guard(state.Lock);
        {
            std::lock_guard<std::mutex> consoleGuard(state.ConsoleStateLock);
            if (state.ConsoleWas != nullptr)
            {
                std::cout.rdbuf(state.ConsoleWas);
                state.ConsoleWas = nullptr;
                state.Tee.reset();
            }
        }
        if (std::shared_ptr<Utf8Writer> writer = state.Writer.load())
        {
            writer->Flush();
        }
        if (std::shared_ptr<Utf8Writer> writer = state.Writer.load())
        {
            writer->Dispose();
        }
        state.Writer.store(nullptr);
        state.Hooked.store(false);
    }

    void DebugLog::Line(std::string_view category, std::string_view message)
    {
        State& state = GetState();
        if (!state.Writer.load())
        {
            return;
        }
        std::lock_guard<std::recursive_mutex> guard(state.Lock);
        if (std::shared_ptr<Utf8Writer> writer = state.Writer.load())
        {
            writer->WriteLine("[" + ::MphRead::NativeRuntime::DateTimeToString(::MphRead::NativeRuntime::DateTimeNow(), "HH:mm:ss.fff") + "] [" + std::string(category)
                + "] " + std::string(message));
        }
    }

    std::vector<void*> DebugLog::CaptureStack()
    {
        std::vector<void*> frames(32);
#if defined(_WIN32)
        frames.resize(::CaptureStackBackTrace(1, static_cast<DWORD>(frames.size()), frames.data(), nullptr));
#elif !defined(__ANDROID__)
        const int count = ::backtrace(frames.data(), static_cast<int>(frames.size()));
        frames.resize(count > 1 ? static_cast<std::size_t>(count) : 0U);
        if (!frames.empty())
        {
            frames.erase(frames.begin());
        }
#else
        // Skip this helper and CaptureStack itself, as the POSIX path does.
        frames = CaptureAndroidStack(2);
#endif
        return frames;
    }

    void DebugLog::StackFrom(std::string_view category, std::string_view message,
        const std::vector<void*>& frames)
    {
        if (!Active())
        {
            return;
        }
        Line(category, message);
#if defined(_WIN32)
        for (void* frame : frames)
        {
            Line(category, "   at " + DescribeAddress(frame));
        }
#elif !defined(__ANDROID__)
        if (!frames.empty())
        {
            std::unique_ptr<char*, decltype(&std::free)> symbols(
                ::backtrace_symbols(frames.data(), static_cast<int>(frames.size())), &std::free);
            for (std::size_t index = 0; symbols && index < frames.size(); ++index)
            {
                Line(category, std::string("   at ") + (symbols.get()[index] ? symbols.get()[index] : "?"));
            }
        }
#else
        for (const void* frame : frames)
        {
            Line(category, std::string("   at ") + DescribeAndroidAddress(frame));
        }
#endif
        FlushWriterNoThrow();
    }

    void DebugLog::Stack(std::string_view category, std::string_view message)
    {
        if (!Active())
        {
            return;
        }
        Line(category, message);
#if defined(_WIN32)
        std::array<void*, 32> frames{};
        const USHORT count = ::CaptureStackBackTrace(1,
            static_cast<DWORD>(frames.size()), frames.data(), nullptr);
        for (USHORT index = 0; index < count; ++index)
        {
            Line(category, "   at " + DescribeAddress(frames[index]));
        }
#elif !defined(__ANDROID__)
        std::array<void*, 32> frames{};
        const int count = ::backtrace(frames.data(), static_cast<int>(frames.size()));
        if (count > 1)
        {
            std::unique_ptr<char*, decltype(&std::free)> symbols(
                ::backtrace_symbols(frames.data(), count), &std::free);
            for (int index = 1; symbols && index < count; ++index)
            {
                Line(category, std::string("   at ") + (symbols.get()[index] ? symbols.get()[index] : "?"));
            }
        }
#else
        // Skip this helper and Stack itself, as the POSIX path does.
        for (const void* frame : CaptureAndroidStack(2))
        {
            Line(category, std::string("   at ") + DescribeAndroidAddress(frame));
        }
#endif
        FlushWriterNoThrow();
    }

    std::optional<std::string> DebugLog::ExceptionStackTrace(
        const std::exception& exception)
    {
        (void)exception;
        return NativeStackTrace();
    }

    void DebugLog::Exception(std::string_view category,
        const std::exception& exception)
    {
        if (!Active())
        {
            return;
        }
        Line(category, ExceptionTypeName(exception) + ": " + exception.what());
        {
            State& state = GetState();
            std::lock_guard<std::recursive_mutex> guard(state.Lock);
            if (std::shared_ptr<Utf8Writer> writer = state.Writer.load())
            {
                // The CLR stores the throw-site stack on Exception itself.
                // Standard C++ exceptions do not, so the closest platform
                // mechanism is the native stack available at the catch/log
                // boundary. It is written raw, with the same one trailing
                // newline as TextWriter.WriteLine(ex.StackTrace).
                if (std::optional<std::string> stack = ExceptionStackTrace(exception))
                {
                    writer->WriteLine(*stack);
                }
                else
                {
                    writer->WriteNullLine();
                }
            }
        }
        if (std::exception_ptr inner = InnerException(exception))
        {
            Line(category, "caused by:");
            Exception(category, inner);
        }
    }

    void DebugLog::Exception(std::string_view category,
        std::exception_ptr exception)
    {
        if (!Active() || !exception)
        {
            return;
        }
#if defined(_WIN32)
        // Reading the exception means rethrowing it, and that rethrow must
        // not replace where it was really thrown.
        ThrowRecordPinned = true;
        struct Unpin final
        {
            ~Unpin()
            {
                ThrowRecordPinned = false;
            }
        } unpin;
#endif
        try
        {
            std::rethrow_exception(exception);
        }
        catch (const std::exception& ex)
        {
            Exception(category, ex);
        }
        catch (...)
        {
            // C# accepts Exception?, so a non-std C++ throw has no managed
            // Exception object to report here.
            return;
        }
    }

    std::unique_ptr<DebugLog::Timed> DebugLog::Step(
        std::string category, std::string what)
    {
        if (!GetState().Writer.load())
        {
            return nullptr;
        }
        return std::unique_ptr<Timed>(new Timed(std::move(category), std::move(what)));
    }

    DebugLog::Timed::Timed(std::string category, std::string what)
        : _category(), _what(), _started(std::chrono::steady_clock::now())
    {
        _category = std::move(category);
        _what = std::move(what);
        DebugLog::Line(_category, _what + ": started");
    }

    DebugLog::Timed::~Timed() noexcept(false)
    {
        Dispose();
    }

    void DebugLog::Timed::Dispose()
    {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - _started).count();
        DebugLog::Line(_category, _what + ": done in " + std::to_string(elapsed) + " ms");
    }
}
