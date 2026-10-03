#include "CompatibilityCheck.hpp"

#include "PlatformDiagnostics.hpp"
#include "../Branding.hpp"
#include "../InputSettings.hpp"
#include "../Launcher/Portable/LauncherPrefs.hpp"
#include "../MapGen/CustomRooms.hpp"
#include "../Platform/AppPaths.hpp"
#include "../../NativeRuntime/System/Console.hpp"
#include "../../NativeRuntime/System/ExceptionText.hpp"
#include "../../NativeRuntime/System/Exceptions.hpp"
#include "../../NativeRuntime/System/Guid.hpp"
#include "../../NativeRuntime/System/IO.hpp"
#include "../../NativeRuntime/System/NativeLibrary.hpp"
#include "../../NativeRuntime/System/Runtime.hpp"

#if !defined(__ANDROID__)
#include "../../NativeRuntime/OpenTK/AL.hpp"
#endif

#if defined(MPHREAD_SHELL)
#include "../Launcher/Gui/GuiLauncher.hpp"
#include "../../NativeRuntime/Avalonia/Platform.hpp"
#include "../../NativeRuntime/Skia/Skia.hpp"
#endif

#include "../../NativeRuntime/OpenTK/GLFW.hpp"

#include <array>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace MphRead::Mods::Diagnostics
{
    namespace Runtime = ::MphRead::NativeRuntime;

    namespace
    {
        class LoadedNativeLibrary final
        {
        public:
            explicit LoadedNativeLibrary(const std::string& path)
                : _handle(Runtime::NativeLibraryLoad(path))
            {
            }

            ~LoadedNativeLibrary()
            {
                Runtime::NativeLibraryFree(_handle);
            }

            LoadedNativeLibrary(const LoadedNativeLibrary&) = delete;
            LoadedNativeLibrary& operator=(const LoadedNativeLibrary&) = delete;

            void RequireExport(const std::string& name) const
            {
                static_cast<void>(Runtime::NativeLibraryGetExport(_handle, name));
            }

            [[nodiscard]] void* Export(const std::string& name) const
            {
                return Runtime::NativeLibraryGetExport(_handle, name);
            }

        private:
            void* _handle;
        };

        template <typename Action>
        void Check(std::string_view name, Action&& action, int& failures)
        {
            try
            {
                std::forward<Action>(action)();
                Runtime::ConsoleWriteLine("[OK] " + std::string(name));
            }
            catch (const std::exception&)
            {
                ++failures;
                Runtime::ConsoleErrorWriteLine("[FAIL] " + std::string(name) + " ("
                    + Runtime::RuntimeInformationProcessArchitecture() + "): "
                    + Runtime::ExceptionToString(std::current_exception()));
            }
        }

        void LoadNative(const std::string& file, const std::string& symbol)
        {
            const std::string path = Runtime::PathCombine(Platform::AppPaths::ExecutableDirectory(), file);
            Runtime::ConsoleWriteLine("Loading " + path);
            LoadedNativeLibrary library(path);
            library.RequireExport(symbol);
        }

        [[noreturn]] void ThrowInvalidOperation(std::string_view message)
        {
            throw System::InvalidOperationException(message);
        }
    }

    int CompatibilityCheck::Run()
    {
        Runtime::ConsoleWriteLine(std::string(Branding::Name) + " compatibility smoke test");
        Runtime::ConsoleWriteLine("Runtime: " + Runtime::RuntimeInformationFrameworkDescription());
        Runtime::ConsoleWriteLine("OS: " + Runtime::RuntimeInformationOSDescription());
        Runtime::ConsoleWriteLine("Architecture: " + Runtime::RuntimeInformationProcessArchitecture());
        Runtime::ConsoleWriteLine("RID: " + Runtime::RuntimeInformationRuntimeIdentifier());
        PlatformDiagnostics::Start();

        int failures = 0;
        Check("configuration", []
        {
            Launcher::LauncherPrefs::Load();
            InputSettings::Load();
            if (Runtime::IsMacOS()
                && Launcher::LauncherPrefs::Directory() == Platform::AppPaths::ExecutableDirectory())
            {
                ThrowInvalidOperation("macOS writes must use the user-data directory.");
            }
            const std::string probe = Runtime::PathCombine(Launcher::LauncherPrefs::Directory(),
                ".smoke-" + Runtime::Guid::NewGuid().ToString("N"));
            try
            {
                Runtime::FileWriteAllText(probe, "configuration write probe");
                if (Runtime::IsMacOS() && !Runtime::FileExists(Runtime::PathGetFileName(probe)))
                {
                    ThrowInvalidOperation("Relative writes must also use user data.");
                }
            }
            catch (...)
            {
                Runtime::FileDelete(probe);
                throw;
            }
            Runtime::FileDelete(probe);
        }, failures);

#if defined(MPHREAD_SHELL)
        Check("Avalonia", []
        {
            if (!Launcher::Gui::GuiLauncher::EnsureSetup(false))
            {
                ThrowInvalidOperation("Avalonia initialization failed.");
            }
        }, failures);
#if defined(MPHREAD_AVALONIA)
        Check("Skia", []
        {
            NativeRuntime::Skia::Bitmap bitmap(2, 2);
            bitmap.Clear(NativeRuntime::Skia::Color{0, 128, 0, 255});
            const std::uint8_t* pixel = bitmap.Pixels();
            if (pixel[0] != 0 || pixel[1] != 128 || pixel[2] != 0 || pixel[3] != 255)
            {
                ThrowInvalidOperation("Skia rasterization failed.");
            }
        }, failures);
#endif
        Check("launcher resources", []
        {
            constexpr std::array<std::string_view, 5> resources = {
                "fruity-prime-logo.png", "fruity-prime-mark.png", "Fonts/heyNovember.ttf",
                "Fonts/Roboto-Bold.ttf", "Backgrounds/launcher-bg.jpg"};
            for (const std::string_view resource : resources)
            {
                const std::vector<std::uint8_t> bytes = NativeRuntime::Avalonia::Platform::AssetLoader::Open(
                    "avares://FruityPrime/Assets/" + std::string(resource));
                if (bytes.empty())
                {
                    throw System::IO::InvalidDataException("Empty launcher resource: "
                        + std::string(resource));
                }
            }
        }, failures);
#endif

        if (Runtime::IsMacOS())
        {
            constexpr std::array<std::pair<std::string_view, std::string_view>, 6> libraries = {{
                {"libopenal.1.dylib", "alcOpenDevice"},
                {"libglfw.3.dylib", "glfwGetVersion"},
                {"libminiaudio.dylib", "ma_version_string"},
                {"libSkiaSharp.dylib", "sk_version_get_milestone"},
                {"libHarfBuzzSharp.dylib", "hb_version_string"},
                {"libAvaloniaNative.dylib", "CreateAvaloniaNative"}}};
            for (const auto& [file, symbol] : libraries)
            {
                Check(file, [file, symbol]
                {
                    LoadNative(std::string(file), std::string(symbol));
                }, failures);
            }
#if !defined(MPHREAD_SERVER) && !defined(__ANDROID__)
            Check("OpenAL bindings", []
            {
                const std::string path = Runtime::PathCombine(
                    Platform::AppPaths::ExecutableDirectory(), "libopenal.1.dylib");
                LoadedNativeLibrary library(path);
                static_cast<void>(OpenTK::Audio::OpenAL::ALC::GetCurrentContext());
                void* const bound = OpenTK::Audio::OpenAL::ALC::GetProcAddress(
                    OpenTK::Audio::OpenAL::ALDevice::Null, "alcOpenDevice");
                if (bound != library.Export("alcOpenDevice"))
                {
                    ThrowInvalidOperation("OpenTK resolved a different OpenAL library.");
                }
            }, failures);
#endif
            Check("GLFW bindings", []
            {
                std::int32_t major = 0;
                std::int32_t minor = 0;
                std::int32_t revision = 0;
                OpenTK::Windowing::GraphicsLibraryFramework::GLFW::GetVersion(major, minor, revision);
                static_cast<void>(minor);
                static_cast<void>(revision);
                if (major < 3)
                {
                    ThrowInvalidOperation("Unsupported GLFW version.");
                }
            }, failures);
        }

        Check("maps", []
        {
            const std::string& maps = MapGen::CustomRooms::MapDirectory();
            const bool exists = Runtime::DirectoryExists(maps)
                && Runtime::DirectoryAnyFileRecursive(maps, [](const std::string& path)
            {
                const std::string extension = Runtime::PathGetExtension(path);
                return extension == ".fpmap" || extension == ".json";
            });
            if (!exists)
            {
                throw System::IO::DirectoryNotFoundException("No bundled maps found in " + maps);
            }
            Runtime::ConsoleWriteLine("Maps: " + maps);
        }, failures);

        Runtime::ConsoleWriteLine(failures == 0
            ? "Smoke test passed." : "Smoke test failed (" + std::to_string(failures) + " checks).");
        return failures == 0 ? 0 : 1;
    }
}
