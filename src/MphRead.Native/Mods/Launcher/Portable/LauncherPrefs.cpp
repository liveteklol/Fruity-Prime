#include "LauncherPrefs.hpp"
#include "../../../NativeRuntime/Rhi/SceneBackend.hpp"

#include "../../Platform/AppPaths.hpp"

#include "../../../Formats/Enums.hpp"
#include "../../WindowMode.hpp"
#include "../../Branding.hpp"
#include "../../Network/NetMaster.hpp"
#include "../../Network/NetProtocol.hpp"
#include "../../Network/PlayerColors.hpp"
#include "../../../NativeRuntime/System/Encoding.hpp"
#include "../../../NativeRuntime/System/Globalization.hpp"
#include "../../../NativeRuntime/System/IO.hpp"
#include "../../../NativeRuntime/System/Runtime.hpp"
#include "NativeRuntime/System/Globalization.hpp"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <system_error>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <stdlib.h>
#include <sys/stat.h>
#elif defined(__FreeBSD__)
#include <limits.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#elif defined(__OpenBSD__)
#include <stdlib.h>
#include <sys/stat.h>
#elif defined(__sun)
#include <stdlib.h>
#include <sys/stat.h>
#elif defined(__linux__)
#include <sys/auxv.h>
#include <stdlib.h>
#include <sys/stat.h>
#elif defined(__unix__)
#include <stdlib.h>
#include <sys/stat.h>
#endif

using ::MphRead::NativeRuntime::AppContextBaseDirectory;
using ::MphRead::NativeRuntime::BooleanTryParse;
using ::MphRead::NativeRuntime::EnvironmentProcessPath;
using ::MphRead::NativeRuntime::FileExists;
using ::MphRead::NativeRuntime::FileReadAllLines;
using ::MphRead::NativeRuntime::FileWriteAllLines;
using ::MphRead::NativeRuntime::Int32TryParseInvariant;
using ::MphRead::NativeRuntime::PathCombine;
using ::MphRead::NativeRuntime::PathFromUtf8;
using ::MphRead::NativeRuntime::StringEqualsOrdinalIgnoreCase;
using ::MphRead::NativeRuntime::StringTrimView;
using ::MphRead::NativeRuntime::Utf8GetString;
using ::MphRead::NativeRuntime::WideToWtf8;

namespace
{

    [[nodiscard]] bool IsIntegerWhitespace(char value) noexcept
    {
        const unsigned char character = static_cast<unsigned char>(value);
        return character == 0x20U
            || (character >= 0x09U && character <= 0x0DU);
    }

    [[nodiscard]] bool HasValidIntegerTrailingCharacters(
        std::string_view value, std::size_t index) noexcept
    {
        while (index < value.size() && IsIntegerWhitespace(value[index]))
        {
            ++index;
        }
        while (index < value.size() && value[index] == '\0')
        {
            ++index;
        }
        return index == value.size();
    }

    [[nodiscard]] bool TryParseHunter(
        std::string_view value, MphRead::Hunter& result)
    {
        // Enum.TryParse(value, ignoreCase: true, out Hunter).
        return ::MphRead::TryParse(value, true, result);
    }

    [[nodiscard]] std::string HunterToString(MphRead::Hunter hunter)
    {
        return ::MphRead::ToString(hunter);
    }

#if defined(__APPLE__) || defined(__OpenBSD__) || defined(__sun) \
    || defined(__linux__) \
    || (defined(__unix__) && !defined(__EMSCRIPTEN__) && !defined(__wasi__))
#endif

}

namespace MphRead::Mods::Launcher
{
    std::string LauncherPrefs::_directory = Platform::AppPaths::UserDataDirectory();
    std::string LauncherPrefs::_serverAddress(LauncherPrefs::DefaultServer);
    std::int32_t LauncherPrefs::_serverPort = Network::NetConfig::DefaultPort;
    std::string LauncherPrefs::_masterHost = Network::NetMasterConfig::DefaultHost;
    std::int32_t LauncherPrefs::_masterPort = Network::NetMasterConfig::DefaultPort;
    std::int32_t LauncherPrefs::_lastRole = 0;
    std::string LauncherPrefs::_playerName = "Player";
    MphRead::Hunter LauncherPrefs::_lastHunter = static_cast<MphRead::Hunter>(0);
    std::int32_t LauncherPrefs::_lastColor = 0;
    std::int32_t LauncherPrefs::_bots = 3;
    std::int32_t LauncherPrefs::_botLevel = 1;
    std::int32_t LauncherPrefs::_hostPort = Network::NetConfig::DefaultPort;
    bool LauncherPrefs::_listHostedGame = true;
    bool LauncherPrefs::_hostOnMaster = true;
    std::int32_t LauncherPrefs::_lastKind = 0;
    bool LauncherPrefs::_autoUpdate = true;
    std::string LauncherPrefs::_renderer = "opengl";
    NativeRuntime::Rhi::LowLatencyMode LauncherPrefs::_lowLatency = NativeRuntime::Rhi::LowLatencyMode::Off;
    std::int32_t LauncherPrefs::_windowWidth = 0;
    std::int32_t LauncherPrefs::_windowHeight = 0;
    std::int32_t LauncherPrefs::_windowX = 0;
    std::int32_t LauncherPrefs::_windowY = 0;
    bool LauncherPrefs::_windowMaximized = false;
    MphRead::Mods::WindowStartMode LauncherPrefs::_windowMode
        = static_cast<MphRead::Mods::WindowStartMode>(0);
    bool LauncherPrefs::_debugLogs = true;

    std::string LauncherPrefs::Directory()
    {
        return _directory;
    }

    void LauncherPrefs::Directory(std::string value)
    {
        _directory = std::move(value);
    }

    std::string LauncherPrefs::ServerAddress()
    {
        return _serverAddress;
    }

    void LauncherPrefs::ServerAddress(std::string value)
    {
        _serverAddress = std::move(value);
    }

    std::int32_t LauncherPrefs::ServerPort() noexcept
    {
        return _serverPort;
    }

    void LauncherPrefs::ServerPort(std::int32_t value) noexcept
    {
        _serverPort = value;
    }

    std::string LauncherPrefs::MasterHost()
    {
        return _masterHost;
    }

    void LauncherPrefs::MasterHost(std::string value)
    {
        _masterHost = std::move(value);
    }

    std::int32_t LauncherPrefs::MasterPort() noexcept
    {
        return _masterPort;
    }

    void LauncherPrefs::MasterPort(std::int32_t value) noexcept
    {
        _masterPort = value;
    }

    std::int32_t LauncherPrefs::LastRole() noexcept
    {
        return _lastRole;
    }

    void LauncherPrefs::LastRole(std::int32_t value) noexcept
    {
        _lastRole = value;
    }

    std::string LauncherPrefs::PlayerName()
    {
        return _playerName;
    }

    void LauncherPrefs::PlayerName(std::string value)
    {
        _playerName = std::move(value);
    }

    MphRead::Hunter LauncherPrefs::LastHunter() noexcept
    {
        return _lastHunter;
    }

    void LauncherPrefs::LastHunter(MphRead::Hunter value) noexcept
    {
        _lastHunter = value;
    }

    std::int32_t LauncherPrefs::LastColor() noexcept
    {
        return _lastColor;
    }

    void LauncherPrefs::LastColor(std::int32_t value) noexcept
    {
        _lastColor = value;
    }

    std::int32_t LauncherPrefs::Bots() noexcept
    {
        return _bots;
    }

    void LauncherPrefs::Bots(std::int32_t value) noexcept
    {
        _bots = value;
    }

    std::int32_t LauncherPrefs::BotLevel() noexcept
    {
        return _botLevel;
    }

    void LauncherPrefs::BotLevel(std::int32_t value) noexcept
    {
        _botLevel = value;
    }

    std::int32_t LauncherPrefs::HostPort() noexcept
    {
        return _hostPort;
    }

    void LauncherPrefs::HostPort(std::int32_t value) noexcept
    {
        _hostPort = value;
    }

    bool LauncherPrefs::ListHostedGame() noexcept
    {
        return _listHostedGame;
    }

    void LauncherPrefs::ListHostedGame(bool value) noexcept
    {
        _listHostedGame = value;
    }

    bool LauncherPrefs::HostOnMaster() noexcept
    {
        return _hostOnMaster;
    }

    void LauncherPrefs::HostOnMaster(bool value) noexcept
    {
        _hostOnMaster = value;
    }

    std::int32_t LauncherPrefs::LastKind() noexcept
    {
        return _lastKind;
    }

    void LauncherPrefs::LastKind(std::int32_t value) noexcept
    {
        _lastKind = value;
    }

    bool LauncherPrefs::AutoUpdate() noexcept
    {
        return _autoUpdate;
    }

    void LauncherPrefs::AutoUpdate(bool value) noexcept
    {
        _autoUpdate = value;
    }

    NativeRuntime::Rhi::LowLatencyMode LauncherPrefs::LowLatency() noexcept { return _lowLatency; }
    void LauncherPrefs::LowLatency(NativeRuntime::Rhi::LowLatencyMode value) noexcept
    { _lowLatency = value <= NativeRuntime::Rhi::LowLatencyMode::OnBoost ? value : NativeRuntime::Rhi::LowLatencyMode::Off; }

    const std::string& LauncherPrefs::Renderer() noexcept
    {
        return _renderer;
    }

    void LauncherPrefs::Renderer(std::string value)
    {
        ::MphRead::NativeRuntime::Rhi::SceneBackendRequest request{};
        if (::MphRead::NativeRuntime::Rhi::ParseSceneBackendRequest(value, request))
            _renderer = std::string(::MphRead::NativeRuntime::Rhi::SceneBackendRequestName(request));
    }

    MphRead::Mods::WindowStartMode LauncherPrefs::WindowMode() noexcept
    {
        return _windowMode;
    }

    void LauncherPrefs::WindowMode(
        MphRead::Mods::WindowStartMode value) noexcept
    {
        _windowMode = value;
    }

    std::int32_t LauncherPrefs::WindowWidth() noexcept
    {
        return _windowWidth;
    }

    void LauncherPrefs::WindowWidth(std::int32_t value) noexcept
    {
        _windowWidth = value;
    }

    std::int32_t LauncherPrefs::WindowHeight() noexcept
    {
        return _windowHeight;
    }

    void LauncherPrefs::WindowHeight(std::int32_t value) noexcept
    {
        _windowHeight = value;
    }

    std::int32_t LauncherPrefs::WindowX() noexcept
    {
        return _windowX;
    }

    void LauncherPrefs::WindowX(std::int32_t value) noexcept
    {
        _windowX = value;
    }

    std::int32_t LauncherPrefs::WindowY() noexcept
    {
        return _windowY;
    }

    void LauncherPrefs::WindowY(std::int32_t value) noexcept
    {
        _windowY = value;
    }

    bool LauncherPrefs::WindowMaximized() noexcept
    {
        return _windowMaximized;
    }

    void LauncherPrefs::WindowMaximized(bool value) noexcept
    {
        _windowMaximized = value;
    }

    void LauncherPrefs::ReadPair(
        std::string_view value, std::int32_t& first, std::int32_t& second)
    {
        first = 0;
        second = 0;
        const std::size_t at = value.find_first_of("xX,");
        if (at == std::string_view::npos || at == 0)
        {
            return;
        }
        std::int32_t a = 0;
        std::int32_t b = 0;
        if (Int32TryParseInvariant(value.substr(0, at), a)
            && Int32TryParseInvariant(value.substr(at + 1), b))
        {
            first = a;
            second = b;
        }
    }

    bool LauncherPrefs::DebugLogs() noexcept
    {
        return _debugLogs;
    }

    void LauncherPrefs::DebugLogs(bool value) noexcept
    {
        _debugLogs = value;
    }

    std::string LauncherPrefs::Path()
    {
        return PathCombine(_directory, "launcher.txt");
    }

    void LauncherPrefs::Load()
    {
        if (!FileExists(Path()))
        {
            return;
        }

        try
        {
            for (const std::string& raw : FileReadAllLines(Path()))
            {
                const std::string_view line = StringTrimView(raw);
                const std::size_t split = line.find('=');
                if (line.empty() || line.front() == '#'
                    || split == std::string_view::npos || split == 0)
                {
                    continue;
                }

                const std::string_view key
                    = StringTrimView(line.substr(0, split));
                const std::string_view value
                    = StringTrimView(line.substr(split + 1));

                if (key == "server_address")
                {
                    _serverAddress.assign(value);
                }
                else if (key == "master_host")
                {
                    if (!value.empty())
                    {
                        _masterHost.assign(value);
                    }
                }
                else if (key == "master_port")
                {
                    std::int32_t masterPort = 0;
                    if (Int32TryParseInvariant(value, masterPort)
                        && masterPort > 0 && masterPort <= 65535)
                    {
                        _masterPort = masterPort;
                    }
                }
                else if (key == "server_port")
                {
                    std::int32_t port = 0;
                    if (Int32TryParseInvariant(value, port))
                    {
                        _serverPort = port;
                    }
                }
                else if (key == "player_name")
                {
                    if (!value.empty())
                    {
                        _playerName.assign(value);
                    }
                }
                else if (key == "last_role")
                {
                    std::int32_t role = 0;
                    if (Int32TryParseInvariant(value, role))
                    {
                        _lastRole = role;
                    }
                }
                else if (key == "hunter")
                {
                    MphRead::Hunter hunter = static_cast<MphRead::Hunter>(0);
                    if (TryParseHunter(value, hunter))
                    {
                        _lastHunter = hunter;
                    }
                }
                else if (key == "color")
                {
                    std::int32_t color = 0;
                    if (Int32TryParseInvariant(value, color))
                    {
                        _lastColor = Network::PlayerColors::Clamp(color);
                    }
                }
                else if (key == "bots")
                {
                    std::int32_t bots = 0;
                    if (Int32TryParseInvariant(value, bots))
                    {
                        _bots = bots;
                    }
                }
                else if (key == "bot_level")
                {
                    std::int32_t level = 0;
                    if (Int32TryParseInvariant(value, level))
                    {
                        _botLevel = level;
                    }
                }
                else if (key == "host_on_master")
                {
                    bool hostOnMaster = false;
                    if (BooleanTryParse(value, hostOnMaster))
                    {
                        _hostOnMaster = hostOnMaster;
                    }
                }
                else if (key == "list_hosted")
                {
                    bool listHosted = false;
                    if (BooleanTryParse(value, listHosted))
                    {
                        _listHostedGame = listHosted;
                    }
                }
                else if (key == "host_port")
                {
                    std::int32_t hostPort = 0;
                    if (Int32TryParseInvariant(value, hostPort))
                    {
                        _hostPort = hostPort;
                    }
                }
                else if (key == "window_mode")
                {
                    _windowMode = MphRead::Mods::WindowMode::Parse(
                        std::optional<std::string_view>{value}, _windowMode);
                }
                else if (key == "window_size")
                {
                    std::int32_t width = 0;
                    std::int32_t height = 0;
                    ReadPair(value, width, height);
                    _windowWidth = width;
                    _windowHeight = height;
                }
                else if (key == "window_pos")
                {
                    std::int32_t x = 0;
                    std::int32_t y = 0;
                    ReadPair(value, x, y);
                    _windowX = x;
                    _windowY = y;
                }
                else if (key == "window_maximized")
                {
                    bool maximized = false;
                    if (BooleanTryParse(value, maximized))
                    {
                        _windowMaximized = maximized;
                    }
                }
                else if (key == "renderer")
                {
                    ::MphRead::NativeRuntime::Rhi::SceneBackendRequest request{};
                    if (::MphRead::NativeRuntime::Rhi::ParseSceneBackendRequest(value, request))
                    {
                        _renderer = std::string(::MphRead::NativeRuntime::Rhi::SceneBackendRequestName(request));
                        // The command line's -rhi, when given, still wins.
                        ::MphRead::NativeRuntime::Rhi::RequestSceneBackend(request, false);
                    }
                }
                else if (key == "low_latency")
                {
                    if (value == "off") _lowLatency = NativeRuntime::Rhi::LowLatencyMode::Off;
                    else if (value == "on") _lowLatency = NativeRuntime::Rhi::LowLatencyMode::On;
                    else if (value == "onboost") _lowLatency = NativeRuntime::Rhi::LowLatencyMode::OnBoost;
                }
                else if (key == "auto_update")
                {
                    bool autoUpdate = false;
                    if (BooleanTryParse(value, autoUpdate))
                    {
                        _autoUpdate = autoUpdate;
                    }
                }
                else if (key == "debug_logs")
                {
                    bool debugLogs = false;
                    if (BooleanTryParse(value, debugLogs))
                    {
                        _debugLogs = debugLogs;
                    }
                }
                else if (key == "last_kind")
                {
                    std::int32_t kind = 0;
                    if (Int32TryParseInvariant(value, kind))
                    {
                        _lastKind = kind;
                    }
                }
            }
        }
        catch (const std::exception&)
        {
        }
        catch (...)
        {
        }
    }

    void LauncherPrefs::Save()
    {
        try
        {
            const std::string path = Path();
            std::vector<std::string> lines;
            lines.reserve(18);
            lines.emplace_back(
                "# " + std::string(Branding::Name) + " launcher preferences.");
            lines.emplace_back("server_address=" + _serverAddress);
            lines.emplace_back(
                "server_port=" + ::MphRead::NativeRuntime::ToStringInvariant(_serverPort));
            lines.emplace_back("master_host=" + _masterHost);
            lines.emplace_back(
                "master_port=" + ::MphRead::NativeRuntime::ToStringInvariant(_masterPort));
            lines.emplace_back("last_role=" + ::MphRead::NativeRuntime::ToStringInvariant(_lastRole));
            lines.emplace_back("player_name=" + _playerName);
            lines.emplace_back("hunter=" + HunterToString(_lastHunter));
            lines.emplace_back("color=" + ::MphRead::NativeRuntime::ToStringInvariant(_lastColor));
            lines.emplace_back("bots=" + ::MphRead::NativeRuntime::ToStringInvariant(_bots));
            lines.emplace_back("bot_level=" + ::MphRead::NativeRuntime::ToStringInvariant(_botLevel));
            lines.emplace_back("host_port=" + ::MphRead::NativeRuntime::ToStringInvariant(_hostPort));
            lines.emplace_back(
                std::string("list_hosted=")
                    + (_listHostedGame ? "true" : "false"));
            lines.emplace_back(
                std::string("host_on_master=")
                    + (_hostOnMaster ? "true" : "false"));
            lines.emplace_back("last_kind=" + ::MphRead::NativeRuntime::ToStringInvariant(_lastKind));
            lines.emplace_back("renderer=" + _renderer);
            lines.emplace_back(std::string("low_latency=") + (_lowLatency == NativeRuntime::Rhi::LowLatencyMode::Off
                ? "off" : _lowLatency == NativeRuntime::Rhi::LowLatencyMode::On ? "on" : "onboost"));
            lines.emplace_back(
                std::string("auto_update=")
                    + (_autoUpdate ? "true" : "false"));
            lines.emplace_back(
                std::string("debug_logs=")
                    + (_debugLogs ? "true" : "false"));
            lines.emplace_back(
                std::string("window_mode=")
                    + (static_cast<std::int32_t>(_windowMode) == 1
                        ? "borderless"
                        : "windowed"));
            lines.emplace_back("window_size=" + ::MphRead::NativeRuntime::ToStringInvariant(_windowWidth)
                + "x" + ::MphRead::NativeRuntime::ToStringInvariant(_windowHeight));
            lines.emplace_back("window_pos=" + ::MphRead::NativeRuntime::ToStringInvariant(_windowX)
                + "," + ::MphRead::NativeRuntime::ToStringInvariant(_windowY));
            lines.emplace_back(
                std::string("window_maximized=")
                    + (_windowMaximized ? "true" : "false"));
            FileWriteAllLines(path, lines);
        }
        catch (const std::exception&)
        {
        }
        catch (...)
        {
        }
    }
}
