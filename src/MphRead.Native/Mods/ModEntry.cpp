#include "ModEntry.hpp"
#include "../NativeRuntime/Rhi/SceneBackend.hpp"
#include "Platform/AppPaths.hpp"

#include "../Entities/Players/PlayerEntity.hpp"
#include "../Features.hpp"
#include "../Formats/Enums.hpp"
#include "../Utility/Console.hpp"
#include "Branding.hpp"
#include "ConsoleWindow.hpp"
#include "Credits.hpp"
#include "DebugLog.hpp"
#include "Diagnostics/CompatibilityCheck.hpp"
#include "Diagnostics/GpuLifetimeCheck.hpp"
#include "Diagnostics/RhiConformanceCheck.hpp"
#include "Diagnostics/PresentConformanceCheck.hpp"
#include "Diagnostics/BackdropParityCheck.hpp"
#include "Diagnostics/FramePerformance.hpp"
#include "Diagnostics/PlatformDiagnostics.hpp"
#if defined(MPHREAD_AVALONIA_SHELL)
#include "Diagnostics/GlfwPathCheck.hpp"
#include "Diagnostics/LauncherWindowCheck.hpp"
#include "Diagnostics/ThumbnailWindowCheck.hpp"
#endif
#include "Input/AimAssist/AimAssistDebug.hpp"
#include "Input/AimAssist/AimAssistTelemetry.hpp"
#include "Input/GamepadChecks.hpp"
#include "Input/GamepadProbe.hpp"
#include "Input/PointerCheck.hpp"
#include "InputSettings.hpp"
#include "MapGen/AltFormProbe.hpp"
#include "MapGen/MapCheck.hpp"
#include "Multiplayer/ResourceAudit.hpp"
#if defined(MPHREAD_AVALONIA)
#include "Launcher/Gui/GuiLauncher.hpp"
#include "Launcher/Gui/TapCheck.hpp"
#include "Launcher/Gui/UiCapture.hpp"
#include "Launcher/Gui/UiDesigns.hpp"
#endif
#if defined(MPHREAD_AVALONIA_SHELL)
#include "Launcher/Gui/DeckTile.hpp"
#include "Launcher/Gui/UiBench.hpp"
#include "Launcher/Gui/UiSurface.hpp"
#endif
#if defined(MPHREAD_SHELL)
#include "Launcher/Gui/GuiLauncher.hpp"
#include "Launcher/Gui/Shell.hpp"
#endif
#include "Launcher/Portable/LauncherPrefs.hpp"
#include "Launcher/Portable/TextLauncher.hpp"
#include "MapGen/CustomRooms.hpp"
#include "MapGen/MapBundle.hpp"
#include "MapGen/MapPacker.hpp"
#include "MapGen/MapReport.hpp"
#include "MapGen/MapTextureBake.hpp"
#include "MapGen/Q3Bsp.hpp"
#include "MapGen/Q3Convert.hpp"
#include "NativeRuntime/System/ExceptionText.hpp"
#include "Network/DedicatedServer.hpp"
#include "Network/DemoInfo.hpp"
#include "Network/HealthSimulationTest.hpp"
#include "Network/HitRig.hpp"
#include "Network/LocalServer.hpp"
#include "Network/MapAudit.hpp"
#include "Network/MapRotation.hpp"
#include "Network/MechanicsDump.hpp"
#include "Network/NetCheckClient.hpp"
#include "Network/NetConnectCommand.hpp"
#include "Network/NetDiagnostics.hpp"
#include "Network/NetHooks.hpp"
#include "Network/NetHitClaims.hpp"
#include "Network/NetHitPrediction.hpp"
#include "Network/NetLag.hpp"
#include "MapGen/MapDefinition.hpp"
#include "Network/NetMaster.hpp"
#include "Network/NetSmoothing.hpp"
#include "Network/NetStatus.hpp"
#include "Network/NetUnlagged.hpp"
#include "Network/SpireAltPoseCheck.hpp"
#include "Network/ServerSimCheck.hpp"
#include "Network/WeaponDps.hpp"
#include "Render/Crosshair.hpp"
#include "Render/FrameTiming.hpp"
#include "Render/FrameTimingCheck.hpp"
#include "Render/GoldenCapture.hpp"
#include "../NativeRuntime/Rhi/Vulkan/VulkanContext.hpp"
#include "../NativeRuntime/Rhi/Vulkan/VulkanSwapchain.hpp"
#include "Render/Radar.hpp"
#include "RenderOptions.hpp"
#include "ShutdownSignals.hpp"
#include "ThumbnailBatch.hpp"
#include "ThumbnailCapture.hpp"
#include "ThumbnailGenerator.hpp"
#include "Update/DesktopUpdate.hpp"
#include "Update/ServerUpdate.hpp"
#include "Update/UpdateCheck.hpp"
#include "Update/UpdateInstall.hpp"
#include "Update/Updater.hpp"
#include "WindowMode.hpp"
#include "../NativeRuntime/System/Encoding.hpp"
#include "../NativeRuntime/System/Console.hpp"
#include "../NativeRuntime/System/Globalization.hpp"
#include "../NativeRuntime/System/IO.hpp"
#include "../NativeRuntime/System/Managed.hpp"
#include "../NativeRuntime/System/Runtime.hpp"
#include "../NativeRuntime/System/Tasks.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <conio.h>
#include <windows.h>
#elif defined(__APPLE__)
#include <crt_externs.h>
#include <limits.h>
#include <mach-o/dyld.h>
#include <unistd.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

using ::MphRead::NativeRuntime::EnvironmentProcessPath;
using ::MphRead::NativeRuntime::Int32TryParseCurrentCulture;
using ::MphRead::NativeRuntime::PathCombine;
using ::MphRead::NativeRuntime::PathFromUtf8;
using ::MphRead::NativeRuntime::PathToUtf8;
using ::MphRead::NativeRuntime::RequireReference;
using ::MphRead::NativeRuntime::StringEqualsOrdinalIgnoreCase;
using ::MphRead::NativeRuntime::WideToWtf8;

// Environment.ExitCode is process state, not an immediate exit. The executable
// wrapper is the Native equivalent of the CLR host and owns the eventual return
// from main; this two-file port only assigns the state through that runtime
// boundary. A focused probe provides the boundary while peer Native entry files
// are still being ported.

namespace
{
    using MphRead::BeamType;
    using MphRead::GameMode;
    using MphRead::Hunter;

    [[nodiscard]] bool IsAsciiWhitespace(unsigned char ch) noexcept
    {
        return ch == 0x20 || (ch >= 0x09 && ch <= 0x0D);
    }

    [[nodiscard]] std::string_view TrimNumberWhitespace(std::string_view value) noexcept
    {
        while (!value.empty() && IsAsciiWhitespace(static_cast<unsigned char>(value.front())))
        {
            value.remove_prefix(1);
        }
        while (!value.empty() && IsAsciiWhitespace(static_cast<unsigned char>(value.back())))
        {
            value.remove_suffix(1);
        }
        return value;
    }

    [[nodiscard]] std::string_view TrimAscii(std::string_view value) noexcept
    {
        return TrimNumberWhitespace(value);
    }

    [[nodiscard]] std::string_view TrimStartHyphen(std::string_view value) noexcept
    {
        while (!value.empty() && value.front() == '-')
        {
            value.remove_prefix(1);
        }
        return value;
    }

    [[nodiscard]] bool IsFlag(std::string_view argument, std::string_view name) noexcept
    {
        return StringEqualsOrdinalIgnoreCase(TrimStartHyphen(argument), name);
    }

    [[nodiscard]] bool HasFlag(const std::vector<std::string>& args, std::string_view name) noexcept
    {
        for (const std::string& argument : args)
        {
            if (IsFlag(argument, name))
            {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] int IndexOfFlag(const std::vector<std::string>& args, std::string_view name) noexcept
    {
        for (std::size_t i = 0; i < args.size(); ++i)
        {
            if (IsFlag(args[i], name))
            {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    [[nodiscard]] std::optional<std::string> ValueAfter(
        const std::vector<std::string>& args, std::string_view name)
    {
        if (args.size() < 2)
        {
            return std::nullopt;
        }
        for (std::size_t i = 0; i + 1 < args.size(); ++i)
        {
            if (IsFlag(args[i], name))
            {
                return args[i + 1];
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::vector<std::string> ValuesAfter(
        const std::vector<std::string>& args, std::string_view name)
    {
        std::vector<std::string> result;
        if (args.size() < 2)
        {
            return result;
        }
        for (std::size_t i = 0; i + 1 < args.size(); ++i)
        {
            if (IsFlag(args[i], name))
            {
                result.push_back(args[i + 1]);
            }
        }
        return result;
    }

    [[nodiscard]] bool StartsWithHyphen(const std::optional<std::string>& value) noexcept
    {
        return value.has_value() && !value->empty() && value->front() == '-';
    }

    template <typename Float>
    [[nodiscard]] bool TryParseFloatingClassic(std::string_view text, Float& value, bool allowThousands)
    {
        using ::MphRead::NativeRuntime::NumberFormatInfo;
        using ::MphRead::NativeRuntime::NumberStyles;
        const NumberStyles styles = allowThousands
            ? NumberStyles::Float | NumberStyles::AllowThousands
            : NumberStyles::Float;
        if constexpr (std::is_same_v<Float, float>)
        {
            return ::MphRead::NativeRuntime::TryParseSingle(text, styles, NumberFormatInfo::InvariantInfo(), value);
        }
        else
        {
            return ::MphRead::NativeRuntime::TryParseDouble(text, styles, NumberFormatInfo::InvariantInfo(), value);
        }
    }
    [[nodiscard]] bool TryParseDoubleInvariant(
        const std::optional<std::string>& text, double& value, bool allowThousands = true)
    {
        if (!text.has_value())
        {
            value = 0;
            return false;
        }
        return TryParseFloatingClassic(*text, value, allowThousands);
    }

    [[nodiscard]] bool TryParseFloatInvariant(
        const std::optional<std::string>& text, float& value, bool allowThousands = true)
    {
        if (!text.has_value())
        {
            value = 0;
            return false;
        }
        return TryParseFloatingClassic(*text, value, allowThousands);
    }

    [[nodiscard]] bool TryParseDoubleCurrent(std::string_view text, double& value)
    {
        return ::MphRead::NativeRuntime::DoubleTryParseCurrentCulture(text, value);
    }
    [[nodiscard]] bool TryParseGameMode(const std::optional<std::string>& text, GameMode& value)
    {
        if (!text.has_value())
        {
            return false;
        }
        return ::MphRead::TryParse(*text, true, value);
    }

    [[nodiscard]] bool TryParseHunter(const std::optional<std::string>& text, Hunter& value)
    {
        if (!text.has_value())
        {
            return false;
        }
        return ::MphRead::TryParse(*text, true, value);
    }

    [[nodiscard]] bool TryParseBeam(const std::optional<std::string>& text, BeamType& value)
    {
        if (!text.has_value())
        {
            return false;
        }
        return ::MphRead::TryParse(*text, true, value);
    }

    [[nodiscard]] std::vector<std::string> GetCommandLineArguments()
    {
#if defined(_WIN32)
        using CommandLineToArgvWFunction = LPWSTR* (WINAPI*)(LPCWSTR, int*);
        HMODULE shell = LoadLibraryW(L"shell32.dll");
        if (shell == nullptr)
        {
            throw std::runtime_error("LoadLibraryW(shell32.dll) failed");
        }
        auto commandLineToArgvW = reinterpret_cast<CommandLineToArgvWFunction>(
            GetProcAddress(shell, "CommandLineToArgvW"));
        if (commandLineToArgvW == nullptr)
        {
            FreeLibrary(shell);
            throw std::runtime_error("CommandLineToArgvW is unavailable");
        }
        int argc = 0;
        LPWSTR* argv = commandLineToArgvW(GetCommandLineW(), &argc);
        if (argv == nullptr)
        {
            FreeLibrary(shell);
            throw std::runtime_error("CommandLineToArgvW failed");
        }
        std::vector<std::string> result;
        result.reserve(static_cast<std::size_t>(argc));
        try
        {
            for (int i = 0; i < argc; ++i)
            {
                result.push_back(WideToWtf8(argv[i]));
            }
        }
        catch (...)
        {
            LocalFree(argv);
            FreeLibrary(shell);
            throw;
        }
        LocalFree(argv);
        FreeLibrary(shell);
        return result;
#elif defined(__APPLE__)
        const int argc = *_NSGetArgc();
        char** argv = *_NSGetArgv();
        std::vector<std::string> result;
        result.reserve(static_cast<std::size_t>(std::max(argc, 0)));
        for (int i = 0; i < argc; ++i)
        {
            result.emplace_back(argv[i] == nullptr ? "" : argv[i]);
        }
        return result;
#else
        std::ifstream input("/proc/self/cmdline", std::ios::binary);
        if (!input)
        {
            throw std::runtime_error("could not read /proc/self/cmdline");
        }
        const std::string data((std::istreambuf_iterator<char>(input)),
            std::istreambuf_iterator<char>());
        std::vector<std::string> result;
        std::size_t start = 0;
        while (start < data.size())
        {
            const std::size_t end = data.find('\0', start);
            if (end == std::string::npos)
            {
                result.emplace_back(data.substr(start));
                break;
            }
            result.emplace_back(data.substr(start, end - start));
            start = end + 1;
        }
        return result;
#endif
    }

    [[nodiscard]] std::string AppBaseDirectory()
    {
        const std::optional<std::string> path = EnvironmentProcessPath();
        if (!path.has_value())
        {
            throw std::runtime_error("process path is unavailable");
        }
        std::filesystem::path directory = PathFromUtf8(*path).parent_path();
        directory = std::filesystem::absolute(directory);
        std::string result = PathToUtf8(directory);
        if (!result.empty() && result.back() != std::filesystem::path::preferred_separator)
        {
            result.push_back(std::filesystem::path::preferred_separator);
        }
        return result;
    }

    [[nodiscard]] std::string MachineName()
    {
#if defined(_WIN32)
        DWORD size = 256;
        std::vector<wchar_t> buffer(size);
        for (;;)
        {
            DWORD actual = size;
            if (GetComputerNameW(buffer.data(), &actual) != FALSE)
            {
                return WideToWtf8(std::wstring_view(buffer.data(), actual));
            }
            if (GetLastError() != ERROR_BUFFER_OVERFLOW)
            {
                throw std::runtime_error("GetComputerNameW failed");
            }
            size *= 2;
            buffer.resize(size);
        }
#else
        std::array<char, 256> buffer{};
        if (gethostname(buffer.data(), buffer.size()) != 0)
        {
            throw std::runtime_error("gethostname failed");
        }
        buffer.back() = '\0';
        return std::string(buffer.data());
#endif
    }

    [[nodiscard]] std::string FullPathCombine(
        std::string_view directory, std::string_view value)
    {
        const std::filesystem::path combined = PathFromUtf8(directory) / PathFromUtf8(value);
        return PathToUtf8(std::filesystem::absolute(combined).lexically_normal());
    }

    [[maybe_unused, nodiscard]] std::string FileNameWithoutExtension(const std::string& path)
    {
        return PathToUtf8(PathFromUtf8(path).stem());
    }

    void WriteLine(std::string_view value)
    {
        std::cout << value << '\n';
    }

    void SetExitCode(std::int32_t value) noexcept
    {
        MphRead::NativeRuntime::SetEnvironmentExitCode(value);
    }

    [[nodiscard]] std::size_t DotNetUtf16Length(std::string_view value) noexcept
    {
        // Native strings are UTF-8; C# alignment pads by System.String.Length,
        // i.e. UTF-16 code units rather than UTF-8 bytes or Unicode scalars.
        std::size_t units = 0;
        for (std::size_t i = 0; i < value.size();)
        {
            const unsigned char lead = static_cast<unsigned char>(value[i]);
            std::uint32_t codePoint = lead;
            std::size_t length = 1;
            if ((lead & 0xE0) == 0xC0 && i + 1 < value.size())
            {
                codePoint = lead & 0x1F;
                length = 2;
            }
            else if ((lead & 0xF0) == 0xE0 && i + 2 < value.size())
            {
                codePoint = lead & 0x0F;
                length = 3;
            }
            else if ((lead & 0xF8) == 0xF0 && i + 3 < value.size())
            {
                codePoint = lead & 0x07;
                length = 4;
            }
            for (std::size_t j = 1; j < length; ++j)
            {
                const unsigned char next = static_cast<unsigned char>(value[i + j]);
                if ((next & 0xC0) != 0x80)
                {
                    length = 1;
                    codePoint = lead;
                    break;
                }
                codePoint = (codePoint << 6) | (next & 0x3F);
            }
            units += codePoint > 0xFFFF ? 2 : 1;
            i += length;
        }
        return units;
    }

    [[nodiscard]] std::string LeftAlign(std::string_view value, std::size_t width)
    {
        const std::size_t length = DotNetUtf16Length(value);
        if (length >= width)
        {
            return std::string(value);
        }
        std::string result(value);
        result.append(width - length, ' ');
        return result;
    }

    [[nodiscard]] std::pair<int, int> ParseSize(const std::vector<std::string>& args)
    {
        std::optional<std::string> value = ValueAfter(args, "size");
        if (value.has_value())
        {
            std::vector<std::string_view> parts;
            std::size_t start = 0;
            for (std::size_t i = 0; i <= value->size(); ++i)
            {
                if (i == value->size() || (*value)[i] == 'x' || (*value)[i] == 'X')
                {
                    parts.emplace_back(value->data() + start, i - start);
                    start = i + 1;
                }
            }
            std::int32_t width = 0;
            std::int32_t height = 0;
            if (parts.size() == 2 && Int32TryParseCurrentCulture(parts[0], width)
                && Int32TryParseCurrentCulture(parts[1], height) && width > 0 && height > 0)
            {
                return {width, height};
            }
            WriteLine("[thumbnails] ignoring -size " + *value
                + " (expected e.g. 1920x1440)");
        }
        return {MphRead::Mods::ThumbnailGenerator::ThumbnailWidth,
            MphRead::Mods::ThumbnailGenerator::ThumbnailHeight};
    }

    [[nodiscard]] int ParsePort(const std::vector<std::string>& args)
    {
        const std::optional<std::string> value = ValueAfter(args, "port");
        std::int32_t port = 0;
        if (value.has_value() && Int32TryParseCurrentCulture(*value, port))
        {
            return port;
        }
        return MphRead::Mods::Network::NetConfig::DefaultPort;
    }

    [[nodiscard]] int ParseMasterPort(const std::vector<std::string>& args)
    {
        const std::optional<std::string> value = ValueAfter(args, "masterport");
        std::int32_t port = 0;
        if (value.has_value() && Int32TryParseCurrentCulture(*value, port))
        {
            return port;
        }
        return MphRead::Mods::Network::NetMasterConfig::DefaultPort;
    }

    [[nodiscard]] std::string ParseName(const std::vector<std::string>& args)
    {
        const std::optional<std::string> value = ValueAfter(args, "name");
        return value.has_value() ? *value : MachineName();
    }

    [[nodiscard]] Hunter ParseHunter(const std::vector<std::string>& args)
    {
        Hunter hunter = Hunter::Samus;
        (void)TryParseHunter(ValueAfter(args, "hunter"), hunter);
        return hunter;
    }

    [[nodiscard]] int ParseRecolor(const std::vector<std::string>& args)
    {
        const std::optional<std::string> value = ValueAfter(args, "recolor");
        std::int32_t recolor = 0;
        if (value.has_value() && Int32TryParseCurrentCulture(*value, recolor))
        {
            return recolor;
        }
        return 0;
    }

    void ApplyRenderOverrides(const std::vector<std::string>& args)
    {
        using MphRead::Features;
        using MphRead::Mods::RenderOptions;
        using MphRead::Mods::Render::Crosshair;
        using MphRead::Mods::Render::FrameTiming;

        // -rhi vulkan: the scene draws through the Vulkan backend, offscreen,
        // into targets the captures read back. -vkvalidation adds the layers.
        const std::optional<std::string> rhi = ValueAfter(args, "rhi");
        ::MphRead::NativeRuntime::Rhi::SceneBackendRequest backend{};
        if (rhi.has_value() && ::MphRead::NativeRuntime::Rhi::ParseSceneBackendRequest(*rhi, backend))
        {
            ::MphRead::NativeRuntime::Rhi::RequestSceneBackend(backend, true);
            std::cout << "[render] scene backend requested: "
                << ::MphRead::NativeRuntime::Rhi::SceneBackendRequestName(backend) << std::endl;
        }
        if (HasFlag(args, "vkvalidation")) ::MphRead::NativeRuntime::Rhi::SetSceneValidation(true);

        const std::optional<std::string> cel = ValueAfter(args, "cel");
        if (cel.has_value() && !StartsWithHyphen(cel))
        {
            RenderOptions::CelShading(
                RenderOptions::ParseOnOff(*cel, RenderOptions::CelShading()));
        }
        else if (HasFlag(args, "cel"))
        {
            RenderOptions::CelShading(true);
        }

        const std::optional<std::string> fog = ValueAfter(args, "fog");
        if (fog.has_value() && !StartsWithHyphen(fog))
        {
            RenderOptions::Fog(RenderOptions::ParseOnOff(*fog, RenderOptions::Fog()));
        }

        const std::optional<std::string> fov = ValueAfter(args, "fov");
        if (fov.has_value() && !StartsWithHyphen(fov))
        {
            RenderOptions::FieldOfView(RenderOptions::ParseFov(
                std::string_view(*fov), RenderOptions::FieldOfView()));
        }

        const std::optional<std::string> fps = ValueAfter(args, "fps");
        if (fps.has_value() && !StartsWithHyphen(fps))
        {
            RenderOptions::ShowFps(RenderOptions::ParseOnOff(*fps, RenderOptions::ShowFps()));
        }
        else if (HasFlag(args, "fps"))
        {
            RenderOptions::ShowFps(true);
        }

        const std::optional<std::string> fpsCap = ValueAfter(args, "fpscap");
        const auto measure = ValueAfter(args, "fpsmeasure");
        if (measure && !StartsWithHyphen(measure))
        {
            std::optional<int> measurementCap;
            if (fpsCap && !StartsWithHyphen(fpsCap))
                measurementCap = MphRead::NativeRuntime::StringEqualsOrdinalIgnoreCase(*fpsCap, "unlimited")
                    || MphRead::NativeRuntime::StringEqualsOrdinalIgnoreCase(*fpsCap, "uncapped")
                    ? -1 : FrameTiming::ParseCap(*fpsCap, FrameTiming::FrameRateCap());
            MphRead::Mods::Diagnostics::FramePerformance::Configure(*measure, HasFlag(args, "gpuprofile"), measurementCap);
        }
        if (fpsCap.has_value() && !StartsWithHyphen(fpsCap))
        {
            FrameTiming::SetFrameRateCap(
                FrameTiming::ParseCap(*fpsCap, FrameTiming::FrameRateCap()));
        }

        const std::optional<std::string> bands = ValueAfter(args, "celbands");
        std::int32_t parsedBands = 0;
        if (bands.has_value() && Int32TryParseCurrentCulture(*bands, parsedBands))
        {
            RenderOptions::CelBands(parsedBands);
        }

        const std::optional<std::string> edge = ValueAfter(args, "celedge");
        if (edge.has_value())
        {
            std::string trimmed = *edge;
            while (!trimmed.empty() && trimmed.back() == '%')
            {
                trimmed.pop_back();
            }
            std::int32_t edgePercent = 0;
            if (Int32TryParseCurrentCulture(trimmed, edgePercent))
            {
                RenderOptions::CelEdge(edgePercent / 100.0f);
            }
        }

        const std::optional<std::string> proHud = ValueAfter(args, "prohud");
        if (proHud.has_value() && !StartsWithHyphen(proHud))
        {
            Features::ProHud(RenderOptions::ParseOnOff(*proHud, Features::ProHud()));
        }
        else if (HasFlag(args, "prohud"))
        {
            Features::ProHud(true);
        }

        const std::optional<std::string> weaponStyle = ValueAfter(args, "weaponstyle");
        if (weaponStyle.has_value() && !StartsWithHyphen(weaponStyle))
        {
            if (StringEqualsOrdinalIgnoreCase(*weaponStyle, "static")
                || StringEqualsOrdinalIgnoreCase(*weaponStyle, "quake"))
            {
                Features::ProHudFixedWeapon(true);
                Features::FixedWeapon(true);
            }
            else if (StringEqualsOrdinalIgnoreCase(*weaponStyle, "dynamic")
                || StringEqualsOrdinalIgnoreCase(*weaponStyle, "metroid"))
            {
                Features::ProHudFixedWeapon(false);
                Features::FixedWeapon(false);
            }
        }

        const std::optional<std::string> crosshair = ValueAfter(args, "crosshair");
        if (crosshair.has_value() && !StartsWithHyphen(crosshair))
        {
            Crosshair::Style = Crosshair::ParseStyle(*crosshair, Crosshair::Style);
        }

        const std::optional<std::string> crosshairSize = ValueAfter(args, "crosshairsize");
        if (crosshairSize.has_value() && !StartsWithHyphen(crosshairSize))
        {
            Crosshair::Size = Crosshair::ParseSize(*crosshairSize, Crosshair::Size);
        }

        const std::optional<std::string> radar = ValueAfter(args, "radar");
        if (radar.has_value() && !StartsWithHyphen(radar))
        {
            MphRead::Mods::Render::Radar::Enabled = RenderOptions::ParseOnOff(
                std::string_view(*radar), MphRead::Mods::Render::Radar::Enabled);
        }
        else if (HasFlag(args, "radar"))
        {
            MphRead::Mods::Render::Radar::Enabled = true;
        }

        const std::optional<std::string> radarBackground = ValueAfter(args, "radarbackground");
        if (radarBackground.has_value() && !StartsWithHyphen(radarBackground))
        {
            MphRead::Mods::Render::Radar::ShowBackground = RenderOptions::ParseOnOff(
                std::string_view(*radarBackground), MphRead::Mods::Render::Radar::ShowBackground);
        }

        const std::optional<std::string> radarOutlines = ValueAfter(args, "radaroutlines");
        if (radarOutlines.has_value() && !StartsWithHyphen(radarOutlines))
        {
            MphRead::Mods::Render::Radar::ShowOutlines = RenderOptions::ParseOnOff(
                std::string_view(*radarOutlines), MphRead::Mods::Render::Radar::ShowOutlines);
        }
    }

#if defined(MPHREAD_SERVER)
    void ServerUsage()
    {
        using MphRead::Mods::Branding;
        using MphRead::Mods::ConsoleWindow;
        using MphRead::Mods::Network::NetConfig;
        using MphRead::Mods::Network::NetMasterConfig;

        const std::optional<std::string> processPath = EnvironmentProcessPath();
        std::string executable = processPath.has_value()
            ? FileNameWithoutExtension(*processPath)
            : std::string("MphReadServer");

        WriteLine("");
        WriteLine(std::string(Branding::Name) + " dedicated server. It needs no game files.");
        WriteLine("");
        WriteLine("  " + executable + " -server -port " + std::to_string(NetConfig::DefaultPort)
            + " -players 8 -servername \"My server\"");
        WriteLine("      run a server. Maps come from maprotation.txt, written");
        WriteLine("      beside this program on first run.");
        WriteLine("");
        WriteLine("  " + executable + " -masterserver -port "
            + std::to_string(NetMasterConfig::DefaultPort));
        WriteLine("      run a server directory of your own.");
        WriteLine("");
        WriteLine("  " + executable + " -servers");
        WriteLine("      list the servers that are up right now.");
        WriteLine("");
        WriteLine("A server lists itself on " + std::string(NetMasterConfig::DefaultHost)
            + " so players can find it;");
        WriteLine("-nomaster keeps it off every list. See SERVER.txt.");
        WriteLine("");
#if defined(_WIN32)
        if (ConsoleWindow::OwnsItsConsole())
        {
            WriteLine("Press any key to close this window...");
            (void)_getche();
        }
#endif
    }
#endif

    void ListServers(const std::string& masterHost,
        const std::optional<std::string>& portValue)
    {
        using namespace MphRead::Mods::Network;

        int port = NetMasterConfig::DefaultPort;
        std::int32_t parsedPort = 0;
        if (portValue.has_value() && Int32TryParseCurrentCulture(*portValue, parsedPort))
        {
            port = parsedPort;
        }
        WriteLine("[servers] asking " + masterHost + ":" + std::to_string(port));
        const auto result = NetMasterClient::Query(masterHost, port);
        if (!result.Answered)
        {
            WriteLine("[servers] no answer from " + masterHost + ":" + std::to_string(port)
                + " -- it may be down, or UDP may not reach it");
            return;
        }
        if (result.CanHost == true)
        {
            WriteLine("[servers] this directory will start games for players");
        }
        else if (result.CanHost == false)
        {
            WriteLine("[servers] this directory starts no games (no host port range)");
        }
        else
        {
            WriteLine("[servers] this directory is from before it could say whether it "
                "starts games; it is offered anyway, since hosting is the default");
        }
        if (RequireReference(result.Servers).empty())
        {
            WriteLine("[servers] the directory is up and has nobody listed");
            return;
        }
        WriteLine("[servers] " + std::to_string(RequireReference(result.Servers).size())
            + " listed; asking each one");
        for (const auto& listing : RequireReference(result.Servers))
        {
            const auto status = NetStatus::Query(listing.Address, listing.Port,
                false /* allowJoinProbe */);
            std::string name;
            if (!status.ServerName.empty())
            {
                name = status.ServerName;
            }
            else if (!listing.ServerName.empty())
            {
                name = listing.ServerName;
            }
            else
            {
                name = listing.Endpoint();
            }
            if (!status.Online)
            {
                WriteLine("  " + LeftAlign(name, 24) + " "
                    + LeftAlign(listing.Endpoint(), 26) + " did not answer");
                continue;
            }
            const std::string players = status.MaxPlayers > 0
                ? std::to_string(status.Players) + "/" + std::to_string(status.MaxPlayers)
                : std::to_string(status.Players);
            const std::string ping = status.Latency >= 0
                ? std::to_string(status.Latency) + " ms"
                : std::string("-- ms");
            WriteLine("  " + LeftAlign(name, 24) + " "
                + LeftAlign(listing.Endpoint(), 26) + " "
                + LeftAlign(status.RoomKey, 20) + " "
                + LeftAlign(NetStatus::ModeName(status.Mode), 14) + " "
                + LeftAlign(players, 6) + " " + ping);
        }
    }

#if defined(_MSC_VER)
    __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    int RunUiCapture(const std::string& directory)
    {
#if defined(MPHREAD_AVALONIA)
        try
        {
            return MphRead::Mods::Launcher::Gui::UiCapture::Run(directory);
        }
        catch (const std::exception& ex)
        {
            WriteLine(std::string("[uishot] no launcher toolkit here: ") + ex.what());
            return 1;
        }
#else
        (void)directory;
        WriteLine("[uishot] this build has no Avalonia launcher");
        return 1;
#endif
    }

#if defined(_MSC_VER)
    __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    int RunUiBench(const std::vector<std::string>& args)
    {
#if defined(MPHREAD_AVALONIA_SHELL)
        try
        {
            using namespace MphRead::Mods::Launcher::Gui;
            UiBench::Slow = HasFlag(args, "uibenchslow");
            UiBench::AsAndroid = HasFlag(args, "uibenchandroid");
            UiBench::FreeFrames = HasFlag(args, "uibenchfree");
            UiBench::OnlySize = ValueAfter(args, "uibenchsize");
            UiBench::OnlyMove = ValueAfter(args, "uibenchonly");
            UiBench::Shot = ValueAfter(args, "uibenchshot");
            double parsed = 0;
            if (TryParseDoubleInvariant(ValueAfter(args, "uibenchscale"), parsed, false))
            {
                UiBench::ScaleOverride = parsed;
            }
            return UiBench::Run(ValueAfter(args, "uibench"));
        }
        catch (const std::exception& ex)
        {
            WriteLine(std::string("[uibench] no launcher toolkit here: ") + ex.what());
            return 1;
        }
#else
        (void)args;
        WriteLine("[uibench] this build has no launcher surface to measure");
        return 1;
#endif
    }

#if defined(_MSC_VER)
    __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    int RunUiDesigns(const std::string& directory)
    {
#if defined(MPHREAD_AVALONIA)
        try
        {
            return MphRead::Mods::Launcher::Gui::UiDesigns::Run(directory);
        }
        catch (const std::exception& ex)
        {
            WriteLine(std::string("[uidesign] no launcher toolkit here: ") + ex.what());
            return 1;
        }
#else
        (void)directory;
        WriteLine("[uidesign] this build has no Avalonia launcher");
        return 1;
#endif
    }

#if defined(_MSC_VER)
    __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    int RunShellCapture(const std::string& directory)
    {
#if defined(MPHREAD_SHELL)
        using namespace MphRead::Mods::Launcher::Gui;
        Shell::RequestShots(directory);
        if (!GuiLauncher::TryRun())
        {
            return 1;
        }
        const std::int32_t misses = Shell::ShotMisses();
        if (misses > 0)
        {
            WriteLine("[shellshot] " + std::to_string(misses) + " step(s) found nothing to press");
            return 1;
        }
        return 0;
#else
        (void)directory;
        WriteLine("[shellshot] this build has no launcher");
        return 1;
#endif
    }

#if defined(_MSC_VER)
    __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    int RunTapCheck()
    {
#if defined(MPHREAD_AVALONIA)
        return MphRead::Mods::Launcher::Gui::TapCheck::Run();
#else
        WriteLine("[tapcheck] this build has no launcher");
        return 1;
#endif
    }

    void GenerateThumbnails(const std::vector<std::string>& args, int width, int height)
    {
        using MphRead::Mods::ThumbnailBatch;
        using MphRead::Mods::ThumbnailGenerator;

        const bool force = HasFlag(args, "force");
        std::vector<std::string> rooms = force
            ? ThumbnailGenerator::MultiplayerRooms()
            : ThumbnailGenerator::MissingThumbnails();
        if (rooms.empty())
        {
            WriteLine("[thumbnails] all previews already present in "
                + ThumbnailGenerator::CacheDirectory());
            WriteLine("[thumbnails] pass -force to re-render them");
            return;
        }
        int jobs = ThumbnailBatch::DefaultParallelism();
        const std::optional<std::string> jobsValue = ValueAfter(args, "jobs");
        std::int32_t parsedJobs = 0;
        if (jobsValue.has_value() && Int32TryParseCurrentCulture(*jobsValue, parsedJobs))
        {
            jobs = parsedJobs;
        }
        WriteLine("[thumbnails] rendering " + std::to_string(rooms.size())
            + " preview(s) at " + std::to_string(width) + "x" + std::to_string(height)
            + ", " + std::to_string(jobs) + " at a time");
        WriteLine("[thumbnails] output: " + ThumbnailGenerator::CacheDirectory());
        const int written = ThumbnailBatch::Run(rooms, jobs, width, height);
        WriteLine("[thumbnails] done -- " + std::to_string(written) + "/"
            + std::to_string(rooms.size()) + " written");
    }
}

namespace MphRead::Mods
{
    bool ModEntry::TryHandleHeadless(const std::vector<std::string>& args)
    {
        if (const std::optional<std::string> contract = ValueAfter(args, "rhicontract"); contract.has_value())
        {
            try
            {
                // Relative to where the command was typed: startup has moved the
                // working directory to the installation by now.
                const std::string path = FullPathCombine(ConsoleSetup::LaunchDirectory(), *contract);
                std::ofstream file(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary | std::ios::trunc);
                file << ::MphRead::NativeRuntime::Rhi::SceneBackendContract();
                SetExitCode(file.good() ? 0 : 1);
            }
            catch (...)
            {
                SetExitCode(1);
            }
            return true;
        }
        if (::HasFlag(args, "vulkancheck"))
        {
            SetExitCode(::MphRead::NativeRuntime::Rhi::Vulkan::RunFoundationCheck());
            return true;
        }
        if (const auto directory = ValueAfter(args, "backdropparity"); directory.has_value())
        {
            SetExitCode(Diagnostics::RunBackdropParityCheck(
                FullPathCombine(ConsoleSetup::LaunchDirectory(), *directory), ::HasFlag(args, "backdropobserve")));
            return true;
        }
        if (::HasFlag(args, "presentconformance"))
        {
            SetExitCode(Diagnostics::RunPresentConformanceCheck());
            return true;
        }
        if (::HasFlag(args, "rhiconformance"))
        {
            SetExitCode(Diagnostics::RunRhiConformanceCheck());
            return true;
        }
        if (::HasFlag(args, "reflexcheck"))
        {
            SetExitCode(::MphRead::NativeRuntime::Rhi::Vulkan::RunPresentationCheck(false, true));
            return true;
        }
        if (::HasFlag(args, "vulkanpresentcheck"))
        {
            SetExitCode(::MphRead::NativeRuntime::Rhi::Vulkan::RunPresentationCheck());
            return true;
        }
        if (::HasFlag(args, "vulkanresourcecheck"))
        {
            SetExitCode(::MphRead::NativeRuntime::Rhi::Vulkan::RunResourceCheck());
            return true;
        }
        if (::HasFlag(args, "vulkanpresentfallbackcheck"))
        {
            SetExitCode(::MphRead::NativeRuntime::Rhi::Vulkan::RunPresentationCheck(true));
            return true;
        }
#if defined(MPHREAD_AVALONIA_SHELL)
        if (::HasFlag(args, "glfwpathcheck"))
        {
            SetExitCode(Diagnostics::GlfwPathCheck::Run());
            return true;
        }
        if (::HasFlag(args, "thumbnailwindowcheck"))
        {
            SetExitCode(Diagnostics::ThumbnailWindowCheck::Run(::HasFlag(args, "legacyglcheck")));
            return true;
        }
        if (::HasFlag(args, "windowcheck"))
        {
            SetExitCode(Diagnostics::LauncherWindowCheck::Run());
            return true;
        }
#endif
        if (::HasFlag(args, "smoketest"))
        {
            SetExitCode(Diagnostics::CompatibilityCheck::Run());
            return true;
        }

        InputSettings::Load();
        Launcher::LauncherPrefs::Load();
        if (::HasFlag(args, "debuglog"))
        {
            DebugLog::Force();
        }
        DebugLog::Attach();
        if (NativeRuntime::IsMacOS())
        {
            Diagnostics::PlatformDiagnostics::Start();
        }

        Update::Updater::Disabled(::HasFlag(args, "noupdate"));
        ApplyRenderOverrides(args);

        if (::HasFlag(args, "pointercheck"))
        {
            SetExitCode(Input::PointerCheck::Run());
            return true;
        }

        Input::AimAssist::AimAssistDebug::Enabled = ::HasFlag(args, "gamepadassistdebug");
        Input::AimAssist::AimAssistDebug::UnassistedArm = ::HasFlag(args, "gamepadassistbaseline");
        Input::AimAssist::AimAssistTelemetry::Configure(ValueAfter(args, "gamepadassisttelemetry"));

        if (::HasFlag(args, "gamepadcheck"))
        {
            SetExitCode(Input::GamepadChecks::Run(ValueAfter(args, "shots")));
            return true;
        }

        if (::HasFlag(args, "gamepad"))
        {
            double seconds = 15;
            const std::optional<std::string> given = ValueAfter(args, "seconds");
            double parsed = 0;
            if (given.has_value() && TryParseDoubleCurrent(*given, parsed) && parsed > 0)
            {
                seconds = parsed;
            }
            SetExitCode(Input::GamepadProbe::Run(seconds, ::HasFlag(args, "verbose")));
            return true;
        }

        if (::HasFlag(args, "frametimingcheck"))
        {
            SetExitCode(Render::FrameTimingCheck::Run());
            return true;
        }

        const int applyAt = IndexOfFlag(args, Update::DesktopUpdate::ApplyFlag);
        if (applyAt >= 0 && static_cast<std::size_t>(applyAt + 2) < args.size())
        {
            std::vector<std::string> relaunch;
            int separator = -1;
            for (std::size_t i = static_cast<std::size_t>(applyAt + 3); i < args.size(); ++i)
            {
                if (args[i] == Update::DesktopUpdate::RelaunchSeparator)
                {
                    separator = static_cast<int>(i);
                    break;
                }
            }
            for (int i = separator + 1; separator >= 0 && static_cast<std::size_t>(i) < args.size(); ++i)
            {
                relaunch.push_back(args[static_cast<std::size_t>(i)]);
            }
            std::int32_t waitFor = -1;
            if (!Int32TryParseCurrentCulture(args[static_cast<std::size_t>(applyAt + 2)], waitFor))
            {
                waitFor = -1;
            }
            SetExitCode(Update::DesktopUpdate::Apply(
                args[static_cast<std::size_t>(applyAt + 1)], waitFor, relaunch));
            return true;
        }

        if (!::HasFlag(args, "spireposecheck") && !::HasFlag(args, "formcheck"))
        {
            Update::DesktopUpdate::Clean();
        }
        Update::UpdateInstall::UseDesktopIfPossible();

        const std::optional<std::string> netLag = ValueAfter(args, "netlag");
        if (netLag.has_value() && !Network::NetLag::Configure(*netLag))
        {
            WriteLine("[net] -netlag " + *netLag
                + " is not a number of milliseconds (try -netlag 200 or -netlag 200:40)");
            return true;
        }
        const std::optional<std::string> netLoss = ValueAfter(args, "netloss");
        if (netLoss.has_value() && !Network::NetLag::ConfigureLoss(*netLoss))
        {
            WriteLine("[net] -netloss " + *netLoss + " is not a percentage");
            return true;
        }
        const std::array<std::pair<std::string_view,
            bool (*)(const std::optional<std::string>&)>, 4> netLagOptions{{
            {"netjitter", &Network::NetLag::ConfigureJitter},
            {"netseed", &Network::NetLag::ConfigureSeed},
            {"netreorder", &Network::NetLag::ConfigureReorder},
            {"netduplicate", &Network::NetLag::ConfigureDuplicate}
        }};
        for (const auto& [name, configure] : netLagOptions)
        {
            const std::optional<std::string> value = ValueAfter(args, name);
            if (value.has_value() && !configure(value))
            {
                WriteLine("[net] invalid -" + std::string(name) + " value: " + *value);
                return true;
            }
        }
        if (Network::NetLag::Active())
        {
            const std::optional<std::string> description = Network::NetLag::Describe();
            WriteLine("[net] simulating a bad line: " + description.value_or(""));
        }
        if (::HasFlag(args, "nounlagged"))
        {
            Network::NetUnlagged::SetEnabled(false);
            WriteLine("[net] lag compensation off: shots resolve against the present");
        }
        if (::HasFlag(args, "snapshotpuppets"))
        {
            Network::NetHooks::SnapshotOwnsPuppets(true);
            WriteLine("[net] puppet positions on this client come from the authority's snapshot, not from relayed intents");
        }
        if (::HasFlag(args, "clientpin"))
        {
            Network::NetHooks::PinPuppetsOnClients(true);
            WriteLine("[net] puppets are pinned to their owner's reported position on clients as well as on the authority");
        }
        if (::HasFlag(args, "pressage"))
        {
            Network::NetUnlagged::PressAgeEnabled(true);
            WriteLine("[net] recovered trigger pulls are rewound by their own age as well as by their packet's ack");
        }
        const std::optional<std::string> rig = ValueAfter(args, "hitrig");
        if (rig.has_value())
        {
            if (Network::HitRig::Configure(rig))
            {
                WriteLine("[net] hit rig: " + Network::ToString(Network::HitRig::Mode()));
            }
            else
            {
                WriteLine("[net] -hitrig " + *rig + " refused: jump, sniper or duel");
            }
        }
        const std::optional<std::string> maxRewind = ValueAfter(args, "maxrewind");
        if (maxRewind.has_value())
        {
            if (Network::NetUnlagged::ConfigureMaxRewind(maxRewind))
            {
                const std::int32_t frames = Network::NetUnlagged::MaxRewindFrames();
                WriteLine("[net] rewind ceiling " + std::to_string(frames) + " frames ("
                    + std::to_string(frames * 1000 / 60) + " ms)");
            }
            else
            {
                WriteLine("[net] -maxrewind " + *maxRewind + " refused: 1 to "
                    + std::to_string(Network::NetUnlagged::MaxRewindCeiling)
                    + " frames, and the history cannot serve more");
            }
        }
        if (::HasFlag(args, "nohitprediction"))
        {
            Network::NetHitPrediction::SetEnabled(false);
            WriteLine("[net] hit prediction off: a client's hits land when the authority says so");
        }
        if (::HasFlag(args, "nohitmarker"))
        {
            Network::NetHitPrediction::SetMarkerEnabled(false);
            WriteLine("[hud] hit marker off");
        }
        if (::HasFlag(args, "deathprediction") || ::HasFlag(args, "nodeathprediction"))
        {
            WriteLine("[net] remote death waits for authority; self-death remains predicted");
        }
        if (::HasFlag(args, "noclaims"))
        {
            Network::NetHitClaims::Enabled(false);
            WriteLine("[net] hit claims off: a shot counts only where the authority finds it itself");
        }
        if (::HasFlag(args, "nointerp"))
        {
            Network::NetSmoothing::Enabled(false);
            WriteLine("[net] puppet interpolation off: remote players move when their snapshots arrive");
        }
        if (::HasFlag(args, "relayedpuppets"))
        {
            Network::NetHooks::SnapshotOwnsPuppets(false);
            Network::NetSmoothing::Enabled(false);
            WriteLine("[net] puppet positions on this client come from relayed intents, not from the snapshot");
        }
        if (::HasFlag(args, "credits"))
        {
            Credits::Print();
            return true;
        }

        const std::optional<std::string> mapDir = ValueAfter(args, "mapdir");
        if (mapDir.has_value())
        {
            MapGen::CustomRooms::MapDirectory(
                FullPathCombine(ConsoleSetup::LaunchDirectory(), *mapDir));
        }

#if defined(MPHREAD_SHELL)
        if (const std::optional<std::string> golden = ValueAfter(args, "goldencapture");
            golden.has_value())
        {
            const std::string directory = ValueAfter(args, "goldendir").value_or(
                NativeRuntime::PathCombine(
                    NativeRuntime::EnvironmentCurrentDirectory(), "golden-rhi"));
            SetExitCode(Render::GoldenCapture::Run(*golden, directory));
            return true;
        }
#endif

        if (::HasFlag(args, "mapbundle"))
        {
            const std::optional<std::string> which = ValueAfter(args, "mapbundle");
            const std::optional<std::string> outPath = ValueAfter(args, "out");
            int cooked = 0;
            int failed = 0;
            for (const auto& definition : MapGen::CustomRooms::Definitions())
            {
                if (which.has_value() && !StringEqualsOrdinalIgnoreCase(*which, RequireReference(definition).Name())
                    && !StringEqualsOrdinalIgnoreCase(*which, "all"))
                {
                    continue;
                }
                if (!RequireReference(definition).SourcePath().has_value() || RequireReference(definition).BundlePath().has_value()
                    || RequireReference(definition).Import() == nullptr)
                {
                    continue;
                }
                try
                {
                    (void)MapGen::MapBundle::Cook(
                        definition.get(), *RequireReference(definition).SourcePath(), outPath);
                    cooked = ::MphRead::NativeRuntime::UncheckedAdd(cooked, 1);
                }
                catch (...)
                {
                    const std::exception_ptr error = std::current_exception();
                    ::MphRead::NativeRuntime::ConsoleWriteLine(
                        RequireReference(definition).Name() + ": "
                        + ::MphRead::NativeRuntime::ExceptionMessage(error));
                    failed = ::MphRead::NativeRuntime::UncheckedAdd(failed, 1);
                }
            }
            if (cooked == 0 && failed == 0)
            {
                ::MphRead::NativeRuntime::ConsoleWriteLine(
                    "No map to bundle. A bundle is cooked from a recipe and the level it converts; put both in "
                    + MapGen::CustomRooms::MapDirectory() + ".");
            }
            SetExitCode(failed == 0 ? 0 : 1);
            return true;
        }

        if (::HasFlag(args, "update"))
        {
            Update::Updater::Disabled(false);
            const std::optional<Update::UpdateInfo> update = Update::Updater::Check();
            if (!update.has_value())
            {
                WriteLine("[update] " + Update::UpdateCheck::LastReason().value_or(""));
                return true;
            }
            WriteLine("[update] " + Update::Updater::Describe(*update));
            WriteLine("[update] " + update->PageUrl.Get().value_or(""));
            (void)Update::Updater::OpenPage(*update);
            return true;
        }

        const std::optional<std::string> uiShot = ValueAfter(args, "uishot");
        if (uiShot.has_value())
        {
            SetExitCode(RunUiCapture(*uiShot));
            return true;
        }

        if (::HasFlag(args, "uibench"))
        {
            SetExitCode(RunUiBench(args));
            return true;
        }

        const std::optional<std::string> uiDesign = ValueAfter(args, "uidesign");
        if (uiDesign.has_value())
        {
            SetExitCode(RunUiDesigns(*uiDesign));
            return true;
        }

        const std::optional<std::string> shellShot = ValueAfter(args, "shellshot");
        if (shellShot.has_value())
        {
            SetExitCode(RunShellCapture(*shellShot));
            return true;
        }

        if (::HasFlag(args, "tapcheck"))
        {
            SetExitCode(RunTapCheck());
            return true;
        }

        if (::HasFlag(args, "fullscreen") || ::HasFlag(args, "borderless"))
        {
            WindowMode::Startup(WindowStartMode::BorderlessFullscreen);
        }
        else if (::HasFlag(args, "windowed"))
        {
            WindowMode::Startup(WindowStartMode::Windowed);
        }

        bool doubleClicked = args.empty();
#if !defined(_WIN32) && !defined(__APPLE__)
        doubleClicked = false;
#endif
#if defined(MPHREAD_SERVER)
        doubleClicked = false;
#endif
        if ((::HasFlag(args, "launcher") || doubleClicked) && !::HasFlag(args, "menu"))
        {
#if defined(MPHREAD_SHELL) || defined(MPHREAD_AVALONIA)
            if (!::HasFlag(args, "text") && Launcher::Gui::GuiLauncher::TryRun())
            {
                return true;
            }
#if defined(_WIN32)
            ConsoleWindow::Show();
#endif
#endif
            Launcher::TextLauncher::Run();
            return true;
        }

#if defined(MPHREAD_SERVER)
        if (args.empty())
        {
            ServerUsage();
            return true;
        }
#endif

        if (::HasFlag(args, "masterserver") || ::HasFlag(args, "server")
            || ::HasFlag(args, "dedicated"))
        {
            Update::ServerUpdate::Enabled(!::HasFlag(args, "noautoupdate"));
            std::vector<std::string> commandLine = GetCommandLineArguments();
            std::vector<std::string> typed;
            if (commandLine.size() > 1)
            {
                typed.assign(commandLine.begin() + 1, commandLine.end());
            }
            if (Update::ServerUpdate::AtStartup(typed))
            {
                return true;
            }
        }

        if (::HasFlag(args, "masterserver"))
        {
            int masterPort = Network::NetMasterConfig::DefaultPort;
            std::optional<std::string> masterPortValue = ValueAfter(args, "port");
            if (!masterPortValue.has_value())
            {
                masterPortValue = ValueAfter(args, "masterport");
            }
            std::int32_t parsedMasterPort = 0;
            if (masterPortValue.has_value() && Int32TryParseCurrentCulture(*masterPortValue, parsedMasterPort))
            {
                masterPort = parsedMasterPort;
            }
            Network::MasterServer master(masterPort);
            ShutdownSignals signals;

            const std::string hostPorts = ValueAfter(args, "hostports").value_or("27900-27919");
            if (!StringEqualsOrdinalIgnoreCase(hostPorts, "none"))
            {
                const std::size_t dash = hostPorts.find('-');
                std::string firstText;
                std::string lastText;
                if (dash == std::string::npos)
                {
                    firstText = hostPorts;
                }
                else
                {
                    firstText = hostPorts.substr(0, dash);
                    lastText = hostPorts.substr(dash + 1);
                }
                std::int32_t first = 0;
                std::int32_t last = 0;
                if (dash != std::string::npos && Int32TryParseCurrentCulture(firstText, first)
                    && Int32TryParseCurrentCulture(lastText, last) && first > 0 && last >= first)
                {
                    master.SetHostPorts(first, last);
                }
                else
                {
                    WriteLine("[master] ignoring -hostports " + hostPorts
                        + " (expected e.g. 27900-27919, or none)");
                }
            }

            std::optional<std::string> publicHost = ValueAfter(args, "public");
            if (!publicHost.has_value())
            {
                publicHost = ValueAfter(args, "publicaddress");
            }
            if (publicHost.has_value())
            {
                master.SetPublicAddress(*publicHost);
            }

            std::stop_source cancel;
            signals.OnShutdown([&cancel, &master]()
            {
                cancel.request_stop();
                master.Stop();
            });
            master.Run(cancel.get_token());
            return true;
        }

        if (::HasFlag(args, "servers"))
        {
            ListServers(ValueAfter(args, "master").value_or(
                std::string(Network::NetMasterConfig::DefaultHost)),
                ValueAfter(args, "masterport"));
            return true;
        }

        if (::HasFlag(args, "hosts"))
        {
            const std::string askHost = ValueAfter(args, "master").value_or(
                std::string(Network::NetMasterConfig::DefaultHost));
            std::int32_t askPort = ParseMasterPort(args);
            WriteLine("[hosts] asking " + askHost + ":" + std::to_string(askPort)
                + " and everyone it names");

            struct HostsQueryState
            {
                std::vector<Network::HostCandidate> Candidates;
                std::mutex Mutex;
                std::condition_variable Finished;
                bool Done = false;
            };
            const auto state = std::make_shared<HostsQueryState>();
            Network::NetMasterClient::FindHosts(askHost, askPort,
                [state](Network::HostCandidate candidate)
                {
                    std::lock_guard lock(state->Mutex);
                    const std::size_t before = state->Candidates.size();
                    Network::NetMasterClient::Merge(state->Candidates, candidate);
                    if (state->Candidates.size() > before)
                    {
                        const std::string endpoint = candidate.Host + ":"
                            + std::to_string(candidate.Port);
                        std::ostringstream row;
                        row << "  " << std::left << std::setw(28) << candidate.Label << ' '
                            << std::setw(26) << endpoint << ' ' << candidate.Describe();
                        WriteLine(row.str());
                    }
                },
                [state]()
                {
                    {
                        std::lock_guard lock(state->Mutex);
                        state->Done = true;
                    }
                    state->Finished.notify_one();
                });

            {
                std::unique_lock lock(state->Mutex);
                (void)state->Finished.wait_for(lock, std::chrono::seconds(20),
                    [&state]() { return state->Done; });
                std::int32_t usable = 0;
                for (const Network::HostCandidate& candidate : state->Candidates)
                {
                    if (candidate.WillHost())
                    {
                        ++usable;
                    }
                }
                WriteLine("[hosts] " + std::to_string(usable) + " of "
                    + std::to_string(state->Candidates.size()) + " can run a match");
            }
            return true;
        }

        if (!::HasFlag(args, "server") && !::HasFlag(args, "dedicated"))
        {
            return false;
        }

        int port = Network::NetConfig::DefaultPort;
        const std::optional<std::string> portValue = ValueAfter(args, "port");
        std::int32_t parsedPort = 0;
        if (portValue.has_value() && Int32TryParseCurrentCulture(*portValue, parsedPort))
        {
            port = parsedPort;
        }
        int maxPlayers = 4;
        const std::optional<std::string> playersValue = ValueAfter(args, "players");
        std::int32_t parsedPlayers = 0;
        if (playersValue.has_value() && Int32TryParseCurrentCulture(*playersValue, parsedPlayers))
        {
            maxPlayers = parsedPlayers;
        }
        std::string rotationPath;
        if (const std::optional<std::string> value = ValueAfter(args, "rotation"); value.has_value())
        {
            rotationPath = *value;
        }
        else
        {
            rotationPath = PathCombine(Mods::Platform::AppPaths::UserDataDirectory(), "maprotation.txt");
        }
        std::shared_ptr<Network::MapRotation> rotation = Network::MapRotation::LoadOrCreate(rotationPath);
        Network::DedicatedServer server(port, maxPlayers, rotation);
        std::string serverName;
        if (const std::optional<std::string> value = ValueAfter(args, "servername"); value.has_value())
        {
            serverName = *value;
        }
        else if (const std::optional<std::string> value = ValueAfter(args, "name"); value.has_value())
        {
            serverName = *value;
        }
        else
        {
            serverName = MachineName();
        }
        server.ServerName(serverName);
        server.FriendlyFire(::HasFlag(args, "friendlyfire"));
        server.ShadowFreeze(!::HasFlag(args, "noshadowfreeze"));
        server.AffinityWeapons(::HasFlag(args, "affinityweapons"));
        server.AllowMapVotes(!::HasFlag(args, "novote"));
        server.AutoUpdate(true);
        // Whether this server will also open *extra* matches, on ports of its
        // own, for players who ask. Off unless an admin says a range.
        if (const std::optional<std::string> serverHostPorts = ValueAfter(args, "hostports");
            serverHostPorts.has_value() && !NativeRuntime::StringEqualsOrdinalIgnoreCase(*serverHostPorts, "none"))
        {
            const std::vector<std::string> halves = NativeRuntime::StringSplit(*serverHostPorts, '-');
            std::int32_t hostFirst = 0;
            std::int32_t hostLast = 0;
            if (halves.size() == 2 && Int32TryParseCurrentCulture(halves[0], hostFirst)
                && Int32TryParseCurrentCulture(halves[1], hostLast) && hostLast >= hostFirst)
            {
                server.Hosts().SetPorts(hostFirst, hostLast);
            }
            else
            {
                WriteLine("[server] ignoring -hostports " + *serverHostPorts + " (expected FIRST-LAST)");
            }
        }
        // -simulate and -authority are accepted and ignored: a server always
        // runs the match itself now.
        if (::HasFlag(args, "simulate") || ::HasFlag(args, "authority"))
        {
            WriteLine("[net] -simulate is the default now and does "
                "nothing; a server always runs the match itself");
        }

        if (!::HasFlag(args, "nomaster") && !::HasFlag(args, "unlisted"))
        {
            const std::string masterHost = ValueAfter(args, "master").value_or(
                std::string(Network::NetMasterConfig::DefaultHost));
            int reportPort = Network::NetMasterConfig::DefaultPort;
            const std::optional<std::string> reportPortValue = ValueAfter(args, "masterport");
            std::int32_t parsedReportPort = 0;
            if (reportPortValue.has_value() && Int32TryParseCurrentCulture(*reportPortValue, parsedReportPort))
            {
                reportPort = parsedReportPort;
            }
            server.Reporter(std::make_unique<Network::MasterReporter>(masterHost, reportPort));
            WriteLine("[server] listing on " + masterHost + ":" + std::to_string(reportPort)
                + " as \"" + server.ServerName() + "\" (-nomaster to stay private)");
        }

        std::stop_source cancel;
        ShutdownSignals signals;
        signals.OnShutdown([&cancel, &server]()
        {
            cancel.request_stop();
            server.Stop();
        });
        try
        {
            server.Run(cancel.get_token());
        }
        catch (const ProgramException& ex)
        {
            // The reason and what to do about it are already on the log.
            WriteLine(std::string("[server] ") + ex.what());
            std::exit(1);
        }
        return true;
    }

    bool ModEntry::TryHandle(const std::vector<std::string>& args)
    {
        const auto [width, height] = ParseSize(args);

        if (!::HasFlag(args, "mapgen"))
        {
            MapGen::CustomRooms::GenerateMissing();
        }

        if (::HasFlag(args, "nohelmet"))
        {
            Features::HelmetOpacity(0);
            Features::VisorOpacity(0);
        }
        if (::HasFlag(args, "uinativeres"))
        {
#if defined(MPHREAD_AVALONIA_SHELL)
            Launcher::Gui::UiSurface::NativeRaster(true);
#endif
        }
        if (::HasFlag(args, "netdebug"))
        {
            Network::NetDiagnostics::SetEnabled(true);
            Network::MapAudit::Diagnostic(true);
        }

        if (::HasFlag(args, "mechanics"))
        {
            Network::MechanicsDump::Run();
            return true;
        }

        if (::HasFlag(args, "gamepad"))
        {
            double seconds = 15;
            const std::optional<std::string> given = ValueAfter(args, "seconds");
            double parsed = 0;
            if (given.has_value() && TryParseDoubleCurrent(*given, parsed) && parsed > 0)
            {
                seconds = parsed;
            }
            SetExitCode(Input::GamepadProbe::Run(seconds, ::HasFlag(args, "verbose")));
            return true;
        }

        if (::HasFlag(args, "resourceaudit"))
        {
            SetExitCode(Multiplayer::ResourceAudit::Run());
            return true;
        }

        if (const std::optional<std::string> healthRoom = ValueAfter(args, "healthsimtest");
            healthRoom.has_value())
        {
            SetExitCode(Network::HealthSimulationTest::Run(*healthRoom));
            return true;
        }

        if (::HasFlag(args, "rooms"))
        {
            for (const std::string& room : ThumbnailGenerator::MultiplayerRooms())
            {
                WriteLine(room);
            }
            return true;
        }

        const std::optional<std::string> dpsTest = ValueAfter(args, "dpstest");
        if (dpsTest.has_value())
        {
            Hunter dpsHunter = Hunter::Sylux;
            (void)TryParseHunter(ValueAfter(args, "hunter"), dpsHunter);
            BeamType dpsBeam = BeamType::ShockCoil;
            (void)TryParseBeam(ValueAfter(args, "weapon"), dpsBeam);
            double dpsSeconds = 10;
            double parsedSeconds = 0;
            if (TryParseDoubleInvariant(ValueAfter(args, "seconds"), parsedSeconds))
            {
                dpsSeconds = parsedSeconds;
            }
            float dpsDistance = 2.2f;
            float parsedDistance = 0;
            if (TryParseFloatInvariant(ValueAfter(args, "distance"), parsedDistance))
            {
                dpsDistance = parsedDistance;
            }
            SetExitCode(Network::WeaponDps::Run(*dpsTest, dpsHunter, dpsBeam,
                dpsSeconds, dpsDistance, ::HasFlag(args, "bombs")));
            return true;
        }

        if (::HasFlag(args, "mapgen"))
        {
            const std::optional<std::string> only = ValueAfter(args, "mapgen");
            const bool force = ::HasFlag(args, "force");
            (void)force;
            int count = 0;
            int failed = 0;
            for (const auto& definition : MapGen::CustomRooms::Definitions())
            {
                if (only.has_value() && !StringEqualsOrdinalIgnoreCase(*only, RequireReference(definition).Name())
                    && !StringEqualsOrdinalIgnoreCase(*only, "all"))
                {
                    continue;
                }
                try
                {
                    MapGen::MapPacker::Generate(definition.get(),
                        MapGen::CustomRooms::ArchiveDirectory(definition.get()),
                        MapGen::CustomRooms::EntityDirectory(),
                        MapGen::CustomRooms::NodeDirectory(), true /* verbose */);
                    count = ::MphRead::NativeRuntime::UncheckedAdd(count, 1);
                }
                catch (...)
                {
                    const std::exception_ptr error = std::current_exception();
                    ::MphRead::NativeRuntime::ConsoleWriteLine(
                        RequireReference(definition).Name() + ": "
                        + ::MphRead::NativeRuntime::ExceptionMessage(error));
                    failed = ::MphRead::NativeRuntime::UncheckedAdd(failed, 1);
                }
            }
            if (count == 0 && failed == 0)
            {
                ::MphRead::NativeRuntime::ConsoleWriteLine("No maps to generate. Put a map JSON in "
                    + MapGen::CustomRooms::MapDirectory() + ".");
            }
            SetExitCode(failed == 0 ? 0 : 1);
            return true;
        }

        const std::optional<std::string> q3Maps = ValueAfter(args, "q3maps");
        if (q3Maps.has_value())
        {
            try
            {
                for (const std::string& map : MapGen::Q3Bsp::ListMaps(*q3Maps))
                {
                    WriteLine(map);
                }
                SetExitCode(0);
            }
            catch (const std::exception& ex)
            {
                WriteLine("Could not read " + *q3Maps + ": " + ex.what());
                SetExitCode(1);
            }
            return true;
        }

        const std::optional<std::string> q3Convert = ValueAfter(args, "q3convert");
        if (q3Convert.has_value())
        {
            std::optional<float> scale;
            float parsedScale = 0;
            if (TryParseFloatInvariant(ValueAfter(args, "scale"), parsedScale,
                false /* NumberStyles.Float has no thousands */) && parsedScale > 0)
            {
                scale = parsedScale;
            }
            int textureSize = MapGen::MapTextureBake::DefaultSize;
            const std::optional<std::string> texSizeValue = ValueAfter(args, "texsize");
            std::int32_t parsedTextureSize = 0;
            if (texSizeValue.has_value() && Int32TryParseCurrentCulture(*texSizeValue, parsedTextureSize)
                && parsedTextureSize >= 8 && parsedTextureSize <= 256)
            {
                textureSize = parsedTextureSize;
            }
            try
            {
                SetExitCode(MapGen::Q3Convert::Run(*q3Convert,
                    ValueAfter(args, "map"), ValueAfter(args, "name"),
                    ValueAfter(args, "out"), ::HasFlag(args, "noclip"), ::HasFlag(args, "noitems"), scale,
                    textureSize));
            }
            catch (...)
            {
                WriteLine("Could not convert " + *q3Convert + ": "
                    + ::MphRead::NativeRuntime::ExceptionMessage(std::current_exception()));
                SetExitCode(1);
            }
            return true;
        }

        const std::optional<std::string> q3Shaders = ValueAfter(args, "q3shaders");
        if (q3Shaders.has_value())
        {
            SetExitCode(MapGen::MapReport::ListShaders(*q3Shaders, ValueAfter(args, "map")));
            return true;
        }

        const std::optional<std::string> mapCheck = ValueAfter(args, "mapcheck");
        if (mapCheck.has_value())
        {
            SetExitCode(MapGen::MapCheck::Run(*mapCheck));
            return true;
        }

        const std::optional<std::string> altProbe = ValueAfter(args, "altprobe");
        if (altProbe.has_value())
        {
            const std::vector<std::string> at = NativeRuntime::StringSplit(
                ValueAfter(args, "at").value_or(""), ',');
            float atX = 0.0F;
            float atY = 0.0F;
            float atZ = 0.0F;
            if (at.size() != 3
                || !TryParseFloatInvariant(std::optional<std::string>(at[0]), atX, false)
                || !TryParseFloatInvariant(std::optional<std::string>(at[1]), atY, false)
                || !TryParseFloatInvariant(std::optional<std::string>(at[2]), atZ, false))
            {
                WriteLine("-altprobe needs -at X,Y,Z");
                SetExitCode(1);
                return true;
            }
            const std::optional<std::string> traceDelay = ValueAfter(args, "delay");
            std::int32_t parsedDelay = -1;
            MapGen::AltFormProbe::TraceDelay(
                traceDelay.has_value() && Int32TryParseCurrentCulture(*traceDelay, parsedDelay)
                    && parsedDelay >= 0
                    ? std::optional<std::int32_t>(parsedDelay)
                    : std::nullopt);
            SetExitCode(MapGen::AltFormProbe::Run(*altProbe,
                OpenTK::Mathematics::Vector3(atX, atY, atZ), ParseHunter(args)));
            return true;
        }

        const std::optional<std::string> mapItems = ValueAfter(args, "mapitems");
        if (mapItems.has_value())
        {
            std::optional<float> itemScale;
            float parsedItemScale = 0.0F;
            if (TryParseFloatInvariant(ValueAfter(args, "scale"), parsedItemScale, false)
                && parsedItemScale > 0.0F)
            {
                itemScale = parsedItemScale;
            }
            SetExitCode(MapGen::MapReport::ListItems(
                *mapItems, ValueAfter(args, "map"), itemScale));
            return true;
        }

        const std::optional<std::string> mapMaterials = ValueAfter(args, "mapmaterials");
        if (mapMaterials.has_value())
        {
            SetExitCode(MapGen::MapReport::ListMaterials(*mapMaterials));
            return true;
        }

        const std::optional<std::string> spirePoseCheck = ValueAfter(args, "spireposecheck");
        if (spirePoseCheck.has_value())
        {
            SetExitCode(Network::SpireAltPoseCheck::Run(*spirePoseCheck));
            return true;
        }

        const std::optional<std::string> simCheck = ValueAfter(args, "simcheck");
        if (simCheck.has_value())
        {
            int players = 8;
            const std::optional<std::string> playersValue = ValueAfter(args, "players");
            std::int32_t parsedPlayers = 0;
            if (playersValue.has_value() && Int32TryParseCurrentCulture(*playersValue, parsedPlayers))
            {
                players = parsedPlayers;
            }
            double seconds = 10;
            double parsedSeconds = 0;
            if (TryParseDoubleInvariant(ValueAfter(args, "seconds"), parsedSeconds))
            {
                seconds = parsedSeconds;
            }
            GameMode mode = GameMode::Battle;
            (void)TryParseGameMode(ValueAfter(args, "mode"), mode);
            SetExitCode(Network::ServerSimCheck::Run(*simCheck, players, seconds, mode, HasFlag(args, "formcheck")));
            return true;
        }

        if (::HasFlag(args, "gpulifetime"))
        {
            std::string room = "TEST ARENA";
            const std::optional<std::string> named = ValueAfter(args, "gpulifetime");
            if (named.has_value() && !named->empty() && named->front() != '-')
            {
                room = *named;
            }
            std::int32_t cycles = 5;
            std::int32_t frames = 90;
            std::int32_t parsed = 0;
            if (const auto value = ValueAfter(args, "cycles"); value.has_value()
                && Int32TryParseCurrentCulture(*value, parsed) && parsed > 0)
            {
                cycles = parsed;
            }
            if (const auto value = ValueAfter(args, "frames"); value.has_value()
                && Int32TryParseCurrentCulture(*value, parsed) && parsed > 0)
            {
                frames = parsed;
            }
            SetExitCode(Diagnostics::GpuLifetimeCheck::Run(room, cycles, frames));
            return true;
        }

        const std::optional<std::string> mapTest = ValueAfter(args, "maptest");
        if (mapTest.has_value())
        {
            int players = 8;
            const std::optional<std::string> playersValue = ValueAfter(args, "players");
            std::int32_t parsedPlayers = 0;
            if (playersValue.has_value() && Int32TryParseCurrentCulture(*playersValue, parsedPlayers))
            {
                players = parsedPlayers;
            }
            double seconds = 10;
            double parsedSeconds = 0;
            if (TryParseDoubleInvariant(ValueAfter(args, "seconds"), parsedSeconds))
            {
                seconds = parsedSeconds;
            }
            GameMode mode = GameMode::Battle;
            (void)TryParseGameMode(ValueAfter(args, "mode"), mode);
            Network::MapAudit::ShowWindow(::HasFlag(args, "hudshots"));
            Network::MapAudit::TeamProbe(::HasFlag(args, "teamprobe"));
            if (ValueAfter(args, "hunter").has_value())
            {
                Network::MapAudit::MainHunter(ParseHunter(args));
            }
            else
            {
                Network::MapAudit::MainHunter(std::nullopt);
            }
            const std::optional<std::string> drawRateValue = ValueAfter(args, "drawrate");
            std::int32_t drawRate = 0;
            if (drawRateValue.has_value() && Int32TryParseCurrentCulture(*drawRateValue, drawRate)
                && drawRate > 0)
            {
                Network::MapAudit::DrawRate(drawRate);
            }
            const std::optional<std::string> sizeValue = ValueAfter(args, "size");
            if (sizeValue.has_value())
            {
                const std::string lowered = ::MphRead::NativeRuntime::ToLowerInvariant(*sizeValue);
                std::vector<std::string_view> split;
                std::size_t start = 0;
                for (std::size_t i = 0; i <= lowered.size(); ++i)
                {
                    if (i == lowered.size() || lowered[i] == 'x')
                    {
                        split.emplace_back(lowered.data() + start, i - start);
                        start = i + 1;
                    }
                }
                std::int32_t auditWidth = 0;
                std::int32_t auditHeight = 0;
                if (split.size() == 2 && Int32TryParseCurrentCulture(split[0], auditWidth)
                    && Int32TryParseCurrentCulture(split[1], auditHeight)
                    && auditWidth > 0 && auditHeight > 0)
                {
                    Network::MapAudit::WindowSize(OpenTK::Mathematics::Vector2i(auditWidth, auditHeight));
                }
            }
            SetExitCode(Network::MapAudit::Run(*mapTest, players, seconds, mode,
                ::HasFlag(args, "bots"), ValueAfter(args, "shots"),
                ::HasFlag(args, "renderprobe"), ::HasFlag(args, "allnodes"),
                ::HasFlag(args, "itemshots")));
            return true;
        }

        if (::HasFlag(args, "installserver"))
        {
            const std::string serverRid = Update::UpdateCheck::ServerRid();
            WriteLine("[server] this platform takes the \"" + serverRid + "\" package");
            if (!Network::LocalServer::CanInstall())
            {
                WriteLine("[server] no server package is published for it");
                SetExitCode(1);
                return true;
            }
            int lastPercent = -1;
            const bool installed = Network::LocalServer::Install(
                [&lastPercent](float fraction)
                {
                    const int percent = static_cast<int>(fraction * 100.0F);
                    if (percent >= lastPercent + 10)
                    {
                        lastPercent = percent;
                        WriteLine("[server] " + std::to_string(percent) + "%");
                    }
                });
            if (!installed)
            {
                WriteLine("[server] " + Network::LocalServer::LastError().value_or(""));
                SetExitCode(1);
                return true;
            }
            WriteLine("[server] installed " + Network::LocalServer::InstalledTag()
                + " into " + Network::LocalServer::Directory());
            return true;
        }

        const std::optional<std::string> hostLocal = ValueAfter(args, "hostlocal");
        if (hostLocal.has_value())
        {
            GameMode localMode = GameMode::Battle;
            (void)TryParseGameMode(ValueAfter(args, "mode"), localMode);
            std::vector<std::pair<std::string, GameMode>> localMaps;
            for (const std::string& entry : NativeRuntime::StringSplit(*hostLocal, ',', true, true))
            {
                localMaps.emplace_back(entry, localMode);
            }

            const std::optional<Network::ServerBinary> binary = Network::LocalServer::Available();
            if (!binary.has_value())
            {
                WriteLine("[hostlocal] nothing on this machine can run a server");
            }
            else
            {
                WriteLine("[hostlocal] using " + binary->Describe() + " (" + binary->Executable + ")");
            }

            double holdSeconds = 8.0;
            double parsedHold = 0.0;
            if (TryParseDoubleInvariant(ValueAfter(args, "seconds"), parsedHold))
            {
                holdSeconds = parsedHold;
            }
            const std::string serverName = ValueAfter(args, "servername").value_or("Local test server");
            const std::string masterHost = ValueAfter(args, "master").value_or(
                std::string(Network::NetMasterConfig::DefaultHost));
            const std::int32_t localPort = Network::LocalServer::Start(serverName, localMaps,
                Entities::PlayerEntity::SlotCapacity, 7.0F * 60.0F, 7,
                masterHost, ParseMasterPort(args), !::HasFlag(args, "nomaster"));
            if (localPort < 0)
            {
                WriteLine("[hostlocal] " + Network::LocalServer::LastError().value_or(""));
                SetExitCode(1);
                return true;
            }
            WriteLine("[hostlocal] listening on 127.0.0.1:" + std::to_string(localPort));
            const Network::ServerStatus localStatus = Network::NetStatus::Query(
                "127.0.0.1", localPort, false);
            WriteLine("[hostlocal] it answers: " + localStatus.RoomKey + " ("
                + Network::NetStatus::ModeName(localStatus.Mode) + "), "
                + std::to_string(localStatus.Players) + "/"
                + std::to_string(localStatus.MaxPlayers) + " players");
            NativeRuntime::ThreadSleep(static_cast<std::int32_t>(holdSeconds * 1000.0));
            Network::LocalServer::Stop();
            WriteLine("[hostlocal] stopped");
            return true;
        }

        const std::optional<std::string> hostGame = ValueAfter(args, "hostgame");
        if (hostGame.has_value())
        {
            const std::string masterHost = ValueAfter(args, "master").value_or(
                std::string(Network::NetMasterConfig::DefaultHost));
            int masterPort = Network::NetMasterConfig::DefaultPort;
            const std::optional<std::string> masterPortValue = ValueAfter(args, "masterport");
            std::int32_t parsedMasterPort = 0;
            if (masterPortValue.has_value() && Int32TryParseCurrentCulture(*masterPortValue, parsedMasterPort))
            {
                masterPort = parsedMasterPort;
            }
            GameMode hostMode = GameMode::Battle;
            (void)TryParseGameMode(ValueAfter(args, "mode"), hostMode);
            const std::string hostName = ParseName(args);
            std::vector<std::pair<std::string, GameMode>> hostRotation{{*hostGame, hostMode}};
            const std::optional<std::string> rotationValue = ValueAfter(args, "maprotation");
            if (rotationValue.has_value())
            {
                for (const std::string& entry : NativeRuntime::StringSplit(*rotationValue, ',', true, true))
                {
                    if (!StringEqualsOrdinalIgnoreCase(entry, *hostGame))
                    {
                        hostRotation.emplace_back(entry, hostMode);
                    }
                }
            }
            WriteLine("[net] asking " + masterHost + ":" + std::to_string(masterPort)
                + " to run " + *hostGame
                + (hostRotation.size() > 1
                    ? " and " + std::to_string(hostRotation.size() - 1) + " more"
                    : ""));
            const auto game = Network::NetMasterClient::RequestGame(masterHost,
                masterPort, *hostGame, hostMode, 420, 7,
                Entities::PlayerEntity::SlotCapacity, hostName + "'s game", 6000, hostRotation);
            if (!game.Started)
            {
                WriteLine("[net] it would not: " + game.Reason);
                SetExitCode(1);
                return true;
            }
            WriteLine("[net] running on " + game.Host + ":" + std::to_string(game.Port)
                + "; joining it");
            Network::NetConnectCommand::Run(game.Host, game.Port, hostName,
                ParseHunter(args), ParseRecolor(args));
            return true;
        }

        const std::optional<std::string> connect = ValueAfter(args, "connect");
        if (connect.has_value())
        {
            Network::NetConnectCommand::Run(*connect, ParsePort(args), ParseName(args),
                ParseHunter(args), ParseRecolor(args));
            return true;
        }

        const std::optional<std::string> check = ValueAfter(args, "netcheck");
        if (check.has_value())
        {
            const std::optional<std::string> shots = ValueAfter(args, "shots");
            double seconds = 30;
            double parsedSeconds = 0;
            if (TryParseDoubleInvariant(ValueAfter(args, "seconds"), parsedSeconds))
            {
                seconds = parsedSeconds;
            }
            double spectateAt = -1;
            double rejoinAt = -1;
            if (::HasFlag(args, "spectate"))
            {
                spectateAt = 0;
                double parsedSpectate = 0;
                if (TryParseDoubleInvariant(ValueAfter(args, "spectate"), parsedSpectate))
                {
                    spectateAt = parsedSpectate;
                }
            }
            double parsedRejoin = 0;
            if (TryParseDoubleInvariant(ValueAfter(args, "rejoin"), parsedRejoin))
            {
                rejoinAt = parsedRejoin;
            }
            Network::NetCheckClient::ShowWindow = ::HasFlag(args, "hudshots");
            const std::optional<std::string> mapVote = ValueAfter(args, "mapvote");
            std::int32_t mapVoteRow = -1;
            if (mapVote.has_value() && Int32TryParseCurrentCulture(*mapVote, mapVoteRow))
            {
                Network::NetCheckClient::MapVoteRow = std::max(0, mapVoteRow);
            }
            const int color = ValueAfter(args, "recolor").has_value()
                ? ParseRecolor(args)
                : -1;
            SetExitCode(Network::NetCheckClient::Run(*check, ParsePort(args),
                ParseName(args), ParseHunter(args), seconds, shots, width, height,
                ::HasFlag(args, "recorddemo"), spectateAt, rejoinAt, color));
            return true;
        }

        const std::optional<std::string> demoInfo = ValueAfter(args, "demoinfo");
        if (demoInfo.has_value())
        {
            SetExitCode(Network::DemoInfo::Print(*demoInfo, ::HasFlag(args, "replay")));
            return true;
        }

        const std::vector<std::string> share = ValuesAfter(args, "thumbnail");
        if (!share.empty())
        {
            const int captured = ThumbnailCapture::CaptureRooms(share, width, height);
            WriteLine("[thumbnails] captured " + std::to_string(captured) + "/"
                + std::to_string(share.size()));
            return true;
        }

        if (::HasFlag(args, "thumbnails"))
        {
            GenerateThumbnails(args, width, height);
            return true;
        }

        return false;
    }
}
