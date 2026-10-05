#include "../../MphRead.Native/Mods/Diagnostics/LauncherWindowCheck.hpp"
// Shell on Qt: the same one-window shell the renderer drives (BeforeFrame,
// TickUi, pointer and key routing), with the menus as a Qt Quick scene (UiHost)
// instead of the Skia-drawn Avalonia port. The match lifecycle below is the
// Avalonia Shell's, unchanged; only the pages differ.

#include "../../MphRead.Native/Mods/Launcher/GuiLauncher.hpp"
#include "../../MphRead.Native/Mods/Launcher/Shell.hpp"
#include "../../MphRead.Native/Mods/ScreenCapture.hpp"
#include "../../MphRead.Native/NativeRuntime/Rhi/SceneBackend.hpp"

#include "ShellBridge.hpp"
#include "SettingsModel.hpp"
#include "UiCapture.hpp"
#include "UiHost.hpp"
#include "../Platform/QtApp.hpp"

#include "../../MphRead.Native/GameState.hpp"
#include "../../MphRead.Native/Scene.hpp"
#include "../../MphRead.Native/Entities/Players/PlayerEntity.hpp"
#include "../../MphRead.Native/Entities/PlayerSpawnEntity.hpp"
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
#include <QtCore/QCoreApplication>
#include <QtCore/QVariantMap>
#include <QtGui/QImage>
#include <QtGui/QMouseEvent>
#include <QtGui/QOpenGLContext>
#include <QtGui/QOpenGLFunctions>
#include <QtGui/QWindow>

#include <exception>
#include <chrono>
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
            g_host.reset();
            MphRead::Mods::Render::UiOverlay::Release();
            g_endPanel = false;
            window.EndScene();
            Portable::MatchStart::AfterMatch();
            MphRead::Mods::Network::NetSession::ResetMatchState();
            MphRead::Mods::PauseMenu::Reset();
            ShowPage("front");
            g_bridge->OpenLobby();
            if (auto* gameWindow = MphRead::Qt::GameWindow())
                g_host = std::make_unique<MphRead::Qt::UiHost>(*gameWindow, *g_bridge);
        }

        void EndMatch(MphRead::RenderWindow& window)
        {
            Shell::CloseMenu();
            g_host.reset();
            MphRead::Mods::Render::UiOverlay::Release();
            g_endPanel = false;
            window.EndScene();
            MphRead::Mods::Network::NetSession::Stop();
            MphRead::Mods::Network::NetHostSession::Stop();
            Portable::MatchStart::AfterMatch();
            ShowFrontScreen();
            if (auto* gameWindow = MphRead::Qt::GameWindow())
                g_host = std::make_unique<MphRead::Qt::UiHost>(*gameWindow, *g_bridge);
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
            // The window as presented, read through the renderer, so a
            // Vulkan window is photographed the same way an OpenGL one is.
            const OpenTK::Mathematics::Vector2i size = window.FramebufferSize();
            const bool saved = MphRead::Mods::ScreenCapture::SaveWindow(size.X, size.Y, parts[0].toStdString());
            std::cout << "[shot] " << parts[0].toStdString() << (saved ? "" : " (not written)") << '\n';
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
            // Historical and current renderers use the same active-match
            // fixture. Keep simulation and bots running while holding only
            // the main player's input and initial spawn/camera fixed.
            if (qEnvironmentVariableIntValue("FRUITY_FPSCHECK_ONLY") != 0)
            {
                if (frame == 30)
                {
                    LaunchPlan::Init init;
                    init.Kind = LaunchKind::Offline;
                    init.RoomKey = qEnvironmentVariable("FRUITY_SHOT_ROOM").toStdString();
                    init.Mode = MphRead::GameMode::Battle;
                    init.Hunter = MphRead::Hunter::Sylux;
                    init.Bots = 3;
                    init.BotLevel = 1;
                    Decided(LaunchPlan(init));
                }
                else if (frame == 31)
                {
                    if (!window.HasScene()) { --frame; return; }
                    const auto main = MphRead::Entities::PlayerEntity::Main();
                    auto spawns = window.Scene().GetPlayerSpawnEntities().GetEnumerator();
                    if (!main || !spawns.MoveNext())
                        throw std::runtime_error("FPS fixture needs the main player and first spawn.");
                    const auto& first = *spawns.Current();
                    main->Spawn(first.Position, first.FacingVector(), first.UpVector(), first.NodeRef, true);
                    std::cout << "[fps fixture] first spawn; Sylux; 3 bots; simulation_frame="
                        << window.Scene().FrameCount() << '\n';
                }
                else if (frame == 32)
                {
                    static const auto start = std::chrono::steady_clock::now();
                    if (std::chrono::steady_clock::now() - start < std::chrono::seconds(10))
                    { --frame; return; }
                    const auto main = MphRead::Entities::PlayerEntity::Main();
                    const auto playing = MphRead::Entities::LoadFlags::Active | MphRead::Entities::LoadFlags::Spawned;
                    if (!main || (main->LoadFlags() & playing) != playing || main->Health() == 0)
                        throw std::runtime_error("FPS fixture lost the active main player.");
                    QDir().mkpath(dir);
                    const auto size = window.FramebufferSize();
                    const auto path = QDir(dir).filePath(QStringLiteral("fps-active.png")).toStdString();
                    if (!MphRead::Mods::ScreenCapture::SaveWindow(size.X, size.Y, path))
                        throw std::runtime_error("FPS fixture could not capture its active match.");
                    std::cout << "[fps fixture] captured active match: " << path << '\n';
                    window.Close();
                }
                return;
            }
            // Enter through the real Fire input before measuring or switching
            // the match. A loaded room with bots is still the main player's
            // "Press fire to begin" screen until that input is received.
            if (frame == 151)
            {
                static const auto spawnStart = std::chrono::steady_clock::now();
                static std::optional<std::chrono::steady_clock::time_point> matchStart;
                const auto now = std::chrono::steady_clock::now();
                const auto main = MphRead::Entities::PlayerEntity::Main();
                const auto playing = MphRead::Entities::LoadFlags::Active | MphRead::Entities::LoadFlags::Spawned;
                const bool active = window.HasScene() && main
                    && (main->LoadFlags() & playing) == playing && main->Health() > 0;
                if (!active)
                {
                    if (now - spawnStart > std::chrono::seconds(5))
                        throw std::runtime_error("Shell check main player did not spawn after Fire.");
                    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now - spawnStart).count();
                    const bool down = (milliseconds / 100) % 2 == 0;
                    auto* target = MphRead::Qt::GameWindow();
                    const QPointF centre(target->width() / 2, target->height() / 2);
                    QMouseEvent fire(down ? QEvent::MouseButtonPress : QEvent::MouseButtonRelease,
                        centre, QPointF(target->mapToGlobal(centre.toPoint())), ::Qt::LeftButton,
                        down ? ::Qt::LeftButton : ::Qt::NoButton, ::Qt::NoModifier);
                    QCoreApplication::sendEvent(target, &fire);
                    --frame;
                    return;
                }
                if (!matchStart)
                {
                    matchStart = now;
                    auto* target = MphRead::Qt::GameWindow();
                    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(), QPointF(),
                        ::Qt::LeftButton, ::Qt::NoButton, ::Qt::NoModifier);
                    QCoreApplication::sendEvent(target, &release);
                    std::cout << "[shellcheck] main player spawned; simulation_frame="
                        << window.Scene().FrameCount() << '\n';
                }
                if (qEnvironmentVariableIntValue("FRUITY_FPSCHECK") != 0
                    && now - *matchStart < std::chrono::seconds(10))
                {
                    --frame;
                    return;
                }
            }
            const bool switchCheck = qEnvironmentVariableIntValue("FRUITY_SWITCHCHECK") != 0
                || qEnvironmentVariableIntValue("FP_QT_DEMO_SWITCH") != 0;
            static MphRead::Scene* keptScene = nullptr;
            static std::uint64_t beforeSimulation = 0;
            static MphRead::NativeRuntime::Rhi::GraphicsBackend expected;
            static OpenTK::Mathematics::Vector2i beforeSize{};
            const auto require = [](bool condition, const char* message)
            {
                if (!condition) { ++Shell::ShotMissCounter(); std::cout << "[switchcheck] FAIL: " << message << '\n'; }
            };
            const auto switchRenderer = [&]
            {
                using namespace MphRead::NativeRuntime::Rhi;
                const bool vulkan = SceneDevice().GetBackend() == GraphicsBackend::Vulkan;
                expected = vulkan ? GraphicsBackend::OpenGl : GraphicsBackend::Vulkan;
                beforeSize = window.FramebufferSize();
                if (window.HasScene()) { keptScene = &window.Scene(); beforeSimulation = keptScene->FrameCount(); }
                MphRead::Qt::SettingsModel settings;
                settings.SetInGame(window.HasScene());
                auto* display = static_cast<MphRead::Qt::RowModel*>(settings.Display());
                auto* row = display->Find(QStringLiteral("renderer"));
                require(row != nullptr, "renderer setting exists");
                if (row)
                {
                    const int index = static_cast<int>(row - display->Rows().data());
                    display->setIndex(index, vulkan ? 0 : 1);
                    require(settings.save(), "Settings Apply succeeds");
                }
                Shell::CloseMenu();
                std::cout << "[switchcheck] requested " << (vulkan ? "opengl" : "vulkan") << '\n';
            };
            const auto verifySwitch = [&]
            {
                require(MphRead::NativeRuntime::Rhi::SceneDevice().GetBackend() == expected, "backend changed");
                require(MphRead::Qt::GameWindow() && MphRead::Qt::GameWindow()->isVisible(), "window visible");
                const auto size = window.FramebufferSize();
                require(size.X == beforeSize.X && size.Y == beforeSize.Y, "geometry kept");
                if (keptScene)
                {
                    require(window.HasScene() && &window.Scene() == keptScene, "same match scene kept");
                    require(window.HasScene() && window.Scene().FrameCount() > beforeSimulation, "simulation continued after resume");
                }
            };
            const auto shoot = [&](const char* name)
            {
                const OpenTK::Mathematics::Vector2i size = window.FramebufferSize();
                QDir().mkpath(dir);
                const QString path = QDir(dir).filePath(QLatin1String(name) + QStringLiteral(".png"));
                const bool saved = MphRead::Mods::ScreenCapture::SaveWindow(size.X, size.Y, path.toStdString());
                if (!saved) ++Shell::ShotMissCounter();
                const QImage image(path);
                bool varied = false;
                if (!image.isNull())
                    for (int y = 0; y < image.height() && !varied; y += 17)
                        for (int x = 0; x < image.width(); x += 17)
                            if (image.pixel(x, y) != image.pixel(0, 0)) { varied = true; break; }
                require(varied, "capture contains rendered content");
                std::cout << "[demo] " << path.toStdString() << (saved ? "" : " (not written)") << '\n';
            };
            if (frame == 80 && switchCheck) { shoot("window-front-before"); switchRenderer(); }
            else if (frame == 140 && switchCheck) { verifySwitch(); shoot("window-front-switched"); }
            else if (frame == 150)
            {
                shoot("window-start");
                if (!g_rooms.empty())
                {
                    LaunchPlan::Init init;
                    init.Kind = LaunchKind::Offline;
                    init.RoomKey = qEnvironmentVariable("FRUITY_SHOT_ROOM").isEmpty() ? g_rooms.front()
                        : qEnvironmentVariable("FRUITY_SHOT_ROOM").toStdString();
                    init.Mode = static_cast<MphRead::GameMode>(3);
                    init.Hunter = static_cast<MphRead::Hunter>(0);
                    init.Bots = 7;
                    init.BotLevel = 1;
                    if (qEnvironmentVariableIntValue("FRUITY_FPSCHECK") != 0)
                    {
                        init.Hunter = Portable::LauncherPrefs::LastHunter();
                        init.Bots = Portable::LauncherPrefs::Bots();
                        init.BotLevel = Portable::LauncherPrefs::BotLevel();
                    }
                    Decided(LaunchPlan(init));
                }
            }
            else if (frame == 500)
            {
                shoot("window-match");
                require(window.HasScene(), "match loaded");
                require(Shell::OpenPauseMenu(), "pause opens");
            }
            else if (frame == 530)
            {
                shoot("window-pause");
                // FP_QT_DEMO_SWITCH=1: the in-place renderer switch, with the
                // match and the pause menu kept, then the other renderer shot.
                if (switchCheck)
                {
                    switchRenderer();
                }
                else
                {
                    Shell::RequestEndMatch();
                }
            }
            else if ((frame == 680 || frame == 880 || frame == 1080) && switchCheck)
            {
                verifySwitch();
                shoot(frame == 680 ? "window-switch-1" : frame == 880 ? "window-switch-2" : "window-switch-3");
                if (frame < 1080) require(Shell::OpenPauseMenu(), "pause reopens");
                else Shell::RequestEndMatch();
            }
            else if ((frame == 730 || frame == 930) && switchCheck) switchRenderer();
            else if ((frame == 620 && !switchCheck) || (frame == 1160 && switchCheck))
            {
                require(!window.HasScene() && g_bridge->Page() == QStringLiteral("front"), "returned to front screen");
                shoot("window-return");
                std::cout << "[switchcheck] " << (Shell::ShotMisses() == 0 ? "PASS" : "FAIL") << '\n';
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

    std::int32_t& Shell::ShotMissCounter() noexcept
    {
        static std::int32_t misses = 0;
        return misses;
    }

    std::int32_t Shell::ShotMisses() noexcept
    {
        return ShotMissCounter();
    }

    void Shell::RequestRenderer(MphRead::NativeRuntime::Rhi::SceneBackendRequest request, bool fromSettings)
    {
        (void)fromSettings;
        if (!g_active || g_window == nullptr)
        {
            return;
        }
        // In place, as the Avalonia shell: the window is remade on the other
        // renderer at the next frame and the match carries on. The menus are
        // released before the old device goes and rebuilt on the new one
        // (InstallRendererSwitchHooks).
        g_window->RequestRendererSwitch(request);
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

        // The menus draw into this window, on whichever renderer it has.
        MphRead::NativeRuntime::Rhi::SceneBackendNeedsWindowUi(true);
        // A renderer switch remakes the window and its device in place: the
        // menus' Qt Quick scene is bound to both, so it goes before the old
        // device does and is made again on the new one.
        MphRead::RenderWindow::BeforeRendererSwitch = []()
        {
            g_host.reset();
            MphRead::Mods::Render::UiOverlay::Release();
        };
        MphRead::RenderWindow::AfterRendererSwitch = [](MphRead::RenderWindow&)
        {
            if (QWindow* const gameWindow = MphRead::Qt::GameWindow())
            {
                g_host = std::make_unique<MphRead::Qt::UiHost>(*gameWindow, *g_bridge);
            }
        };
        MphRead::RenderWindow::LogCreatingWindow();
        std::unique_ptr<MphRead::RenderWindow> window;
        bool ran = false;
        const auto previousReporter = MphRead::RenderWindow::ReportRendererSwitchFailure;
        if (!qEnvironmentVariable("FP_QT_DEMO").isEmpty())
        {
            MphRead::RenderWindow::ReportRendererSwitchFailure = [](std::exception_ptr error, bool recovered)
            {
                ++Shell::ShotMissCounter();
                std::cout << "[switchcheck] FAIL: " << MphRead::NativeRuntime::ExceptionMessage(error)
                    << "; recovered=" << recovered << '\n';
            };
        }
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
        MphRead::RenderWindow::ReportRendererSwitchFailure = previousReporter;
        MphRead::RenderWindow::BeforeRendererSwitch = {};
        MphRead::RenderWindow::AfterRendererSwitch = {};
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
        const bool want = g_window != nullptr && g_window->HasScene()
            && MphRead::Mods::EndScreen::Available() && !g_menuOpen;
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
        qputenv("FP_QT_DEMO", QByteArray::fromStdString(directory));
    }

    void Shell::AfterDraw(MphRead::RenderWindow& window)
    {
        MphRead::Mods::Diagnostics::LauncherWindowCheck::AfterDraw(window);
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
            const bool captured = MphRead::Qt::UiCapture::Run(shots.toStdString()) == 0;
            MphRead::Qt::ShutdownApplication();
            return captured;
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
