#include "Updater.hpp"
#include "BuildVersion.hpp"
#include "../Platform/WebLink.hpp"
#include "../../NativeRuntime/System/Console.hpp"
#include "../../NativeRuntime/System/Encoding.hpp"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#else
#include <fcntl.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

using ::MphRead::NativeRuntime::EnvironmentGetVariable;
using ::MphRead::NativeRuntime::Utf8ToWide;

namespace MphRead::Mods::Update
{
    namespace
    {
        constexpr std::int64_t UnixEpochTicks = 621'355'968'000'000'000LL;
        constexpr std::int64_t MaxDateTimeTicks = 3'155'378'975'999'999'999LL;

        struct State final
        {
            std::atomic_bool Disabled{false};
            std::mutex AvailableGate;
            std::optional<UpdateInfo> Available;
            std::atomic_bool Checked{false};
        };

        [[nodiscard]] State& GetState()
        {
            alignas(State) static unsigned char storage[sizeof(State)];
            static State* state = ::new (static_cast<void*>(storage)) State();
            return *state;
        }

        [[nodiscard]] const std::string& RequireString(
            const Detail::InitOnlyProperty<std::optional<std::string>>& value)
        {
            const std::optional<std::string>& string = value.Get();
            if (!string.has_value())
            {
                throw NullReferenceException();
            }
            return *string;
        }

        [[nodiscard]] std::string InterpolateString(
            const Detail::InitOnlyProperty<std::optional<std::string>>& value)
        {
            const std::optional<std::string>& string = value.Get();
            return string.has_value() ? *string : std::string{};
        }

        [[nodiscard]] std::int64_t UtcNowTicks()
        {
            using TimeSpan = Updater::TimeSpan;
            const auto sinceUnix = std::chrono::system_clock::now().time_since_epoch();
            const std::int64_t unixTicks = std::chrono::duration_cast<TimeSpan>(sinceUnix).count();
            return UnixEpochTicks + unixTicks;
        }

        [[nodiscard]] std::int64_t AddDateTimeTicks(
            std::int64_t ticks, Updater::TimeSpan value)
        {
            const std::int64_t delta = value.count();
            if (delta > 0)
            {
                if (ticks > MaxDateTimeTicks - delta)
                {
                    throw std::out_of_range(
                        "The added or subtracted value results in an un-representable "
                        "DateTime. (Parameter 't')");
                }
            }
            else if (delta < 0)
            {
                if (delta == std::numeric_limits<std::int64_t>::min()
                    || ticks < -delta)
                {
                    throw std::out_of_range(
                        "The added or subtracted value results in an un-representable "
                        "DateTime. (Parameter 't')");
                }
            }
            return ticks + delta;
        }

#ifdef _WIN32
        using ShellExecuteExWFunction = BOOL (WINAPI*)(SHELLEXECUTEINFOW*);
        using CoGetApartmentTypeFunction = HRESULT (WINAPI*)(int*, int*);
        using CoInitializeExFunction = HRESULT (WINAPI*)(void*, DWORD);
        using CoUninitializeFunction = void (WINAPI*)();

        [[nodiscard]] FARPROC LoadProcedure(const wchar_t* moduleName, const char* procedureName)
        {
            HMODULE module = ::GetModuleHandleW(moduleName);
            if (module == nullptr)
            {
                module = ::LoadLibraryW(moduleName);
            }
            return module == nullptr ? nullptr : ::GetProcAddress(module, procedureName);
        }

        [[nodiscard]] ShellExecuteExWFunction ShellExecuteExWApi()
        {
            static auto function = reinterpret_cast<ShellExecuteExWFunction>(
                LoadProcedure(L"shell32.dll", "ShellExecuteExW"));
            return function;
        }

        [[nodiscard]] CoGetApartmentTypeFunction CoGetApartmentTypeApi()
        {
            static auto function = reinterpret_cast<CoGetApartmentTypeFunction>(
                LoadProcedure(L"ole32.dll", "CoGetApartmentType"));
            return function;
        }

        [[nodiscard]] CoInitializeExFunction CoInitializeExApi()
        {
            static auto function = reinterpret_cast<CoInitializeExFunction>(
                LoadProcedure(L"ole32.dll", "CoInitializeEx"));
            return function;
        }

        [[nodiscard]] CoUninitializeFunction CoUninitializeApi()
        {
            static auto function = reinterpret_cast<CoUninitializeFunction>(
                LoadProcedure(L"ole32.dll", "CoUninitialize"));
            return function;
        }

        [[nodiscard]] bool CurrentThreadIsSta() noexcept
        {
            CoGetApartmentTypeFunction getApartmentType = CoGetApartmentTypeApi();
            if (getApartmentType == nullptr)
            {
                return false;
            }
            int apartmentType = -1;
            int qualifier = 0;
            const HRESULT result = getApartmentType(&apartmentType, &qualifier);
            return SUCCEEDED(result) && (apartmentType == 0 || apartmentType == 3);
        }

        [[nodiscard]] bool InvokeShellExecute(SHELLEXECUTEINFOW& info)
        {
            ShellExecuteExWFunction shellExecute = ShellExecuteExWApi();
            return shellExecute != nullptr && shellExecute(&info) != FALSE;
        }

        [[nodiscard]] bool StartWindowsUrl(std::string_view url)
        {
            const std::wstring wide = Utf8ToWide(url);

            SHELLEXECUTEINFOW info{};
            info.cbSize = sizeof(info);
            info.fMask = SEE_MASK_NOCLOSEPROCESS
                | SEE_MASK_FLAG_DDEWAIT
                | SEE_MASK_FLAG_NO_UI;
            info.lpVerb = nullptr;
            info.lpFile = wide.c_str();
            info.lpParameters = nullptr;
            info.lpDirectory = nullptr;
            info.nShow = SW_SHOWNORMAL;

            bool succeeded = false;
            if (CurrentThreadIsSta())
            {
                succeeded = InvokeShellExecute(info);
            }
            else
            {
                std::thread executionThread([&]
                {
                    CoInitializeExFunction initialize = CoInitializeExApi();
                    CoUninitializeFunction uninitialize = CoUninitializeApi();
                    if (initialize == nullptr || uninitialize == nullptr)
                    {
                        return;
                    }
                    constexpr DWORD CoinitApartmentThreaded = 0x2;
                    const HRESULT initialized = initialize(nullptr, CoinitApartmentThreaded);
                    if (FAILED(initialized))
                    {
                        return;
                    }
                    succeeded = InvokeShellExecute(info);
                    uninitialize();
                });
                executionThread.join();
            }

            if (info.hProcess != nullptr)
            {
                (void)::CloseHandle(info.hProcess);
            }
            return succeeded;
        }
#else
        void ReapChild(pid_t child) noexcept
        {
            int status = 0;
            while (::waitpid(child, &status, 0) < 0 && errno == EINTR)
            {
            }
        }

        [[nodiscard]] bool StartDesktopCommand(const char* program, std::string_view url)
        {
            pid_t child = -1;
            std::string urlArgument(url);
            char* const argv[] = {
                const_cast<char*>(program),
                urlArgument.data(),
                nullptr
            };

#if defined(__ANDROID__) && __ANDROID_API__ < 28
            // Android did not expose posix_spawnp until API 28, while this
            // target supports API 24. Mirror Process.Start's synchronous
            // exec-failure reporting with a close-on-exec pipe instead.
            int pipefd[2]{-1, -1};
            if (::pipe(pipefd) != 0)
            {
                return false;
            }
            const int flags = ::fcntl(pipefd[1], F_GETFD);
            if (flags < 0 || ::fcntl(pipefd[1], F_SETFD, flags | FD_CLOEXEC) < 0)
            {
                ::close(pipefd[0]);
                ::close(pipefd[1]);
                return false;
            }

            child = ::fork();
            if (child < 0)
            {
                ::close(pipefd[0]);
                ::close(pipefd[1]);
                return false;
            }
            if (child == 0)
            {
                ::close(pipefd[0]);
                ::execvp(program, argv);

                const int error = errno;
                const char* bytes = reinterpret_cast<const char*>(&error);
                std::size_t written = 0;
                while (written < sizeof(error))
                {
                    const ssize_t count = ::write(
                        pipefd[1], bytes + written, sizeof(error) - written);
                    if (count > 0)
                    {
                        written += static_cast<std::size_t>(count);
                    }
                    else if (count < 0 && errno == EINTR)
                    {
                        continue;
                    }
                    else
                    {
                        break;
                    }
                }
                ::_exit(127);
            }

            ::close(pipefd[1]);
            int launchError = 0;
            std::size_t received = 0;
            while (received < sizeof(launchError))
            {
                const ssize_t count = ::read(
                    pipefd[0],
                    reinterpret_cast<char*>(&launchError) + received,
                    sizeof(launchError) - received);
                if (count > 0)
                {
                    received += static_cast<std::size_t>(count);
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
                    received = sizeof(launchError);
                    break;
                }
            }
            ::close(pipefd[0]);
            if (received != 0)
            {
                ReapChild(child);
                return false;
            }
#else
            const int error = ::posix_spawnp(&child, program, nullptr, nullptr, argv, environ);
            if (error != 0)
            {
                return false;
            }
#endif
            try
            {
                std::thread(ReapChild, child).detach();
            }
            catch (...)
            {
                // Process.Start has already succeeded. Failure to create a
                // native reaper must not turn that successful launch into a
                // failed OpenUrl result.
            }
            return true;
        }
#endif
    }

    bool Updater::Disabled() noexcept
    {
        return GetState().Disabled.load(std::memory_order_relaxed);
    }

    void Updater::Disabled(bool value) noexcept
    {
        GetState().Disabled.store(value, std::memory_order_relaxed);
    }

    std::optional<UpdateInfo> Updater::Available()
    {
        State& state = GetState();
        std::lock_guard lock(state.AvailableGate);
        return state.Available;
    }

    bool Updater::Checked() noexcept
    {
        return GetState().Checked.load(std::memory_order_relaxed);
    }

    std::optional<UpdateInfo> Updater::Check(CancellationToken cancel)
    {
        if (Disabled())
        {
            return std::nullopt;
        }

        std::optional<UpdateInfo> update = UpdateCheck::Latest(cancel);
        State& state = GetState();
        {
            std::lock_guard lock(state.AvailableGate);
            state.Available = std::move(update);
        }
        state.Checked.store(true, std::memory_order_relaxed);
        return Available();
    }

    void Updater::CheckInBackground(
        std::function<void(UpdateInfo)> found,
        std::function<void()> done)
    {
        if (Disabled())
        {
            return;
        }

        std::thread([found = std::move(found), done = std::move(done)]() mutable
        {
            try
            {
                try
                {
                    std::optional<UpdateInfo> update = Check();
                    if (update.has_value())
                    {
                        found(*update);
                    }
                }
                catch (...)
                {
                    // A background check never exposes its failure to callers.
                }

                if (done)
                {
                    done();
                }
            }
            catch (...)
            {
                // Task.Run retains exceptions thrown by the completion callback
                // in its unobserved task. The native fire-and-forget equivalent
                // keeps them from escaping the worker thread.
            }
        }).detach();
    }

    void Updater::WaitForCheck(TimeSpan limit)
    {
        if (Disabled())
        {
            return;
        }

        const std::int64_t until = AddDateTimeTicks(UtcNowTicks(), limit);
        while (!Checked() && UtcNowTicks() < until)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    bool Updater::OpenPage(UpdateInfo update)
    {
        const std::string& page = RequireString(update.PageUrl);
        return OpenUrl(page.length() > 0
            ? std::string_view(page)
            : UpdateCheck::ReleasesPage);
    }

    bool Updater::OpenLink(const std::string& url)
    {
        return OpenUrl(url);
    }

    bool Updater::OpenLink(std::nullptr_t url)
    {
        return OpenUrl(url);
    }

    bool Updater::OpenUrl(std::string_view url)
    {
        if (!url.starts_with("https://"))
        {
            return false;
        }

        try
        {
            // The platform's own, where there is one: a phone has no
            // browser to start as a process and no display variable to
            // answer the question below with.
            if (const std::shared_ptr<Mods::Platform::IWebLink> link = Mods::Platform::WebLink::Current())
            {
                return link->Open(url);
            }
#ifdef _WIN32
            return StartWindowsUrl(url);
#elif defined(__APPLE__)
            return StartDesktopCommand("open", url);
#else
            if (EnvironmentGetVariable("DISPLAY").value_or("").empty()
                && EnvironmentGetVariable("WAYLAND_DISPLAY").value_or("").empty())
            {
                return false;
            }
            return StartDesktopCommand("xdg-open", url);
#endif
        }
        catch (...)
        {
            return false;
        }
    }

    bool Updater::OpenUrl(std::nullptr_t)
    {
        throw NullReferenceException();
    }

    std::string Updater::Describe(UpdateInfo update)
    {
        const std::string& assetName = RequireString(update.AssetName);
        std::string which;
        if (!assetName.empty())
        {
            which = " -- you want " + assetName;
        }

        const std::string tag = InterpolateString(update.Tag);
        const std::string display = BuildVersion::Display();
        return tag + " is available (this is " + display + ")" + which;
    }
}

namespace MphRead::Mods::Update::Detail
{
    bool ServerUpdateUpdaterDisabled()
    {
        return Updater::Disabled();
    }

    std::optional<UpdateInfo> ServerUpdateUpdaterCheck()
    {
        if (Updater::Disabled())
        {
            return std::nullopt;
        }
        return UpdateCheck::LatestServer();
    }

    std::string ServerUpdateUpdaterDescribe(UpdateInfo update)
    {
        return Updater::Describe(std::move(update));
    }
}
