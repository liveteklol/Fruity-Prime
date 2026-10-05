// Shell on Qt: the same one-window shell the renderer drives (BeforeFrame,
// TickUi, pointer and key routing), with the menus as a Qt Quick scene (UiHost)
// instead of the Skia-drawn Avalonia port. The match lifecycle below is the
// Avalonia Shell's, unchanged; only the pages differ.

#include "../../MphRead.Native/Mods/Launcher/Gui/GuiLauncher.hpp"
#include "../../MphRead.Native/Mods/Launcher/Gui/Shell.hpp"

#include "ShellBridge.hpp"
#include "UiCapture.hpp"
#include "UiHost.hpp"
#include "../Platform/QtApp.hpp"

#include "../../MphRead.Native/GameState.hpp"
#include "../../MphRead.Native/Menu.hpp"
#include "../../MphRead.Native/Metadata/Metadata.hpp"
#include "../../MphRead.Native/Renderer.hpp"
#include "../../MphRead.Native/Mods/Branding.hpp"
#include "../../MphRead.Native/Mods/DebugLog.hpp"
#include "../../MphRead.Native/Mods/EndScreen.hpp"
#include "../../MphRead.Native/Mods/GameSettings.hpp"
#include "../../MphRead.Native/Mods/Launcher/Portable/GameFiles.hpp"
#include "../../MphRead.Native/Mods/Launcher/Portable/LauncherPrefs.hpp"
#include "../../MphRead.Native/Mods/Launcher/Portable/MatchStart.hpp"
#include "../../MphRead.Native/Mods/Network/NetHostSession.hpp"
#include "../../MphRead.Native/Mods/Network/NetSession.hpp"
#include "../../MphRead.Native/Mods/PauseMenu.hpp"
#include "../../MphRead.Native/Mods/Render/UiOverlay.hpp"
#include "../../MphRead.Native/Mods/ThumbnailGenerator.hpp"
#include "../../MphRead.Native/Mods/WindowMode.hpp"
#include "../../MphRead.Native/NativeRuntime/System/Console.hpp"
#include "../../MphRead.Native/NativeRuntime/System/ExceptionText.hpp"
#include "../../MphRead.Native/NativeRuntime/System/Runtime.hpp"

#include <QtCore/QDir>
#include <QtCore/QVariantMap>
#include <QtGui/QImage>
#include <QtGui/QOpenGLContext>
#include <QtGui/QOpenGLFunctions>
#include <QtGui/QWindow>

#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace MphRead::Mods::Launcher::Gui
{
    namespace
    {
        using ::MphRead::Mods::Launcher::LaunchKind;
        using ::MphRead::Mods::Launcher::LaunchPlan;
        namespace Portable = ::MphRead::Mods::Launcher;

        bool g_active = false;
        MphRead::RenderWindow* g_window = nullptr;
        std::shared_ptr<MphRead::MenuSettings> g_settings;
        std::vector<std::string> g_rooms;
        std::optional<LaunchPlan> g_pending;
        std::optional<LaunchPlan> g_played;
        bool g_endMatch = false;
        bool g_quit = false;
        bool g_menuOpen = false;
        bool g_endPanel = false;
        std::unique_ptr<MphRead::Qt::ShellBridge> g_bridge;
        std::unique_ptr<MphRead::Qt::UiHost> g_host;

        [[nodiscard]] LaunchPlan WithRoomKey(const LaunchPlan& source, std::string roomKey)
        {
            LaunchPlan::Init init;
            init.Kind = source.Kind();
            init.Lobby = source.Lobby();
            init.Hunter = source.Hunter();
            init.RoomKey = std::move(roomKey);
            init.Mode = source.Mode();
            init.Bots = source.Bots();
            init.BotLevel = source.BotLevel();
            init.Port = source.Port();
            init.PlayerName = source.PlayerName();
            init.SaveSlot = source.SaveSlot();
            init.NewGame = source.NewGame();
            init.DemoPath = source.DemoPath();
            return LaunchPlan(init);
        }

        void Decided(LaunchPlan plan)
        {
            if (plan.Kind() == LaunchKind::None)
            {
                Shell::RequestQuit();
                return;
            }
            g_pending = std::move(plan);
        }

        void EnsureBridge()
        {
            if (g_bridge != nullptr)
            {
                return;
            }
            MphRead::Qt::ShellBridge::Actions actions;
            actions.Launch = [](LaunchPlan plan) { Decided(std::move(plan)); };
            actions.Quit = []() { Shell::RequestQuit(); };
            actions.Resume = []() { Shell::CloseMenu(); };
            actions.GameFilesChanged = []()
            {
                if (!Portable::GameFiles::Ready())
                {
                    return;
                }
                Portable::GameFiles::ApplyPaths();
                g_rooms = MphRead::Mods::ThumbnailGenerator::MultiplayerRooms();
                if (g_bridge != nullptr)
                {
                    g_bridge->SetRooms(g_rooms, true);
                }
            };
            actions.ToggleFullscreen = []()
            {
                // InGameMenu: the toggle waits for the frame, then the menu closes.
                MphRead::Mods::PauseMenu::RequestFullscreenToggle();
                Shell::CloseMenu();
            };
            g_bridge = std::make_unique<MphRead::Qt::ShellBridge>(std::move(actions));
        }

        void ShowPage(const char* page)
        {
            EnsureBridge();
            g_bridge->SetPage(QString::fromUtf8(page));
            if (g_host != nullptr)
            {
                g_host->MarkDirty();
            }
        }

        void HidePage()
        {
            if (g_bridge != nullptr)
            {
                g_bridge->SetPage(QString());
            }
            MphRead::Mods::Render::UiOverlay::Visible(false);
        }

        void ShowFrontScreen()
        {
            MphRead::Mods::PauseMenu::Reset();
            g_settings = MphRead::GameState::LoadSettings();
            MphRead::Mods::GameSettings::Apply(g_settings);
            Portable::LauncherPrefs::Load();
            if (g_rooms.empty() && Portable::GameFiles::Ready())
            {
                g_rooms = MphRead::Mods::ThumbnailGenerator::MultiplayerRooms();
            }
            if (g_window != nullptr)
            {
                g_window->Title(std::string(MphRead::Mods::Branding::Name));
            }
            EnsureBridge();
            g_bridge->SetSettings(g_settings);
            g_bridge->SetRooms(g_rooms, Portable::GameFiles::Ready());
            g_bridge->refreshProfile();
            ShowPage("front");
        }

        void StartMatch(MphRead::RenderWindow& window, LaunchPlan plan)
        {
            g_played = plan;
            HidePage();
            try
            {
                if (!Portable::MatchStart::Begin(window, g_settings, plan))
                {
                    MphRead::Mods::Network::NetSession::ReportMatchLoadFailed(
                        "The map could not be loaded.");
                    g_endMatch = true;
                }
            }
            catch (const std::exception&)
            {
                const std::exception_ptr exception = std::current_exception();
                std::cout << "\nThe game could not start: "
                          << MphRead::NativeRuntime::ExceptionMessage(exception) << '\n';
                MphRead::Mods::DebugLog::Line("crash", "the match could not start");
                MphRead::Mods::DebugLog::Exception("crash", exception);
                MphRead::Mods::Network::NetSession::ReportMatchLoadFailed(
                    MphRead::NativeRuntime::ExceptionMessage(exception));
                g_endMatch = true;
            }
        }

        // Shell.EndNetworkMatchToLobby: a persistent lobby's match is over;
        // its players are back in the lobby screen, still connected.
        void EndNetworkMatchToLobby(MphRead::RenderWindow& window)
        {
            Shell::CloseMenu();
            window.EndScene();
            Portable::MatchStart::AfterMatch();
            MphRead::Mods::Network::NetSession::ResetMatchState();
            MphRead::Mods::PauseMenu::Reset();
            ShowPage("front");
            g_bridge->OpenLobby();
        }

        void EndMatch(MphRead::RenderWindow& window)
        {
            Shell::CloseMenu();
            window.EndScene();
            MphRead::Mods::Network::NetSession::Stop();
            MphRead::Mods::Network::NetHostSession::Stop();
            Portable::MatchStart::AfterMatch();
            ShowFrontScreen();
        }

        // FP_QT_SHOT=path[,frame]: save the presented frame once, for checks
        // without a screen; FP_QT_SHOT_QUIT=1 closes the window after it.
        void MaybeShoot(MphRead::RenderWindow& window)
        {
            static const QString spec = qEnvironmentVariable("FP_QT_SHOT");
            static int frame = 0;
            static bool done = false;
            if (spec.isEmpty() || done)
            {
                return;
            }
            const QStringList parts = spec.split(QLatin1Char(','));
            const int at = parts.size() > 1 ? parts[1].toInt() : 120;
            if (++frame < at)
            {
                return;
            }
            done = true;
            const OpenTK::Mathematics::Vector2i size = window.FramebufferSize();
            QImage image(size.X, size.Y, QImage::Format_RGBA8888);
            QOpenGLContext::currentContext()->functions()->glReadPixels(
                0, 0, size.X, size.Y, GL_RGBA, GL_UNSIGNED_BYTE, image.bits());
            image.mirrored(false, true).save(parts[0]);
            std::cout << "[shot] " << parts[0].toStdString() << '\n';
            if (qEnvironmentVariableIntValue("FP_QT_SHOT_QUIT") != 0)
            {
                window.Close();
            }
        }

        // FP_BENCH_SECONDS=N: the same offline match on every build
        // (FRUITY_SHOT_ROOM, Samus, Battle, 3 level-1 bots), held N seconds
        // once loaded, frames per second printed each second, then quit. The
        // main player stays on "press fire"; the bots play.
        void BenchStep(MphRead::RenderWindow& window)
        {
            static const int seconds = qEnvironmentVariableIntValue("FP_BENCH_SECONDS");
            if (seconds <= 0)
            {
                return;
            }
            static int frame = 0;
            ++frame;
            if (frame == 60)
            {
                LaunchPlan::Init init;
                init.Kind = LaunchKind::Offline;
                init.RoomKey = qEnvironmentVariable("FRUITY_SHOT_ROOM").toStdString();
                init.Mode = static_cast<MphRead::GameMode>(3); // Battle
                init.Hunter = static_cast<MphRead::Hunter>(0); // Samus
                init.Bots = 3;
                init.BotLevel = 1;
                Decided(LaunchPlan(init));
                return;
            }
            if (frame < 60 || !window.HasScene())
            {
                return;
            }
            using Clock = std::chrono::steady_clock;
            static std::optional<Clock::time_point> start;
            static Clock::time_point second;
            static int frames = 0;
            const auto now = Clock::now();
            if (!start)
            {
                start = now;
                second = now;
                std::cout << "[bench] match loaded" << std::endl;
            }
            ++frames;
            if (now - second >= std::chrono::seconds(1))
            {
                std::cout << "[bench] fps=" << frames << std::endl;
                frames = 0;
                second = now;
            }
            if (now - *start >= std::chrono::seconds(seconds))
            {
                std::cout << "[bench] done" << std::endl;
                Shell::RequestQuit();
            }
        }

        // FP_QT_DEMO=DIR: a scripted check in the real window -- the front
        // screen, then an offline match, then the pause menu over it, each
        // captured to DIR, then quit.
        void DemoStep(MphRead::RenderWindow& window)
        {
            static const QString dir = qEnvironmentVariable("FP_QT_DEMO");
            static int frame = 0;
            if (dir.isEmpty())
            {
                return;
            }
            ++frame;
            const auto shoot = [&](const char* name)
            {
                const OpenTK::Mathematics::Vector2i size = window.FramebufferSize();
                QImage image(size.X, size.Y, QImage::Format_RGBA8888);
                QOpenGLContext::currentContext()->functions()->glReadPixels(
                    0, 0, size.X, size.Y, GL_RGBA, GL_UNSIGNED_BYTE, image.bits());
                QDir().mkpath(dir);
                const QString path = QDir(dir).filePath(QLatin1String(name) + QStringLiteral(".png"));
                const bool saved = image.flipped(::Qt::Vertical).convertToFormat(QImage::Format_RGB32).save(path);
                std::cout << "[demo] " << path.toStdString() << (saved ? "" : " (not written)") << '\n';
            };
            if (frame == 150)
            {
                shoot("window-start");
                if (!g_rooms.empty())
                {
                    LaunchPlan::Init init;
                    init.Kind = LaunchKind::Offline;
                    init.RoomKey = g_rooms.front();
                    init.Mode = static_cast<MphRead::GameMode>(3);
                    init.Hunter = static_cast<MphRead::Hunter>(0);
                    init.Bots = 3;
                    init.BotLevel = 1;
                    Decided(LaunchPlan(init));
                }
            }
            else if (frame == 500)
            {
                shoot("window-match");
                (void)Shell::OpenPauseMenu();
            }
            else if (frame == 530)
            {
                shoot("window-pause");
                Shell::RequestQuit();
            }
        }

        // The game window's current Qt event, handed to the menus whole.
        void DeliverCurrentEvent()
        {
            if (g_host == nullptr)
            {
                return;
            }
            if (QEvent* const event = MphRead::Qt::CurrentEvent())
            {
                g_host->Deliver(*event);
            }
        }
    }

    bool Shell::Active() noexcept
    {
        return g_active;
    }

    bool Shell::UiVisible()
    {
        return g_bridge != nullptr && g_bridge->Showing();
    }

    MphRead::RenderWindow* Shell::Window() noexcept
    {
        return g_window;
    }

    bool Shell::EndPanelUp() noexcept
    {
        return g_endPanel;
    }

    bool Shell::CanPlayAnother()
    {
        return g_active && !g_pending.has_value() && g_played.has_value()
            && g_played->Kind() == LaunchKind::Offline;
    }

    std::int32_t Shell::ShotMisses() noexcept
    {
        return 0;
    }

    bool Shell::Run()
    {
        Portable::LauncherPrefs::Load();
        if (Portable::GameFiles::Ready())
        {
            Portable::GameFiles::ApplyPaths();
            MphRead::Mods::ThumbnailGenerator::EnsureCustomPreviews();
        }
        if (!MphRead::Mods::WindowMode::StartupForced())
        {
            MphRead::Mods::WindowMode::Startup(Portable::LauncherPrefs::WindowMode());
        }

        MphRead::RenderWindow::LogCreatingWindow();
        std::unique_ptr<MphRead::RenderWindow> window;
        bool ran = false;
        try
        {
            window = std::make_unique<MphRead::RenderWindow>(true);
            g_window = window.get();
            g_active = true;
            EnsureBridge();
            if (QWindow* const gameWindow = MphRead::Qt::GameWindow())
            {
                g_host = std::make_unique<MphRead::Qt::UiHost>(*gameWindow, *g_bridge);
            }
            ShowFrontScreen();
            window->Run();
            ran = true;
        }
        catch (const std::exception&)
        {
            const std::exception_ptr exception = std::current_exception();
            std::cout << "The window could not be opened: "
                      << MphRead::NativeRuntime::ExceptionMessage(exception) << '\n';
            MphRead::Mods::DebugLog::Exception("launcher", exception);
        }

        g_host.reset();
        g_active = false;
        g_window = nullptr;
        g_pending.reset();
        g_endMatch = false;
        g_quit = false;
        MphRead::Mods::Network::NetSession::Stop();
        MphRead::Mods::Network::NetHostSession::Stop();
        window.reset();
        return ran;
    }

    void Shell::BeforeFrame(MphRead::RenderWindow& window)
    {
        if (!g_active)
        {
            return;
        }
        if (g_quit)
        {
            g_quit = false;
            window.Close();
            return;
        }
        if (window.HasScene() && MphRead::Mods::Network::NetSession::PersistentLobby()
            && MphRead::Mods::Network::NetSession::IsInLobby() && !g_endMatch)
        {
            EndNetworkMatchToLobby(window);
        }
        if (window.HasScene() && (MphRead::Mods::Network::NetSession::Refused()
            || MphRead::Mods::Network::NetSession::SessionTimedOut()))
        {
            g_endMatch = true;
        }
        if (g_endMatch)
        {
            g_endMatch = false;
            EndMatch(window);
        }
        if (g_pending.has_value())
        {
            LaunchPlan plan = *g_pending;
            g_pending.reset();
            StartMatch(window, std::move(plan));
        }
    }

    void Shell::TickUi(MphRead::RenderWindow& window)
    {
        if (g_host == nullptr)
        {
            MphRead::Mods::Render::UiOverlay::Visible(false);
            return;
        }
        const OpenTK::Mathematics::Vector2i framebuffer = window.FramebufferSize();
        g_host->Tick(framebuffer.X, framebuffer.Y);
    }

    void Shell::TickEndPanel()
    {
        // The results' side panel: up while the end screen is, unless the
        // pause menu is over the match.
        const bool want = MphRead::Mods::EndScreen::Available() && !g_menuOpen;
        if (want && !g_endPanel)
        {
            g_endPanel = true;
            MphRead::Mods::EndScreen::PanelUp(true);
            ShowPage("end");
        }
        else if (!want && g_endPanel)
        {
            g_endPanel = false;
            MphRead::Mods::EndScreen::PanelUp(false);
            if (!g_menuOpen)
            {
                HidePage();
            }
        }
    }

    void Shell::RequestEndMatch()
    {
        if (g_active)
        {
            g_endMatch = true;
        }
    }

    void Shell::RequestQuit()
    {
        g_quit = true;
    }

    void Shell::LeaveMatch(MphRead::RenderWindow& window)
    {
        if (g_active)
        {
            RequestEndMatch();
            return;
        }
        window.Close();
    }

    void Shell::Quit(MphRead::RenderWindow& window)
    {
        if (g_active)
        {
            RequestQuit();
            return;
        }
        window.Close();
    }

    bool Shell::OpenPauseMenu()
    {
        if (!g_active)
        {
            return false;
        }
        g_menuOpen = true;
        ShowPage("pause");
        return true;
    }

    void Shell::CloseMenu()
    {
        if (!g_menuOpen)
        {
            return;
        }
        g_menuOpen = false;
        // The results panel, if the match is on its end screen, comes back.
        g_endPanel = false;
        HidePage();
        MphRead::Mods::PauseMenu::MarkClosed();
    }

    void Shell::RequestShots(std::string directory)
    {
        (void)directory;
    }

    void Shell::AfterDraw(MphRead::RenderWindow& window)
    {
        MaybeShoot(window);
        BenchStep(window);
        DemoStep(window);
    }

    void Shell::PlayAnother(std::string roomKey)
    {
        if (!CanPlayAnother() || roomKey.empty() || !g_played.has_value())
        {
            return;
        }
        g_endMatch = true;
        g_pending = WithRoomKey(*g_played, std::move(roomKey));
    }

    void Shell::PointerMoved(double x, double y)
    {
        (void)x;
        (void)y;
        DeliverCurrentEvent();
    }

    void Shell::PointerButton(OpenTK::Windowing::GraphicsLibraryFramework::MouseButton button,
        double x, double y, bool down)
    {
        (void)button;
        (void)x;
        (void)y;
        (void)down;
        DeliverCurrentEvent();
    }

    void Shell::PointerWheel(double deltaX, double deltaY)
    {
        (void)deltaX;
        (void)deltaY;
        DeliverCurrentEvent();
    }

    void Shell::KeyDown(const OpenTK::Windowing::Common::KeyboardKeyEventArgs& e)
    {
        (void)e;
        DeliverCurrentEvent();
    }

    void Shell::KeyUp(const OpenTK::Windowing::Common::KeyboardKeyEventArgs& e)
    {
        (void)e;
        DeliverCurrentEvent();
    }

    void Shell::TextInput(const std::string& text)
    {
        // The key event that carried the text reached the scene already.
        (void)text;
    }

    std::atomic_bool GuiLauncher::_setUp{false};
    std::atomic_bool GuiLauncher::_failed{false};

    bool GuiLauncher::TryRun()
    {
        if (!EnsureSetup())
        {
            return false;
        }
        // FP_QT_UISHOT=DIR: photograph the menus and stop (see UiCapture).
        if (const QString shots = qEnvironmentVariable("FP_QT_UISHOT"); !shots.isEmpty())
        {
            if (Portable::GameFiles::Ready())
            {
                Portable::GameFiles::ApplyPaths();
                g_rooms = MphRead::Mods::ThumbnailGenerator::MultiplayerRooms();
            }
            (void)MphRead::Qt::UiCapture::Run(shots);
            MphRead::Qt::ShutdownApplication();
            return true;
        }
        try
        {
            const bool ran = Shell::Run();
            g_bridge.reset();
            MphRead::Qt::ShutdownApplication();
            return ran;
        }
        catch (...)
        {
            MphRead::NativeRuntime::ConsoleWriteLine("[launcher] the window could not be opened: "
                + MphRead::NativeRuntime::ExceptionMessage(std::current_exception()));
            return false;
        }
    }

    bool GuiLauncher::EnsureSetup(bool requireDisplay)
    {
        if (_setUp.load(std::memory_order_relaxed))
        {
            return true;
        }
        if (_failed.load(std::memory_order_relaxed) || (requireDisplay && !Probe()))
        {
            return false;
        }
        MphRead::Qt::EnsureApplication();
        _setUp.store(true, std::memory_order_relaxed);
        return true;
    }

    void GuiLauncher::SayWhyOnLinux()
    {
    }

    bool GuiLauncher::Probe()
    {
        namespace Runtime = ::MphRead::NativeRuntime;
        if (!Runtime::IsLinux())
        {
            return true;
        }
        const std::string display = Runtime::EnvironmentGetVariable("DISPLAY").value_or("");
        const std::string wayland = Runtime::EnvironmentGetVariable("WAYLAND_DISPLAY").value_or("");
        if (display.empty() && wayland.empty())
        {
            Runtime::ConsoleWriteLine("[launcher] no DISPLAY or WAYLAND_DISPLAY; using the text launcher");
            return false;
        }
        return true;
    }
}
