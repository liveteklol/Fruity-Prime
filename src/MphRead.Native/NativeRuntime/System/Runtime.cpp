#include "Runtime.hpp"

#include "Encoding.hpp"
#include "Globalization.hpp"
#include "Exceptions.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <intrin.h>
#else
#if defined(__APPLE__)
#include <TargetConditionals.h>
#include <climits>
#include <mach-o/dyld.h>
#endif
#include <csignal>
#include <time.h>
#include <fstream>
#include <stdlib.h>
#if defined(__linux__)
#include <sys/auxv.h>
#elif defined(__FreeBSD__)
#include <climits>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#endif
#include <string>
#include <sys/utsname.h>
#include <unistd.h>
#endif

namespace MphRead::NativeRuntime
{
    namespace
    {
        std::atomic<std::int32_t> ExitCode{0};
    }

    namespace
    {
#if !defined(_WIN32)
        [[nodiscard]] std::optional<std::string> RealPath(const char* path)
        {
            std::unique_ptr<char, decltype(&std::free)> resolved(::realpath(path, nullptr), &std::free);
            if (!resolved)
            {
                return std::nullopt;
            }
            return Utf8GetString(std::string_view(resolved.get()));
        }
#endif

        [[nodiscard]] std::optional<std::string> ReadProcessPath()
        {
#if defined(_WIN32)
            std::vector<wchar_t> buffer(260);
            for (;;)
            {
                const DWORD length = ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
                if (length == 0)
                {
                    return std::nullopt;
                }
                if (length < buffer.size())
                {
                    return WideToWtf8(std::wstring_view(buffer.data(), length));
                }
                if (buffer.size() > static_cast<std::size_t>(std::numeric_limits<DWORD>::max()) / 2U)
                {
                    return std::nullopt;
                }
                buffer.resize(buffer.size() * 2U);
            }
#elif defined(__APPLE__)
            std::uint32_t size = 0;
            char probe = 0;
            if (::_NSGetExecutablePath(&probe, &size) == 0 || size == 0)
            {
                return std::nullopt;
            }
            std::vector<char> buffer(size);
            if (::_NSGetExecutablePath(buffer.data(), &size) != 0)
            {
                return std::nullopt;
            }
            return RealPath(buffer.data());
#elif defined(__FreeBSD__)
            static const int name[] = {CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1};
            char path[PATH_MAX];
            std::size_t length = sizeof(path);
            if (::sysctl(name, 4, path, &length, nullptr, 0) != 0 || length == 0)
            {
                return std::nullopt;
            }
            return Utf8GetString(std::string_view(path, path[length - 1] == '\0' ? length - 1 : length));
#elif defined(__sun)
            const char* path = ::getexecname();
            return path == nullptr ? std::nullopt : RealPath(path);
#elif defined(__linux__)
            if (std::optional<std::string> path = RealPath("/proc/self/exe"))
            {
                // The dedicated-server package starts the game through its own
                // dynamic loader (lib/ld-linux-*.so.*), because a server box's
                // glibc can be older than the one the build needs. The running
                // image is then the loader, which would put the base directory
                // in lib/. The package's launch script names itself in
                // FRUITY_LAUNCHER; it is believed only when the loader really
                // is what is running.
                const std::string_view name = std::string_view(*path).substr(
                    path->find_last_of('/') + 1);
                if (name.starts_with("ld-linux") && name.find(".so") != std::string_view::npos)
                {
                    const char* launcher = std::getenv("FRUITY_LAUNCHER");
                    if (launcher != nullptr && *launcher != '\0')
                    {
                        if (std::optional<std::string> script = RealPath(launcher))
                        {
                            return script;
                        }
                    }
                }
                return path;
            }
#if defined(AT_EXECFN)
            if (const auto executable = reinterpret_cast<const char*>(::getauxval(AT_EXECFN)))
            {
                return RealPath(executable);
            }
#endif
            return std::nullopt;
#elif defined(__unix__)
            return RealPath("/proc/curproc/exe");
#else
            return std::nullopt;
#endif
        }
    }

    std::wstring PasteArgument(std::wstring_view value)
    {
        bool simple = !value.empty();
        for (const wchar_t ch : value)
        {
            if (ch == L'"' || CharIsWhiteSpace(static_cast<char32_t>(ch)))
            {
                simple = false;
                break;
            }
        }
        if (simple)
        {
            return std::wstring(value);
        }
        std::wstring result;
        result.push_back(L'"');
        std::size_t slashes = 0;
        for (const wchar_t ch : value)
        {
            if (ch == L'\\')
            {
                ++slashes;
                continue;
            }
            if (ch == L'"')
            {
                result.append(slashes * 2U + 1U, L'\\');
                result.push_back(L'"');
                slashes = 0;
                continue;
            }
            result.append(slashes, L'\\');
            slashes = 0;
            result.push_back(ch);
        }
        result.append(slashes * 2U, L'\\');
        result.push_back(L'"');
        return result;
    }

    std::optional<std::string> EnvironmentProcessPath()
    {
        // Environment.ProcessPath is read once and cached.
        static const std::optional<std::string> path = ReadProcessPath();
        return path;
    }

    std::string AppContextBaseDirectory()
    {
        static const std::string directory = []
        {
            const std::optional<std::string> path = EnvironmentProcessPath();
#if defined(_WIN32)
            const std::size_t separator = path.has_value() ? path->find_last_of("/\\") : std::string::npos;
#else
            const std::size_t separator = path.has_value() ? path->find_last_of('/') : std::string::npos;
#endif
            if (separator != std::string::npos)
            {
                return path->substr(0, separator + 1);
            }
            std::string current = EnvironmentCurrentDirectory();
#if defined(_WIN32)
            if (current.empty() || (current.back() != '\\' && current.back() != '/'))
            {
                current.push_back('\\');
            }
#else
            if (current.empty() || current.back() != '/')
            {
                current.push_back('/');
            }
#endif
            return current;
        }();
        return directory;
    }

    void ForceFullGc()
    {
        // There is no garbage-collected heap: every object is freed when its last
        // owner releases it, so a forced collection has nothing left to collect.
    }

    void SetSustainedLowLatencyGc()
    {
        // No collector exists whose pauses the latency mode would govern.
    }

    bool DebuggerAttached()
    {
#if defined(_WIN32)
        return ::IsDebuggerPresent() != FALSE;
#else
        std::ifstream status("/proc/self/status");
        std::string line;
        while (std::getline(status, line))
        {
            constexpr const char* key = "TracerPid:";
            if (line.rfind(key, 0) == 0)
            {
                return std::strtol(line.c_str() + 10, nullptr, 10) != 0;
            }
        }
        return false;
#endif
    }

    void DebuggerBreak()
    {
        // Debugger.Break() on .NET (Core) signals a user breakpoint to an
        // attached debugger and is a no-op otherwise -- there is no JIT-attach
        // prompt as on .NET Framework, and nothing that ends the process.
        if (!DebuggerAttached())
        {
            return;
        }
#if defined(_WIN32)
        __debugbreak();
#else
        std::raise(SIGTRAP);
#endif
    }

    void DebugAssert(bool condition)
    {
        if (condition)
        {
            return;
        }
        // DebugProvider.FailCore: break into an attached debugger, otherwise
        // Environment.FailFast.
        if (DebuggerAttached())
        {
            DebuggerBreak();
            return;
        }
        std::fputs("Process terminated. Assertion failed.\n", stderr);
        std::fflush(stderr);
        std::abort();
    }

    bool IsAndroid()
    {
#if defined(__ANDROID__)
        return true;
#else
        return false;
#endif
    }

    bool IsMacOS()
    {
#if defined(__APPLE__)
        return TARGET_OS_OSX != 0;
#else
        return false;
#endif
    }

    std::string EnvironmentCurrentDirectory()
    {
        std::error_code error;
        const std::filesystem::path directory = std::filesystem::current_path(error);
        if (error)
        {
            return std::string();
        }
        const std::u8string text = directory.u8string();
        return std::string(text.begin(), text.end());
    }

    std::string EnvironmentOSVersion()
    {
#if defined(_WIN32)
        using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
        const HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
        if (ntdll != nullptr)
        {
            const auto rtlGetVersion = reinterpret_cast<RtlGetVersionFn>(
                reinterpret_cast<void*>(::GetProcAddress(ntdll, "RtlGetVersion")));
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
            return std::string("Unix ") + info.release;
        }
        return "Unix";
#endif
    }

    std::string EnvironmentVersion()
    {
        return "native";
    }

    bool EnvironmentIs64BitProcess() noexcept
    {
        return sizeof(void*) == 8;
    }

    std::int32_t EnvironmentProcessId() noexcept
    {
#if defined(_WIN32)
        return static_cast<std::int32_t>(::GetCurrentProcessId());
#else
        return static_cast<std::int32_t>(::getpid());
#endif
    }

    std::string EnvironmentCommandLine()
    {
#if defined(_WIN32)
        const wchar_t* const line = ::GetCommandLineW();
        if (line == nullptr)
        {
            return std::string();
        }
        const int length = ::WideCharToMultiByte(
            CP_UTF8, 0, line, -1, nullptr, 0, nullptr, nullptr);
        if (length <= 1)
        {
            return std::string();
        }
        std::string result(static_cast<std::size_t>(length - 1), '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, line, -1, result.data(), length, nullptr, nullptr);
        return result;
#else
        // /proc/self/cmdline holds the arguments separated by NULs; .NET
        // rebuilds the line from them, quoting any that contain a space.
        std::ifstream file("/proc/self/cmdline", std::ios::binary);
        std::string raw((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        std::string result;
        std::size_t start = 0;
        while (start < raw.size())
        {
            const std::size_t end = raw.find('\0', start);
            const std::string argument = raw.substr(
                start, end == std::string::npos ? std::string::npos : end - start);
            if (!result.empty())
            {
                result.push_back(' ');
            }
            if (argument.find(' ') != std::string::npos)
            {
                result.push_back('"');
                result.append(argument);
                result.push_back('"');
            }
            else
            {
                result.append(argument);
            }
            if (end == std::string::npos)
            {
                break;
            }
            start = end + 1;
        }
        return result;
#endif
    }

    std::string RuntimeInformationProcessArchitecture()
    {
#if defined(__x86_64__) || defined(_M_X64)
        return "X64";
#elif defined(__i386__) || defined(_M_IX86)
        return "X86";
#elif defined(__aarch64__) || defined(_M_ARM64)
        return "Arm64";
#elif defined(__arm__) || defined(_M_ARM)
#if defined(__ARM_ARCH_6__)
        return "Armv6";
#else
        return "Arm";
#endif
#elif defined(__wasm__)
        return "Wasm";
#elif defined(__s390x__)
        return "S390x";
#elif defined(__loongarch64)
        return "LoongArch64";
#elif defined(__powerpc64__) && defined(__LITTLE_ENDIAN__)
        return "Ppc64le";
#else
        return "Unknown";
#endif
    }

    std::string RuntimeInformationOSArchitecture()
    {
#if defined(_WIN32)
        SYSTEM_INFO info{};
        ::GetNativeSystemInfo(&info);
        switch (info.wProcessorArchitecture)
        {
        case PROCESSOR_ARCHITECTURE_AMD64:
            return "X64";
        case PROCESSOR_ARCHITECTURE_INTEL:
            return "X86";
        case PROCESSOR_ARCHITECTURE_ARM:
            return "Arm";
        case PROCESSOR_ARCHITECTURE_ARM64:
            return "Arm64";
        default:
            return RuntimeInformationProcessArchitecture();
        }
#else
        struct utsname info{};
        if (::uname(&info) != 0)
        {
            return RuntimeInformationProcessArchitecture();
        }
        const std::string machine(info.machine);
        if (machine == "x86_64" || machine == "amd64")
        {
            return "X64";
        }
        if (machine == "i386" || machine == "i486"
            || machine == "i586" || machine == "i686")
        {
            return "X86";
        }
        if (machine == "aarch64" || machine == "arm64")
        {
            return "Arm64";
        }
        if (machine.rfind("armv6", 0) == 0)
        {
            return "Armv6";
        }
        if (machine.rfind("arm", 0) == 0)
        {
            return "Arm";
        }
        if (machine == "s390x")
        {
            return "S390x";
        }
        if (machine == "loongarch64")
        {
            return "LoongArch64";
        }
        if (machine == "ppc64le")
        {
            return "Ppc64le";
        }
        return RuntimeInformationProcessArchitecture();
#endif
    }

    std::string RuntimeInformationOSDescription()
    {
#if defined(_WIN32)
        // "Microsoft Windows <major>.<minor>.<build>", as .NET spells it.
        using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
        RTL_OSVERSIONINFOW version{};
        version.dwOSVersionInfoSize = sizeof(version);
        const HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
        if (ntdll != nullptr)
        {
            const auto getVersion = reinterpret_cast<RtlGetVersionFn>(
                reinterpret_cast<void*>(::GetProcAddress(ntdll, "RtlGetVersion")));
            if (getVersion != nullptr && getVersion(&version) == 0)
            {
                return "Microsoft Windows " + std::to_string(version.dwMajorVersion) + "."
                    + std::to_string(version.dwMinorVersion) + "."
                    + std::to_string(version.dwBuildNumber);
            }
        }
        return "Microsoft Windows";
#else
        // "<sysname> <release> <version>", which is what .NET builds from uname.
        struct utsname info{};
        if (::uname(&info) != 0)
        {
            return std::string();
        }
        return std::string(info.sysname) + " " + info.release + " " + info.version;
#endif
    }

    std::string RuntimeInformationRuntimeIdentifier()
    {
        std::string architecture = RuntimeInformationProcessArchitecture();
        for (char& value : architecture)
        {
            value = static_cast<char>(
                value >= 'A' && value <= 'Z' ? value - 'A' + 'a' : value);
        }
#if defined(__ANDROID__)
        const std::string platform = "android";
#elif defined(_WIN32)
        const std::string platform = "win";
#elif defined(__APPLE__)
        const std::string platform = "osx";
#else
        const std::string platform = "linux";
#endif
        return platform + "-" + architecture;
    }

    std::string RuntimeInformationFrameworkDescription()
    {
        return ".NET native";
    }

    std::string EnvironmentUserProfile()
    {
#if defined(_WIN32)
        // Environment.GetFolderPath reads the known folder; USERPROFILE is
        // what .NET falls back to and what every shell here sets.
        std::vector<wchar_t> buffer(260);
        for (;;)
        {
            const DWORD length = ::GetEnvironmentVariableW(
                L"USERPROFILE", buffer.data(), static_cast<DWORD>(buffer.size()));
            if (length == 0)
            {
                return std::string();
            }
            if (length < buffer.size())
            {
                const std::wstring wide(buffer.data(), length);
                const std::u8string text = std::filesystem::path(wide).u8string();
                return std::string(text.begin(), text.end());
            }
            buffer.resize(length);
        }
#else
        const char* const home = std::getenv("HOME");
        return home == nullptr ? std::string() : std::string(home);
#endif
    }

    std::int32_t EnvironmentExitCode() noexcept
    {
        return ExitCode.load();
    }

    void SetEnvironmentExitCode(std::int32_t value) noexcept
    {
        ExitCode.store(value);
    }

    void ThrowDivideByZeroException()
    {
        throw System::DivideByZeroException();
    }

    void ThrowListIndexOutOfRange()
    {
        throw System::ArgumentOutOfRangeException();
    }

    std::int64_t EnvironmentTickCount64() noexcept
    {
#if defined(_WIN32)
        return static_cast<std::int64_t>(::GetTickCount64());
#else
        timespec now{};
        ::clock_gettime(CLOCK_MONOTONIC, &now);
        return static_cast<std::int64_t>(now.tv_sec) * 1000 + now.tv_nsec / 1000000;
#endif
    }
}
