#include "NativeFilePicker.hpp"

#include "../../DebugLog.hpp"
#include "../../../NativeRuntime/System/Console.hpp"
#include "../../../NativeRuntime/System/Encoding.hpp"
#include "../../../NativeRuntime/System/Globalization.hpp"
#include "../../../NativeRuntime/System/IO.hpp"
#include "../../../NativeRuntime/System/Process.hpp"
#include "../../../NativeRuntime/System/Runtime.hpp"

#include <exception>
#include <memory>
#include <mutex>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commdlg.h>
#include <objbase.h>
#endif

namespace MphRead::Mods::Launcher
{
    namespace Runtime = ::MphRead::NativeRuntime;

    namespace
    {
        // Path.PathSeparator.
#if defined(_WIN32)
        constexpr char PathSeparator = ';';
#else
        constexpr char PathSeparator = ':';
#endif

        [[nodiscard]] std::shared_future<std::optional<std::string>> FromResult(std::optional<std::string> value)
        {
            std::promise<std::optional<std::string>> promise;
            promise.set_value(std::move(value));
            return promise.get_future().share();
        }

        template <typename Function>
        [[nodiscard]] std::shared_future<std::optional<std::string>> TaskRun(Function function)
        {
            return std::async(std::launch::async, std::move(function)).share();
        }

#if defined(_WIN32)
        // The game's own window, when nobody published it: the one this thread
        // has active, else the foreground window if this process owns it.
        // A fullscreen game window is topmost, and a dialog with no owner is
        // not, so it opened behind the game where nobody could see it; owned
        // by the game it is always in front.
        [[nodiscard]] void* OwnWindow()
        {
            if (const HWND active = ::GetActiveWindow(); active != nullptr)
            {
                return active;
            }
            if (const HWND foreground = ::GetForegroundWindow(); foreground != nullptr)
            {
                DWORD process = 0;
                ::GetWindowThreadProcessId(foreground, &process);
                if (process == ::GetCurrentProcessId())
                {
                    return foreground;
                }
            }
            return nullptr;
        }
#endif

        [[nodiscard]] std::string Replace(std::string text, const std::string& from, const std::string& to)
        {
            std::size_t at = 0;
            while ((at = text.find(from, at)) != std::string::npos)
            {
                text.replace(at, from.size(), to);
                at += to.size();
            }
            return text;
        }
    }

#if defined(__ANDROID__)
    namespace
    {
        std::mutex AndroidGate;
        std::shared_ptr<std::promise<std::optional<std::string>>> AndroidPending;
    }

    void NativeFilePicker::AndroidRequest(std::function<void()> request)
    {
        _androidRequest = std::move(request);
    }

    void NativeFilePicker::Deliver(std::optional<std::string> path)
    {
        std::shared_ptr<std::promise<std::optional<std::string>>> pending;
        {
            std::lock_guard lock(AndroidGate);
            pending = std::move(AndroidPending);
        }
        if (pending)
        {
            pending->set_value(std::move(path));
        }
    }
#else
    void NativeFilePicker::AndroidRequest(std::function<void()>) {}
    void NativeFilePicker::Deliver(std::optional<std::string>) {}
#endif

    bool NativeFilePicker::Available()
    {
#if defined(__ANDROID__)
        return !_suppressed && static_cast<bool>(_androidRequest);
#elif defined(_WIN32) || defined(__APPLE__)
        return !_suppressed;
#elif defined(__linux__) && !defined(__ANDROID__)
        return !_suppressed && LinuxTool().has_value();
#else
        return false;
#endif
    }

    std::shared_future<std::optional<std::string>> NativeFilePicker::OpenFile(
        const std::string& title, const std::string& description, const std::string& extension)
    {
#if defined(_WIN32)
        return WindowsFile(title, description, extension, _owner);
#elif defined(__APPLE__)
        (void)description;
        return TaskRun([title, extension] { return MacFile(title, extension); });
#elif defined(__linux__) && !defined(__ANDROID__)
        return TaskRun([title, description, extension] { return LinuxFile(title, description, extension); });
#elif defined(__ANDROID__)
        (void)title;
        (void)description;
        (void)extension;
        if (!_androidRequest)
        {
            return FromResult(std::nullopt);
        }
        auto promise = std::make_shared<std::promise<std::optional<std::string>>>();
        std::shared_future<std::optional<std::string>> future = promise->get_future().share();
        std::shared_ptr<std::promise<std::optional<std::string>>> previous;
        {
            std::lock_guard lock(AndroidGate);
            previous = std::move(AndroidPending);
            AndroidPending = promise;
        }
        if (previous)
        {
            previous->set_value(std::nullopt);
        }
        _androidRequest();
        return future;
#else
        (void)title;
        (void)description;
        (void)extension;
        return FromResult(std::nullopt);
#endif
    }

    // ------------------------------------------------------------ windows

    // The common dialog on a thread of its own, STA -- the Explorer-style
    // dialog is a shell control -- which also keeps the frame loop drawing.
    std::shared_future<std::optional<std::string>> NativeFilePicker::WindowsFile(
        const std::string& title, const std::string& description, const std::string& extension, void* owner)
    {
#if defined(_WIN32)
        auto result = std::make_shared<std::promise<std::optional<std::string>>>();
        std::shared_future<std::optional<std::string>> future = result->get_future().share();
        // Looked up here, on the thread that called, since the window a
        // thread has active is that thread's to say.
        if (owner == nullptr)
        {
            owner = OwnWindow();
        }
        std::thread thread([result, title, description, extension, owner]
        {
            const HRESULT apartment = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            struct ApartmentGuard final
            {
                bool Initialized;
                ~ApartmentGuard()
                {
                    if (Initialized)
                    {
                        ::CoUninitialize();
                    }
                }
            } apartmentGuard{SUCCEEDED(apartment)};
            std::wstring buffer(MaxPath, L'\0');
            try
            {
                // Nulls inside the filter: pairs of strings, ended by an empty one.
                const std::wstring filter = Runtime::Utf8ToWide(description + " (*." + extension + ")") + L'\0'
                    + Runtime::Utf8ToWide("*." + extension) + L'\0' + L"Every file (*.*)" + L'\0' + L"*.*" + L'\0'
                    + L'\0';
                const std::wstring wideTitle = Runtime::Utf8ToWide(title);
                const std::wstring wideExtension = Runtime::Utf8ToWide(extension);
                OPENFILENAMEW options{};
                options.lStructSize = sizeof(options);
                // Owned by the game window, so a dialog over a borderless
                // fullscreen game is in front of it rather than behind it.
                options.hwndOwner = static_cast<HWND>(owner);
                options.lpstrFilter = filter.c_str();
                options.nFilterIndex = 1;
                options.lpstrFile = buffer.data();
                options.nMaxFile = MaxPath;
                options.lpstrTitle = wideTitle.c_str();
                options.lpstrDefExt = wideExtension.c_str();
                options.Flags = OpenFileFlags;
                if (::GetOpenFileNameW(&options) != FALSE)
                {
                    result->set_value(Runtime::WideToUtf8(std::wstring(buffer.c_str())));
                }
                else
                {
                    result->set_value(std::nullopt);
                }
            }
            catch (const std::exception& ex)
            {
                DebugLog::Exception("picker", ex);
                result->set_value(std::nullopt);
            }
            catch (...)
            {
                DebugLog::Exception("picker", std::current_exception());
                result->set_value(std::nullopt);
            }
        });
        thread.detach();
        return future;
#else
        (void)title;
        (void)description;
        (void)extension;
        (void)owner;
        return FromResult(std::nullopt);
#endif
    }

    // -------------------------------------------------------------- linux

    // The first of the desktop dialogs this box has, or none.
    std::optional<std::string> NativeFilePicker::LinuxTool()
    {
        for (const char* tool : {"zenity", "kdialog", "qarma"})
        {
            if (OnPath(tool))
            {
                return std::string(tool);
            }
        }
        return std::nullopt;
    }

    bool NativeFilePicker::OnPath(const std::string& tool)
    {
        const std::string paths = Runtime::EnvironmentGetVariable("PATH").value_or("");
        for (const std::string& directory : Runtime::StringSplit(paths, PathSeparator, true))
        {
            try
            {
                if (Runtime::FileExists(Runtime::PathCombine(directory, tool)))
                {
                    return true;
                }
            }
            catch (const std::invalid_argument&)
            {
                // A malformed entry in PATH is not this program's problem.
            }
        }
        return false;
    }

    std::optional<std::string> NativeFilePicker::LinuxFile(
        const std::string& title, const std::string& description, const std::string& extension)
    {
        const std::optional<std::string> tool = LinuxTool();
        if (!tool.has_value())
        {
            return std::nullopt;
        }
        std::vector<std::string> arguments;
        if (*tool == "kdialog")
        {
            arguments.emplace_back("--title");
            arguments.push_back(title);
            arguments.emplace_back("--getopenfilename");
            arguments.push_back(Runtime::EnvironmentUserProfile());
            arguments.push_back("*." + extension + "|" + description);
        }
        else
        {
            arguments.emplace_back("--file-selection");
            arguments.push_back("--title=" + title);
            arguments.push_back("--file-filter=" + description + " | *." + extension);
            arguments.emplace_back("--file-filter=Every file | *");
        }
        return RunTool(*tool, arguments);
    }

    // --------------------------------------------------------------- macos

    std::optional<std::string> NativeFilePicker::MacFile(const std::string& title, const std::string& extension)
    {
        // A prompt is still a string going into a script, so it is escaped.
        const std::string prompt = Replace(Replace(title, "\\", "\\\\"), "\"", "\\\"");
        return RunTool("osascript", {"-e",
            "POSIX path of (choose file with prompt \"" + prompt + "\" of type {\"" + extension + "\"})"});
    }

    // ---------------------------------------------------------------- both

    // Run the dialog and read the one line it prints. A non-zero exit is how
    // all three say "cancelled", which is not logged as a failure.
    std::optional<std::string> NativeFilePicker::RunTool(const std::string& tool, const std::vector<std::string>& arguments)
    {
        try
        {
            std::string outputBytes;
            const std::int32_t exitCode = Runtime::ProcessRunCaptureOutput(tool, arguments, outputBytes);
            if (exitCode != 0)
            {
                return std::nullopt;
            }
            const std::string output = Runtime::Utf8GetString(outputBytes);
            const std::string path = Runtime::StringTrim(output);
            return !path.empty() && Runtime::FileExists(path) ? std::optional<std::string>(path) : std::nullopt;
        }
        catch (const std::exception& ex)
        {
            DebugLog::Exception("picker", ex);
            return std::nullopt;
        }
        catch (...)
        {
            DebugLog::Exception("picker", std::current_exception());
            return std::nullopt;
        }
    }
}
