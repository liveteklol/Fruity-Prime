#include "DesktopUpdate.hpp"
#include "../../NativeRuntime/System/Exceptions.hpp"
#include "NativeRuntime/System/AtomicSharedPtr.hpp"

#include "BuildVersion.hpp"
#include "UpdateDownload.hpp"
#include "../../NativeRuntime/System/Console.hpp"
#include "../../NativeRuntime/System/Encoding.hpp"
#include "../../NativeRuntime/System/ExceptionText.hpp"
#include "../../NativeRuntime/System/Globalization.hpp"
#include "../../NativeRuntime/System/ArchiveFile.hpp"
#include "../../NativeRuntime/System/IO.hpp"
#include "../../NativeRuntime/System/Runtime.hpp"
#include "NativeRuntime/System/Globalization.hpp"

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <copyfile.h>
#include <fcntl.h>
#include <mach-o/dyld.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using ::MphRead::NativeRuntime::AppContextBaseDirectory;
using ::MphRead::NativeRuntime::CharIsWhiteSpace;
using ::MphRead::NativeRuntime::DirectoryCreateDirectory;
using ::MphRead::NativeRuntime::DirectoryExists;
using ::MphRead::NativeRuntime::EnvironmentCurrentDirectory;
using ::MphRead::NativeRuntime::EnvironmentGetVariable;
using ::MphRead::NativeRuntime::EnvironmentProcessPath;
using ::MphRead::NativeRuntime::FileDelete;
using ::MphRead::NativeRuntime::FileExists;
using ::MphRead::NativeRuntime::IsAndroid;
using ::MphRead::NativeRuntime::IsWindows;
using ::MphRead::NativeRuntime::PasteArgument;
using ::MphRead::NativeRuntime::PathCombine;
using ::MphRead::NativeRuntime::PathFromUtf8;
using ::MphRead::NativeRuntime::PathGetDirectoryName;
using ::MphRead::NativeRuntime::PathToUtf8;
using ::MphRead::NativeRuntime::Utf8ToWide;
using ::MphRead::NativeRuntime::WideToUtf8;

namespace MphRead::Mods::Update
{
    namespace
    {
        namespace fs = std::filesystem;
        using namespace std::chrono_literals;

        ::MphRead::NativeRuntime::AtomicSharedPtr<const std::string> LastErrorValue{nullptr};

        using IOException = ::System::IO::IOException;

        using UnauthorizedAccessException = ::System::UnauthorizedAccessException;


        [[noreturn]] void ThrowFileError(
            const std::string& path, const std::error_code& error)
        {
            if (error == std::errc::permission_denied)
            {
                throw UnauthorizedAccessException(
                    "Access to the path '" + path + "' is denied.");
            }
            throw IOException(error.message());
        }

        void WriteEmptyFile(const std::string& path)
        {
#if defined(_WIN32)
            const std::wstring native = Utf8ToWide(path);
            HANDLE handle = ::CreateFileW(native.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (handle == INVALID_HANDLE_VALUE)
            {
                ThrowFileError(path, std::error_code(
                    static_cast<int>(::GetLastError()), std::system_category()));
            }
            if (!::CloseHandle(handle))
            {
                ThrowFileError(path, std::error_code(
                    static_cast<int>(::GetLastError()), std::system_category()));
            }
#else
            int flags = O_WRONLY | O_CREAT | O_TRUNC;
#ifdef O_CLOEXEC
            flags |= O_CLOEXEC;
#endif
            const int fd = ::open(PathFromUtf8(path).c_str(), flags, 0666);
            if (fd < 0)
            {
                ThrowFileError(path, std::error_code(errno, std::generic_category()));
            }
            if (::close(fd) != 0)
            {
                ThrowFileError(path, std::error_code(errno, std::generic_category()));
            }
#endif
        }

        // The archive half moved to NativeRuntime/System/ArchiveFile.

#if defined(_WIN32)
        [[nodiscard]] std::wstring_view TrimDotNetWhiteSpace(
            std::wstring_view value) noexcept
        {
            while (!value.empty() && CharIsWhiteSpace(value.front()))
            {
                value.remove_prefix(1);
            }
            while (!value.empty() && CharIsWhiteSpace(value.back()))
            {
                value.remove_suffix(1);
            }
            return value;
        }

#else
        [[nodiscard]] bool IsDotNetNullOrWhiteSpaceUtf8(
            std::string_view value) noexcept
        {
            std::size_t i = 0;
            while (i < value.size())
            {
                const unsigned char ch = static_cast<unsigned char>(value[i]);
                if ((ch >= 0x09U && ch <= 0x0DU) || ch == 0x20U)
                {
                    ++i;
                    continue;
                }
                if (i + 1U < value.size() && ch == 0xC2U)
                {
                    const unsigned char next = static_cast<unsigned char>(value[i + 1U]);
                    if (next == 0x85U || next == 0xA0U)
                    {
                        i += 2U;
                        continue;
                    }
                }
                if (i + 2U < value.size())
                {
                    const unsigned char second = static_cast<unsigned char>(value[i + 1U]);
                    const unsigned char third = static_cast<unsigned char>(value[i + 2U]);
                    if ((ch == 0xE1U && second == 0x9AU && third == 0x80U)
                        || (ch == 0xE2U && second == 0x80U
                            && ((third >= 0x80U && third <= 0x8AU)
                                || third == 0xA8U || third == 0xA9U
                                || third == 0xAFU))
                        || (ch == 0xE2U && second == 0x81U && third == 0x9FU)
                        || (ch == 0xE3U && second == 0x80U && third == 0x80U))
                    {
                        i += 3U;
                        continue;
                    }
                }
                return false;
            }
            return true;
        }

        [[nodiscard]] bool IsUnixExecutable(const std::string& path)
        {
            struct stat info{};
            const fs::path native = PathFromUtf8(path);
            if (::stat(native.c_str(), &info) != 0 || S_ISDIR(info.st_mode))
            {
                return false;
            }
            return ::access(native.c_str(), X_OK) == 0;
        }

        [[nodiscard]] std::optional<std::string> ResolveUnixProcessPath(
            const std::string& executable)
        {
            if (!executable.empty() && executable.front() == '/')
            {
                return executable;
            }

            // Process.ResolvePath: beside this executable, then the current
            // directory, then PATH.
            if (const std::optional<std::string> processPath = EnvironmentProcessPath())
            {
                const std::string beside = PathCombine(
                    PathGetDirectoryName(*processPath).value_or(std::string()), executable);
                if (FileExists(beside))
                {
                    return beside;
                }
            }

            std::string candidate = PathCombine(EnvironmentCurrentDirectory(), executable);
            if (FileExists(candidate))
            {
                return candidate;
            }

            if (const std::optional<std::string> pathValue = EnvironmentGetVariable("PATH"))
            {
                const std::string_view paths(*pathValue);
                std::size_t start = 0;
                while (start <= paths.size())
                {
                    const std::size_t end = paths.find(':', start);
                    const std::string_view directory = paths.substr(start,
                        end == std::string_view::npos
                            ? paths.size() - start : end - start);
                    if (!directory.empty())
                    {
                        candidate = PathCombine(std::string(directory), executable);
                        if (IsUnixExecutable(candidate))
                        {
                            return candidate;
                        }
                    }
                    if (end == std::string_view::npos)
                    {
                        break;
                    }
                    start = end + 1U;
                }
            }
            return std::nullopt;
        }
#endif

        void StartProcess(const std::string& executable,
            const std::string& workingDirectory,
            std::span<const std::string> arguments)
        {
#if defined(_WIN32)
            const std::wstring rawExe = Utf8ToWide(executable);
            const std::wstring_view exe = TrimDotNetWhiteSpace(rawExe);
            const std::wstring cwd = Utf8ToWide(workingDirectory);
            std::wstring command;
            const bool fileNameIsQuoted = exe.size() >= 2U
                && exe.front() == L'\"' && exe.back() == L'\"';
            if (!fileNameIsQuoted)
            {
                command.push_back(L'\"');
            }
            command.append(exe);
            if (!fileNameIsQuoted)
            {
                command.push_back(L'\"');
            }
            for (const std::string& argument : arguments)
            {
                command.push_back(L' ');
                command += PasteArgument(Utf8ToWide(argument));
            }
            std::vector<wchar_t> mutableCommand(command.begin(), command.end());
            mutableCommand.push_back(L'\0');

            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            PROCESS_INFORMATION process{};
            static std::mutex processStartMutex;
            std::lock_guard lock(processStartMutex);
            if (!::CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr,
                TRUE, 0, nullptr, cwd.empty() ? nullptr : cwd.c_str(),
                &startup, &process))
            {
                throw std::system_error(
                    static_cast<int>(::GetLastError()), std::system_category());
            }
            ::CloseHandle(process.hThread);
            ::CloseHandle(process.hProcess);
#else
            const bool hasWorkingDirectory
                = !IsDotNetNullOrWhiteSpaceUtf8(workingDirectory);
            const std::optional<std::string> resolvedExecutable
                = ResolveUnixProcessPath(executable);

            std::vector<std::string> owned;
            owned.reserve(arguments.size() + 1U);
            owned.push_back(executable);
            for (const std::string& argument : arguments)
            {
                owned.push_back(argument);
            }
            std::vector<char*> argv;
            argv.reserve(owned.size() + 1U);
            for (std::string& value : owned)
            {
                argv.push_back(value.data());
            }
            argv.push_back(nullptr);

            if (!resolvedExecutable.has_value())
            {
                throw std::system_error(ENOENT, std::generic_category());
            }
            const fs::path nativeExecutable = PathFromUtf8(*resolvedExecutable);
            std::optional<fs::path> nativeWorkingDirectory;
            if (hasWorkingDirectory)
            {
                nativeWorkingDirectory = PathFromUtf8(workingDirectory);
            }

            int errorPipe[2] = {-1, -1};
#if defined(O_CLOEXEC) && defined(__linux__)
            if (::pipe2(errorPipe, O_CLOEXEC) != 0)
            {
                throw std::system_error(errno, std::generic_category());
            }
#else
            if (::pipe(errorPipe) != 0)
            {
                throw std::system_error(errno, std::generic_category());
            }
            for (int fd : errorPipe)
            {
                const int oldFlags = ::fcntl(fd, F_GETFD);
                if (oldFlags < 0 || ::fcntl(fd, F_SETFD, oldFlags | FD_CLOEXEC) != 0)
                {
                    const int error = errno;
                    ::close(errorPipe[0]);
                    ::close(errorPipe[1]);
                    throw std::system_error(error, std::generic_category());
                }
            }
#endif

            const pid_t child = ::fork();
            if (child < 0)
            {
                const int error = errno;
                ::close(errorPipe[0]);
                ::close(errorPipe[1]);
                throw std::system_error(error, std::generic_category());
            }
            if (child == 0)
            {
                ::close(errorPipe[0]);
                auto fail = [&](int error) noexcept
                {
                    const char* bytes = reinterpret_cast<const char*>(&error);
                    std::size_t offset = 0;
                    while (offset < sizeof(error))
                    {
                        const ssize_t written = ::write(errorPipe[1],
                            bytes + offset, sizeof(error) - offset);
                        if (written > 0)
                        {
                            offset += static_cast<std::size_t>(written);
                        }
                        else if (written < 0 && errno == EINTR)
                        {
                            continue;
                        }
                        else
                        {
                            break;
                        }
                    }
                    ::_exit(127);
                };

                if (nativeWorkingDirectory.has_value()
                    && ::chdir(nativeWorkingDirectory->c_str()) != 0)
                {
                    fail(errno);
                }
                ::execv(nativeExecutable.c_str(), argv.data());
                fail(errno);
            }

            ::close(errorPipe[1]);
            int childError = 0;
            std::size_t received = 0;
            char* bytes = reinterpret_cast<char*>(&childError);
            for (;;)
            {
                const ssize_t count = ::read(errorPipe[0],
                    bytes + received, sizeof(childError) - received);
                if (count > 0)
                {
                    received += static_cast<std::size_t>(count);
                    if (received == sizeof(childError))
                    {
                        break;
                    }
                }
                else if (count == 0)
                {
                    break;
                }
                else if (errno == EINTR)
                {
                    continue;
                }
                else
                {
                    const int error = errno;
                    ::close(errorPipe[0]);
                    throw std::system_error(error, std::generic_category());
                }
            }
            ::close(errorPipe[0]);
            if (received != 0)
            {
                int status = 0;
                while (::waitpid(child, &status, 0) < 0 && errno == EINTR)
                {
                }
                throw std::system_error(childError, std::generic_category());
            }
            try
            {
                std::thread([child]
                {
                    int status = 0;
                    while (::waitpid(child, &status, 0) < 0 && errno == EINTR)
                    {
                    }
                }).detach();
            }
            catch (...)
            {
                // Process.Start succeeded; inability to create a housekeeping
                // waiter must not turn that success into a failed launch.
            }
#endif
        }

        [[nodiscard]] std::int32_t CurrentProcessId() noexcept
        {
#if defined(_WIN32)
            return static_cast<std::int32_t>(::GetCurrentProcessId());
#else
            return static_cast<std::int32_t>(::getpid());
#endif
        }

        void Sleep(std::chrono::milliseconds duration)
        {
            std::this_thread::sleep_for(duration);
        }

        void CopyFileOnce(const std::string& from, const std::string& to)
        {
#if defined(_WIN32)
            const std::wstring source = Utf8ToWide(from);
            const std::wstring destination = Utf8ToWide(to);
            if (!::CopyFileW(source.c_str(), destination.c_str(), FALSE))
            {
                ThrowFileError(to, std::error_code(
                    static_cast<int>(::GetLastError()), std::system_category()));
            }
#else
            int sourceFlags = O_RDONLY;
#ifdef O_CLOEXEC
            sourceFlags |= O_CLOEXEC;
#endif
            int sourceFd = ::open(PathFromUtf8(from).c_str(), sourceFlags);
            if (sourceFd < 0)
            {
                ThrowFileError(from, std::error_code(errno, std::generic_category()));
            }

            struct stat initial{};
            if (::fstat(sourceFd, &initial) != 0)
            {
                const int error = errno;
                (void)::close(sourceFd);
                ThrowFileError(from, std::error_code(error, std::generic_category()));
            }

            int destinationFlags = O_RDWR | O_CREAT | O_TRUNC;
#ifdef O_CLOEXEC
            destinationFlags |= O_CLOEXEC;
#endif
            int destinationFd = ::open(PathFromUtf8(to).c_str(), destinationFlags,
                static_cast<mode_t>(initial.st_mode & 0777));
            if (destinationFd < 0)
            {
                const int error = errno;
                (void)::close(sourceFd);
                ThrowFileError(to, std::error_code(error, std::generic_category()));
            }

            auto closeBoth = [&]() noexcept
            {
                if (destinationFd >= 0)
                {
                    (void)::close(destinationFd);
                    destinationFd = -1;
                }
                if (sourceFd >= 0)
                {
                    (void)::close(sourceFd);
                    sourceFd = -1;
                }
            };

            try
            {
#if defined(__APPLE__)
                if (::fcopyfile(sourceFd, destinationFd, nullptr, COPYFILE_ALL) != 0)
                {
                    ThrowFileError(to,
                        std::error_code(errno, std::generic_category()));
                }
#else
                std::vector<char> buffer(64U * 1024U);
                for (;;)
                {
                    ssize_t count = ::read(sourceFd, buffer.data(), buffer.size());
                    if (count == 0)
                    {
                        break;
                    }
                    if (count < 0)
                    {
                        if (errno == EINTR)
                        {
                            continue;
                        }
                        ThrowFileError(from,
                            std::error_code(errno, std::generic_category()));
                    }

                    std::size_t offset = 0;
                    const std::size_t total = static_cast<std::size_t>(count);
                    while (offset < total)
                    {
                        const ssize_t written = ::write(destinationFd,
                            buffer.data() + offset, total - offset);
                        if (written > 0)
                        {
                            offset += static_cast<std::size_t>(written);
                        }
                        else if (written < 0 && errno == EINTR)
                        {
                            continue;
                        }
                        else
                        {
                            ThrowFileError(to,
                                std::error_code(errno, std::generic_category()));
                        }
                    }
                }

                struct stat finalSource{};
                if (::fstat(sourceFd, &finalSource) != 0)
                {
                    ThrowFileError(from,
                        std::error_code(errno, std::generic_category()));
                }

                struct timespec times[2]{};
#if defined(__APPLE__)
                times[0] = finalSource.st_atimespec;
                times[1] = finalSource.st_mtimespec;
#else
                times[0] = finalSource.st_atim;
                times[1] = finalSource.st_mtim;
#endif
                if (::futimens(destinationFd, times) != 0 && errno != EPERM)
                {
                    ThrowFileError(to,
                        std::error_code(errno, std::generic_category()));
                }
                if (::fchmod(destinationFd,
                    static_cast<mode_t>(finalSource.st_mode & 0777)) != 0
                    && errno != EPERM)
                {
                    ThrowFileError(to,
                        std::error_code(errno, std::generic_category()));
                }
#endif
                closeBoth();
            }
            catch (...)
            {
                closeBoth();
                throw;
            }
#endif
        }

        void AssignLastError(std::optional<std::string> value)
        {
            LastErrorValue.store(value.has_value()
                ? std::make_shared<const std::string>(std::move(*value))
                : std::shared_ptr<const std::string>(),
                std::memory_order_relaxed);
        }

        [[nodiscard]] std::string MessageForUnknownException()
        {
            return "Exception";
        }
    }

    std::string DesktopUpdate::Staging()
    {
        return PathCombine(AppContextBaseDirectory(), ".update");
    }

    std::string DesktopUpdate::StagedBuild()
    {
        return PathCombine(Staging(), "staged");
    }

    std::string DesktopUpdate::StagedBuildPath()
    {
        return StagedBuild();
    }

    std::optional<std::string> DesktopUpdate::BinaryDirectory(const std::string& root)
    {
        namespace fs = std::filesystem;
        const std::string name = UpdateCheck::BinaryName();
        const fs::path top = PathFromUtf8(root);
        std::error_code error;
        if (fs::is_regular_file(top / PathFromUtf8(name), error))
        {
            return std::string();
        }
        // The installs put it one directory down (bin/, or the macOS bundle's
        // Contents/MacOS), so look a few levels in, shallowest first.
        std::optional<fs::path> best;
        std::size_t bestDepth = 0;
        fs::recursive_directory_iterator iterator(top, fs::directory_options::skip_permission_denied, error);
        for (; !error && iterator != fs::recursive_directory_iterator(); iterator.increment(error))
        {
            if (iterator.depth() > 4)
            {
                iterator.disable_recursion_pending();
                continue;
            }
            std::error_code typeError;
            if (iterator->path().filename() == PathFromUtf8(name) && iterator->is_regular_file(typeError))
            {
                const std::size_t depth = static_cast<std::size_t>(iterator.depth());
                if (!best.has_value() || depth < bestDepth)
                {
                    best = iterator->path().parent_path().lexically_relative(top);
                    bestDepth = depth;
                }
            }
        }
        if (!best.has_value())
        {
            return std::nullopt;
        }
        return PathToUtf8(*best);
    }

    std::optional<std::string> DesktopUpdate::LastError()
    {
        const std::shared_ptr<const std::string> value
            = LastErrorValue.load(std::memory_order_relaxed);
        return value == nullptr
            ? std::nullopt
            : std::optional<std::string>(*value);
    }

    void DesktopUpdate::SetLastError(std::optional<std::string> value)
    {
        AssignLastError(std::move(value));
    }

    bool DesktopUpdate::Supported()
    {
        // Copying files into a signed app invalidates its resource seal.
        // macOS updates use the release page and replace the whole app.
        if (::MphRead::NativeRuntime::IsMacOS() || IsAndroid() || !BuildVersion::IsRelease())
        {
            return false;
        }
        try
        {
            const std::string probe = PathCombine(AppContextBaseDirectory(), ".update-probe");
            WriteEmptyFile(probe);
            FileDelete(probe);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool DesktopUpdate::Stage(
        UpdateInfo update,
        const std::function<void(float)>& progress,
        CancellationToken cancel)
    {
        SetLastError(std::nullopt);

        const std::optional<std::string>& assetUrl = update.AssetUrl.Get();
        if (!assetUrl.has_value())
        {
            throw NullReferenceException();
        }
        if (assetUrl->empty())
        {
            SetLastError("this release has no package for this platform");
            return false;
        }

        try
        {
            Clean();
            const std::string staging = Staging();
            DirectoryCreateDirectory(staging);

            const std::optional<std::string>& assetName = update.AssetName.Get();
            if (!assetName.has_value())
            {
                throw NullReferenceException();
            }
            const bool zip = ::MphRead::NativeRuntime::StringEndsWithOrdinalIgnoreCase(*assetName, ".zip");
            const std::string archive = PathCombine(staging,
                zip ? "package.zip" : "package.tar.gz");

            if (!UpdateDownload::Fetch(*assetUrl, archive,
                update.AssetSize.Get(), progress, cancel))
            {
                std::optional<std::string> error = UpdateDownload::LastError();
                SetLastError(error.has_value()
                    ? std::move(error)
                    : std::optional<std::string>("the download failed"));
                return false;
            }

            const std::string stagedBuild = StagedBuild();
            DirectoryCreateDirectory(stagedBuild);
            ::MphRead::NativeRuntime::ArchiveExtractToDirectory(archive, stagedBuild, zip);
            FileDelete(archive);

            const std::optional<std::string> directory = BinaryDirectory(stagedBuild);
            if (!directory.has_value())
            {
                SetLastError("the package does not contain " + UpdateCheck::BinaryName());
                return false;
            }
            MakeExecutable(PathCombine(PathCombine(stagedBuild, *directory), UpdateCheck::BinaryName()));
            return true;
        }
        catch (const std::exception& ex)
        {
            SetLastError(ex.what());
            std::cout << "[update] could not stage the update: "
                << ::MphRead::NativeRuntime::ExceptionToString(ex) << '\n';
            return false;
        }
        catch (...)
        {
            const std::string message = MessageForUnknownException();
            SetLastError(message);
            std::cout << "[update] could not stage the update: " << message << '\n';
            return false;
        }
    }

    bool DesktopUpdate::Launch(
        std::optional<std::span<const std::string>> relaunchArgs)
    {
        try
        {
            const std::string stagedBuild = StagedBuild();
            const std::optional<std::string> directory = BinaryDirectory(stagedBuild);
            if (!directory.has_value())
            {
                throw std::runtime_error("the package does not contain " + UpdateCheck::BinaryName());
            }
            const std::string binaryDirectory = PathCombine(stagedBuild, *directory);
            const std::string binary = PathCombine(binaryDirectory, UpdateCheck::BinaryName());
            // A package that keeps its binary in a subdirectory replaces the
            // install as a whole: the target is the running binary's directory
            // less that subdirectory, so the libraries and plugins beside it
            // are updated too. A flat install keeps the old behaviour.
            std::string target = AppContextBaseDirectory();
            if (!directory->empty())
            {
                namespace fs = std::filesystem;
                const fs::path running = PathFromUtf8(target).lexically_normal();
                const fs::path relative = PathFromUtf8(*directory).lexically_normal();
                fs::path root = running;
                bool matches = true;
                for (auto part = relative.end(); part != relative.begin();)
                {
                    --part;
                    if (part->empty())
                    {
                        continue;
                    }
                    if (root.filename().empty())
                    {
                        root = root.parent_path();
                    }
                    if (root.filename() != *part)
                    {
                        matches = false;
                        break;
                    }
                    root = root.parent_path();
                }
                if (matches && !root.empty())
                {
                    target = PathToUtf8(root);
                }
            }
            std::vector<std::string> arguments;
            arguments.emplace_back("-" + std::string(ApplyFlag));
            arguments.push_back(target);
            arguments.push_back(std::to_string(CurrentProcessId()));
            if (relaunchArgs.has_value())
            {
                arguments.emplace_back(RelaunchSeparator);
                arguments.insert(arguments.end(), relaunchArgs->begin(), relaunchArgs->end());
            }
            StartProcess(binary, binaryDirectory, arguments);
            return true;
        }
        catch (const std::exception& ex)
        {
            SetLastError(ex.what());
            std::cout << "[update] could not start the update: "
                << ::MphRead::NativeRuntime::ExceptionToString(ex) << '\n';
            return false;
        }
        catch (...)
        {
            const std::string message = MessageForUnknownException();
            SetLastError(message);
            std::cout << "[update] could not start the update: " << message << '\n';
            return false;
        }
    }

    std::int32_t DesktopUpdate::Apply(
        const std::string& target,
        std::int32_t waitFor,
        std::optional<std::span<const std::string>> relaunchArgs)
    {
        std::cout << "[update] applying to " << target << '\n';
        WaitForExit(waitFor);
        // The staged package as a whole when its binary sits in a subdirectory
        // and the install has the same shape (see Launch); otherwise just the
        // directory this process runs from, as before.
        std::string source = AppContextBaseDirectory();
        std::string binaryDirectory = target;
        try
        {
            const std::string stagedBuild = StagedBuild();
            const std::optional<std::string> directory = BinaryDirectory(stagedBuild);
            if (directory.has_value() && !directory->empty()
                && FileExists(PathCombine(PathCombine(target, *directory), UpdateCheck::BinaryName())))
            {
                source = stagedBuild;
                binaryDirectory = PathCombine(target, *directory);
            }
        }
        catch (...)
        {
        }
        try
        {
            Copy(source, target);
        }
        catch (const std::exception& ex)
        {
            std::cout << "[update] the copy failed: " << ex.what() << '\n';
            std::cout << "[update] the new build is in " << source
                << " -- copy it over " << target << " by hand\n";
            return 1;
        }
        catch (...)
        {
            std::cout << "[update] the copy failed: "
                << MessageForUnknownException() << '\n';
            std::cout << "[update] the new build is in " << source
                << " -- copy it over " << target << " by hand\n";
            return 1;
        }

        try
        {
            const std::string binary = PathCombine(binaryDirectory, UpdateCheck::BinaryName());
            MakeExecutable(binary);
            const std::span<const std::string> arguments = relaunchArgs.has_value()
                ? *relaunchArgs : std::span<const std::string>{};
            StartProcess(binary, binaryDirectory, arguments);
        }
        catch (const std::exception& ex)
        {
            std::cout << "[update] updated, but could not restart: "
                << ex.what() << '\n';
            return 1;
        }
        catch (...)
        {
            std::cout << "[update] updated, but could not restart: "
                << MessageForUnknownException() << '\n';
            return 1;
        }
        std::cout << "[update] done\n";
        return 0;
    }

    void DesktopUpdate::WaitForExit(std::int32_t pid)
    {
#if defined(_WIN32)
        if (pid == 0)
        {
            throw std::system_error(
                static_cast<int>(ERROR_ACCESS_DENIED), std::system_category());
        }

        HANDLE probe = ::OpenProcess(
            PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
            FALSE, static_cast<DWORD>(pid));
        if (probe == nullptr)
        {
            const DWORD error = ::GetLastError();
            if (error == ERROR_INVALID_PARAMETER)
            {
                Sleep(400ms);
                return;
            }
            if (error != ERROR_ACCESS_DENIED)
            {
                throw std::system_error(
                    static_cast<int>(error), std::system_category());
            }
        }
        else
        {
            DWORD exitCode = STILL_ACTIVE;
            const BOOL gotExitCode = ::GetExitCodeProcess(probe, &exitCode);
            if (gotExitCode && exitCode != STILL_ACTIVE)
            {
                ::CloseHandle(probe);
                Sleep(400ms);
                return;
            }

            const DWORD signaled = ::WaitForSingleObject(probe, 0);
            if (signaled == WAIT_FAILED)
            {
                const DWORD error = ::GetLastError();
                ::CloseHandle(probe);
                throw std::system_error(
                    static_cast<int>(error), std::system_category());
            }
            if (signaled == WAIT_OBJECT_0)
            {
                if (!::GetExitCodeProcess(probe, &exitCode))
                {
                    const DWORD error = ::GetLastError();
                    ::CloseHandle(probe);
                    throw std::system_error(
                        static_cast<int>(error), std::system_category());
                }
                ::CloseHandle(probe);
                Sleep(400ms);
                return;
            }
            ::CloseHandle(probe);
        }

        HANDLE process = ::OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
        if (process == nullptr)
        {
            const DWORD error = ::GetLastError();
            if (error == ERROR_INVALID_PARAMETER)
            {
                Sleep(400ms);
                return;
            }
            throw std::system_error(
                static_cast<int>(error), std::system_category());
        }

        const DWORD waited = ::WaitForSingleObject(process, 30000U);
        const DWORD error = waited == WAIT_FAILED ? ::GetLastError() : ERROR_SUCCESS;
        ::CloseHandle(process);
        if (waited == WAIT_TIMEOUT)
        {
            std::cout << "[update] process " << pid
                << " is still running; carrying on\n";
        }
        else if (waited == WAIT_FAILED)
        {
            throw std::system_error(
                static_cast<int>(error), std::system_category());
        }
#else
        if (pid <= 0)
        {
            // GetProcessById rejects non-positive PIDs. The parse-failure
            // sentinel -1 must never reach kill(-1, 0), which queries every
            // process the caller may signal.
            Sleep(400ms);
            return;
        }

        bool exists = false;
        if (::kill(static_cast<pid_t>(pid), 0) == 0)
        {
            exists = true;
        }
        else
        {
            const int error = errno;
            exists = error == EPERM;
        }

        if (exists)
        {
            const auto deadline = std::chrono::steady_clock::now() + 30s;
            auto delay = 1ms;
            while (std::chrono::steady_clock::now() < deadline)
            {
                if (::kill(static_cast<pid_t>(pid), 0) != 0)
                {
                    const int error = errno;
                    if (error != EPERM)
                    {
                        exists = false;
                        break;
                    }
                }

                const auto now = std::chrono::steady_clock::now();
                if (now >= deadline)
                {
                    break;
                }
                const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - now);
                Sleep(std::min(delay, remaining));
                delay = std::min(delay * 2, 100ms);
            }
            if (exists)
            {
                std::cout << "[update] process " << pid
                    << " is still running; carrying on\n";
            }
        }
#endif
        Sleep(400ms);
    }

    void DesktopUpdate::Copy(const std::string& source, const std::string& target)
    {
        const fs::path sourcePath = PathFromUtf8(source);
        std::queue<fs::path> pending;
        pending.push(sourcePath);

        while (!pending.empty())
        {
            const fs::path directory = std::move(pending.front());
            pending.pop();

            std::error_code error;
            fs::directory_iterator iterator(directory, error);
            if (error)
            {
                ThrowFileError(PathToUtf8(directory), error);
            }
            const fs::directory_iterator end;
            for (; iterator != end; iterator.increment(error))
            {
                if (error)
                {
                    ThrowFileError(PathToUtf8(directory), error);
                }

                std::error_code typeError;
                const bool isDirectory = iterator->is_directory(typeError);
                if (isDirectory && !typeError)
                {
                    pending.push(iterator->path());
                    continue;
                }
                // FileSystemEntry.Initialize(..., continueOnError: true) treats
                // an unstatable symlink/unknown entry as a non-directory. It is
                // still yielded by EnumerateFiles; File.Copy reports the error.
                typeError.clear();

                const fs::path relative = iterator->path().lexically_relative(sourcePath);
                const fs::path destinationPath = PathFromUtf8(target) / relative;
                const fs::path directoryPath = destinationPath.parent_path();
                if (!directoryPath.empty())
                {
                    std::error_code createError;
                    fs::create_directories(directoryPath, createError);
                    if (createError)
                    {
                        ThrowFileError(PathToUtf8(directoryPath), createError);
                    }
                }
                CopyWithRetries(PathToUtf8(iterator->path()), PathToUtf8(destinationPath));
            }
            if (error)
            {
                ThrowFileError(PathToUtf8(directory), error);
            }
        }
    }

    void DesktopUpdate::CopyWithRetries(const std::string& from, const std::string& to)
    {
        for (std::int32_t attempt = 0; ; ++attempt)
        {
            try
            {
                CopyFileOnce(from, to);
                return;
            }
            catch (const IOException&)
            {
                if (attempt >= 20)
                {
                    throw;
                }
                Sleep(250ms);
            }
            catch (const UnauthorizedAccessException&)
            {
                if (attempt >= 20)
                {
                    throw;
                }
                Sleep(250ms);
            }
        }
    }

    void DesktopUpdate::Clean()
    {
        if (::MphRead::NativeRuntime::IsMacOS())
        {
            return;
        }
        try
        {
            const std::string staging = Staging();
            if (DirectoryExists(staging))
            {
                std::error_code error;
                fs::remove_all(PathFromUtf8(staging), error);
                if (error)
                {
                    ThrowFileError(staging, error);
                }
            }
        }
        catch (...)
        {
        }
    }

    void DesktopUpdate::MakeExecutable(const std::string& path)
    {
        if (IsWindows())
        {
            return;
        }
        try
        {
            std::error_code error;
            fs::permissions(PathFromUtf8(path),
                fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                fs::perm_options::add, error);
            if (error)
            {
                ThrowFileError(path, error);
            }
        }
        catch (const std::exception& ex)
        {
            std::cout << "[update] could not make " << path
                << " executable: " << ex.what() << '\n';
        }
        catch (...)
        {
            std::cout << "[update] could not make " << path
                << " executable: " << MessageForUnknownException() << '\n';
        }
    }
}
