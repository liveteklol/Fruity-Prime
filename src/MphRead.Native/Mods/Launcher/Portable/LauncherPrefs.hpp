#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace MphRead
{
    enum class Hunter : std::uint8_t;
}
namespace MphRead::NativeRuntime::Rhi { enum class LowLatencyMode : std::uint8_t; }

namespace MphRead::Mods
{
    enum class WindowStartMode : std::int32_t;
}

namespace MphRead::Mods::Launcher
{
    class LauncherPrefs final
    {
    public:
        LauncherPrefs() = delete;
        inline static constexpr std::string_view DefaultServer = "89.160.162.50";

        // Where launcher.txt lives: Application Support on macOS and beside
        // the executable in portable Windows/Linux installs. Android's head
        // points this at the app's writable data directory before any reads.
        [[nodiscard]] static std::string Directory();
        static void Directory(std::string value);

        [[nodiscard]] static std::string ServerAddress();
        static void ServerAddress(std::string value);

        [[nodiscard]] static std::int32_t ServerPort() noexcept;
        static void ServerPort(std::int32_t value) noexcept;

        [[nodiscard]] static std::string MasterHost();
        static void MasterHost(std::string value);

        [[nodiscard]] static std::int32_t MasterPort() noexcept;
        static void MasterPort(std::int32_t value) noexcept;

        [[nodiscard]] static std::int32_t LastRole() noexcept;
        static void LastRole(std::int32_t value) noexcept;

        [[nodiscard]] static std::string PlayerName();
        static void PlayerName(std::string value);

        [[nodiscard]] static MphRead::Hunter LastHunter() noexcept;
        static void LastHunter(MphRead::Hunter value) noexcept;

        [[nodiscard]] static std::int32_t LastColor() noexcept;
        static void LastColor(std::int32_t value) noexcept;

        [[nodiscard]] static std::int32_t Bots() noexcept;
        static void Bots(std::int32_t value) noexcept;

        [[nodiscard]] static std::int32_t BotLevel() noexcept;
        static void BotLevel(std::int32_t value) noexcept;

        [[nodiscard]] static std::int32_t HostPort() noexcept;
        static void HostPort(std::int32_t value) noexcept;

        [[nodiscard]] static bool ListHostedGame() noexcept;
        static void ListHostedGame(bool value) noexcept;

        [[nodiscard]] static bool HostOnMaster() noexcept;
        static void HostOnMaster(bool value) noexcept;

        [[nodiscard]] static std::int32_t LastKind() noexcept;
        static void LastKind(std::int32_t value) noexcept;

        [[nodiscard]] static bool AutoUpdate() noexcept;
        static void AutoUpdate(bool value) noexcept;

        // Which renderer the game starts with: "opengl", "vulkan" or "auto".
        // Read at the next start (the window is made for one backend).
        [[nodiscard]] static const std::string& Renderer() noexcept;
        static void Renderer(std::string value);
        [[nodiscard]] static NativeRuntime::Rhi::LowLatencyMode LowLatency() noexcept;
        static void LowLatency(NativeRuntime::Rhi::LowLatencyMode value) noexcept;
        [[nodiscard]] static MphRead::Mods::WindowStartMode WindowMode() noexcept;
        static void WindowMode(MphRead::Mods::WindowStartMode value) noexcept;

        // The size and corner the game window last had, or zeroes for a
        // first run.
        //
        // The *windowed* geometry, never fullscreen's: a window remembered at
        // the size of the monitor and then opened with a title bar is a
        // window taller than the screen, and the thing worth putting back is
        // what the player dragged it to.
        //
        // The position travels with the size because half of it is no
        // feature: a window that comes back the right shape in the middle of
        // the screen has still been moved. Both are checked against the
        // displays that exist now before they are used -- see
        // Mods::WindowGeometry -- because a monitor that has been unplugged is
        // a window nobody can reach.
        [[nodiscard]] static std::int32_t WindowWidth() noexcept;
        static void WindowWidth(std::int32_t value) noexcept;
        [[nodiscard]] static std::int32_t WindowHeight() noexcept;
        static void WindowHeight(std::int32_t value) noexcept;

        // Where the window's client area started. Meaningless while both
        // sizes are 0.
        [[nodiscard]] static std::int32_t WindowX() noexcept;
        static void WindowX(std::int32_t value) noexcept;
        [[nodiscard]] static std::int32_t WindowY() noexcept;
        static void WindowY(std::int32_t value) noexcept;

        // Whether it was maximized, which is not a size.
        //
        // Kept apart because restoring a maximized window by its rectangle
        // gets it visibly wrong: it comes back filling the screen but not
        // *maximized*, so the button says restore, dragging it does nothing
        // expected, and it does not follow a change of resolution. The
        // rectangle underneath is still saved, so un-maximizing lands where it
        // used to.
        [[nodiscard]] static bool WindowMaximized() noexcept;
        static void WindowMaximized(bool value) noexcept;

        [[nodiscard]] static bool DebugLogs() noexcept;
        static void DebugLogs(bool value) noexcept;

        static void Load();
        static void Save();

    private:
        [[nodiscard]] static std::string Path();

        // "1280x768" or "40,60" -- one parser for both, since the only
        // difference is which character is in the middle. Leaves both at zero
        // on anything it does not understand, which is the value that means
        // "no saved geometry".
        static void ReadPair(
            std::string_view value, std::int32_t& first, std::int32_t& second);

        static std::string _directory;
        static std::string _serverAddress;
        static std::int32_t _serverPort;
        static std::string _masterHost;
        static std::int32_t _masterPort;
        static std::int32_t _lastRole;
        static std::string _playerName;
        static MphRead::Hunter _lastHunter;
        static std::int32_t _lastColor;
        static std::int32_t _bots;
        static std::int32_t _botLevel;
        static std::int32_t _hostPort;
        static bool _listHostedGame;
        static bool _hostOnMaster;
        static std::int32_t _lastKind;
        static bool _autoUpdate;
        static MphRead::Mods::WindowStartMode _windowMode;
        static std::string _renderer;
        static NativeRuntime::Rhi::LowLatencyMode _lowLatency;
        static std::int32_t _windowWidth;
        static std::int32_t _windowHeight;
        static std::int32_t _windowX;
        static std::int32_t _windowY;
        static bool _windowMaximized;
        static bool _debugLogs;
    };
}
