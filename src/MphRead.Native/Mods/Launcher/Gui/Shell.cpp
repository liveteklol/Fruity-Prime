#include "Shell.hpp"
#include <filesystem>
#include <fstream>
#include <iterator>
#include "../../../NativeRuntime/Rhi/SceneBackend.hpp"
#include "../../../NativeRuntime/System/ErrorDialog.hpp"

#include "DeckButton.hpp"
#include "DeckTile.hpp"
#include "Rows.hpp"
#include "../../../Entities/Players/PlayerEntity.hpp"
#include "../../../Entities/PlayerSpawnEntity.hpp"
#include "../../../Entities/BombEntity.hpp"
#include "../../../Formats/Effects.hpp"
#include "../../../Metadata/Metadata.hpp"
#include "EndPanelView.hpp"
#include "InGameMenu.hpp"
#include "StartScreen.hpp"
#include "UiMark.hpp"
#include "UiSurface.hpp"
#include "../../Diagnostics/LauncherWindowCheck.hpp"
#include "../../Diagnostics/RhiReadbackCheck.hpp"
#include "../../Diagnostics/RendererSwitchWitness.hpp"
#include "../../../Export/Images.hpp"
#include "../../../GameState.hpp"
#include "../../../Menu.hpp"
#include "../../../Renderer.hpp"
#include "../../../Read.hpp"
#include "../../../NativeRuntime/OpenTK/GLFW.hpp"
#include "../../../NativeRuntime/System/Globalization.hpp"
#include "../../../NativeRuntime/System/IO.hpp"
#include "../../../NativeRuntime/System/Managed.hpp"
#include "../../../NativeRuntime/System/Runtime.hpp"
#include "../../Branding.hpp"
#include "../../DebugLog.hpp"
#include "../../EndScreen.hpp"
#include "../../GameSettings.hpp"
#include "../../RenderOptions.hpp"
#include "../../MapPick.hpp"
#include "../../Network/NetHostSession.hpp"
#include "../../Network/NetSession.hpp"
#include "../../PauseMenu.hpp"
#include "../../Render/LauncherHunter.hpp"
#include "../../Render/FrameTiming.hpp"
#include "../../Render/LauncherPhoto.hpp"
#include "../../../NativeRuntime/Rhi/OpenGL/OpenGlDevice.hpp"
#include "../../Render/MapThumbnail.hpp"
#include "../../Render/LauncherNoise.hpp"
#include "../../Render/UiOverlay.hpp"
#include "../../ScreenCapture.hpp"
#include "../../ThumbnailGenerator.hpp"
#include "../../WindowGeometry.hpp"
#include "../../WindowMode.hpp"
#include "../Portable/GameFiles.hpp"
#include "../Portable/LauncherPrefs.hpp"
#include "../Portable/MatchStart.hpp"
#include "../Portable/NativeFilePicker.hpp"
#include "../../../NativeRuntime/System/ExceptionText.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <iomanip>
#include <iostream>
#include <locale>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#if !defined(__ANDROID__)
#include <GLFW/glfw3.h>
#endif
#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

namespace MphRead::Mods::Launcher::Gui
{
    namespace
    {
        std::unique_ptr<Diagnostics::RendererSwitchWitness> g_switchWitness;
        bool g_switchWitnessSelfChecked = false;
        int g_stressCycles = 0, g_stressCycle = 0, g_stressReleases = 0, g_stressResizes = 0;
        bool g_stressFinished = false, g_stressClosed = false;
        std::uint64_t g_stressResizeWaits = 0;
        int g_stressScale = 100;
        OpenTK::Mathematics::Vector2i g_stressOriginalSize;
        std::uint64_t g_switchFixtureReadyFrame = 0;
        std::optional<std::chrono::steady_clock::time_point> g_fpsCheckUntil;
        bool WaitForFpsMeasurement()
        {
            if (!std::getenv("FRUITY_FPSCHECK")) return false;
            const auto now = std::chrono::steady_clock::now();
            if (!g_fpsCheckUntil) g_fpsCheckUntil = now + std::chrono::seconds(4);
            if (now < *g_fpsCheckUntil) return true;
            g_fpsCheckUntil.reset(); return false;
        }
        bool g_switchFailureArmed = false;
        int g_switchFailureCycle = -1, g_switchRecoveryReports = 0;
        std::optional<NativeRuntime::Rhi::BackendFailure> g_switchInjectedFailure;
        bool g_switchRecoveryFailureArmed = false;
        std::optional<NativeRuntime::Rhi::BackendFailure> g_switchInjectedRecoveryFailure;
        std::weak_ptr<MphRead::Model> g_textureOnlySource{};
        bool DoubleFailureCheck()
        {
            const auto* mode = std::getenv("FRUITY_SWITCHCHECK_FAILURES");
            return std::getenv("FRUITY_SWITCHCHECK") && mode && std::string_view(mode) == "double";
        }
        std::optional<NativeRuntime::Rhi::BackendFailure> FailurePayload(std::exception_ptr failure)
        {
            namespace Rhi = MphRead::NativeRuntime::Rhi;
            try { std::rethrow_exception(failure); }
            catch (const Rhi::BackendError& error) { return error.Failure(); }
            catch (const Rhi::SceneBackendUnavailable& error) { return error.Failure(); }
            catch (...) { return {}; }
        }
        bool SameFailure(const std::optional<NativeRuntime::Rhi::BackendFailure>& a,
            const std::optional<NativeRuntime::Rhi::BackendFailure>& b)
        {
            return a && b && a->backend == b->backend && a->kind == b->kind
                && a->nativeCode == b->nativeCode && a->message == b->message;
        }
    }
    bool Shell::_active = false;
    MphRead::RenderWindow* Shell::_window = nullptr;
    std::shared_ptr<StartScreen> Shell::_front{};
    std::shared_ptr<InGameMenu> Shell::_menu{};
    std::shared_ptr<MphRead::MenuSettings> Shell::_settings
        = std::make_shared<MphRead::MenuSettings>();
    std::vector<std::string> Shell::_rooms{};
    std::optional<MphRead::Mods::Launcher::LaunchPlan> Shell::_pending{};
    bool Shell::_endMatch = false;
    bool Shell::_quit = false;
    std::int32_t Shell::_basisWidth = 0;
    std::int32_t Shell::_basisHeight = 0;
    std::shared_ptr<EndPanelView> Shell::_endPanel{};
    std::optional<MphRead::Mods::Launcher::LaunchPlan> Shell::_played{};
    std::optional<std::string> Shell::_shotDirectory{};
    std::int32_t Shell::_shotStep = 0;
    std::int32_t Shell::_shotWait = 0;
    std::int32_t Shell::_shotMisses = 0;
    std::uint64_t Shell::_shotPreviewGeneration = 0;

    namespace
    {
        std::chrono::steady_clock::time_point g_shotUiReadyAt{};
        using Keys = OpenTK::Windowing::GraphicsLibraryFramework::Keys;
        using MouseButton = OpenTK::Windowing::GraphicsLibraryFramework::MouseButton;
        using MphRead::Mods::Launcher::LaunchKind;
        using MphRead::Mods::Launcher::LaunchPlan;
        namespace Av = ::MphRead::NativeRuntime::Avalonia;

        [[nodiscard]] std::string ExceptionMessage(std::exception_ptr exception)
        {
            return MphRead::NativeRuntime::ExceptionMessage(exception);
        }

        [[nodiscard]] std::optional<std::string> ExceptionStackTrace(
            std::exception_ptr exception)
        {
            (void)exception;
            return std::nullopt;
        }

        [[nodiscard]] std::string Number(double value)
        {
            std::ostringstream text;
            text.imbue(std::locale::classic());
            text << std::fixed << std::setprecision(4) << value;
            std::string result = text.str();
            while (!result.empty() && result.back() == '0')
            {
                result.pop_back();
            }
            if (!result.empty() && result.back() == '.')
            {
                result.pop_back();
            }
            return result;
        }

        [[nodiscard]] std::string SizeText(OpenTK::Mathematics::Vector2i size)
        {
            return std::to_string(size.X) + "x" + std::to_string(size.Y);
        }

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
    }

    bool Shell::Active() noexcept
    {
        return _active;
    }

    bool Shell::UiVisible()
    {
        const std::shared_ptr<UiSurface> surface = UiSurface::Current();
        return surface != nullptr && surface->Visible();
    }

    MphRead::RenderWindow* Shell::Window() noexcept
    {
        return _window;
    }

    bool Shell::EndPanelUp() noexcept
    {
        return _endPanel != nullptr;
    }

    bool Shell::CanPlayAnother()
    {
        return _active && !_pending.has_value() && _played.has_value()
            && _played->Kind() == LaunchKind::Offline;
    }

    std::int32_t& Shell::ShotMissCounter() noexcept
    {
        return _shotMisses;
    }

    std::int32_t Shell::ShotMisses() noexcept
    {
        return _shotMisses + (g_stressCycles && (!g_stressFinished || !g_stressClosed) ? 1 : 0);
    }

    void Shell::PublishNativeHandle(MphRead::RenderWindow& window)
    {
        if (!MphRead::NativeRuntime::IsWindows())
        {
            return;
        }
        try
        {
#if defined(_WIN32)
            NativeFilePicker::Owner(::glfwGetWin32Window(
                static_cast<GLFWwindow*>(window.WindowPtr())));
#endif
        }
        catch (...)
        {
            // An owner is an improvement, not a requirement.
        }
    }

    bool Shell::Run()
    {
        if (UiSurface::Ensure() == nullptr)
        {
            return false;
        }
        LauncherPrefs::Load();
        MphRead::Mods::Render::LauncherPhoto::Enabled(true);
        if (GameFiles::Ready())
        {
            GameFiles::ApplyPaths();
            MphRead::Mods::ThumbnailGenerator::EnsureCustomPreviews();
        }
        if (!MphRead::Mods::WindowMode::StartupForced())
        {
            MphRead::Mods::WindowMode::Startup(LauncherPrefs::WindowMode());
        }

        // The launcher draws into this window: a Vulkan one needs Skia's.
        MphRead::NativeRuntime::Rhi::SceneBackendNeedsWindowUi(true);
        InstallRendererSwitchHooks();
        std::unique_ptr<MphRead::RenderWindow> window;
        bool ran = false;
        MphRead::RenderWindow::LogCreatingWindow();
        try
        {
            window = std::make_unique<MphRead::RenderWindow>(true);
            PublishNativeHandle(*window);
            _window = window.get();
            _active = true;
            ShowFrontScreen();
            window->Run();
            ran = true;
        }
        catch (const MphRead::NativeRuntime::Rhi::SceneBackendUnavailable& unavailable)
        {
            // Asked for by name and not there: said to the person, never
            // replaced behind their back by the other backend.
            MphRead::Mods::DebugLog::Exception("launcher", std::current_exception());
            MphRead::NativeRuntime::ShowErrorDialog(std::string(MphRead::Mods::Branding::Name),
                std::string(unavailable.what())
                    + "\n\nChoose OpenGL or Auto under Settings > Game > Renderer, or start with -rhi opengl.");
        }
        catch (const MphRead::NativeRuntime::Rhi::SceneBackendRecoveryFailed& failure)
        {
            if (DoubleFailureCheck())
            {
                // Expected fatal boundary: no frame may use the failed devices.
                // Destroy the real window/scene before checking shutdown validation.
                const bool preserved = SameFailure(FailurePayload(failure.Incoming()), g_switchInjectedFailure)
                    && SameFailure(FailurePayload(failure.Recovery()), g_switchInjectedRecoveryFailure)
                    && g_switchRecoveryReports == 3 && !g_switchRecoveryFailureArmed;
                _window = nullptr;
                window.reset();
                const auto validationErrors = MphRead::NativeRuntime::Rhi::SceneValidationErrors();
                const bool sourceReleased = g_textureOnlySource.expired();
                const bool released = sourceReleased && validationErrors == 0;
                if (!preserved || !released) ++_shotMisses;
                std::cout << "[switch failure] double failure " << (preserved && released ? "PASS" : "FAIL")
                    << "; both typed errors preserved=" << (preserved ? "yes" : "NO")
                    << "; scene released=" << (sourceReleased ? "yes" : "NO")
                    << "; shutdown validation=" << validationErrors << '\n';
                ran = true; // Diagnostic reached its expected terminal failure.
            }
            else
            {
                std::cout << "The window could not be reopened: " << failure.what() << '\n';
                MphRead::Mods::DebugLog::Exception("launcher", std::current_exception());
                MphRead::NativeRuntime::ShowErrorDialog(std::string(MphRead::Mods::Branding::Name), failure.what());
            }
        }
        catch (const std::exception&)
        {
            const std::exception_ptr exception = std::current_exception();
            std::cout << "The window could not be opened: " << ExceptionMessage(exception) << '\n';
            MphRead::Mods::DebugLog::Exception("launcher", exception);
        }


        _active = false;
        _window = nullptr;
        _pending.reset();
        _endMatch = false;
        _quit = false;
        MphRead::Mods::Network::NetSession::Stop();
        MphRead::Mods::Network::NetHostSession::Stop();
        if (window != nullptr)
        {
            // UiOverlay owns Phase 4 VBO/IBO objects. Release them while the
            // launcher context is still alive, before RenderWindow destruction.
            MphRead::Mods::Render::UiOverlay::Release();
        }
        window.reset();
        return ran;
    }

    void Shell::RequestRenderer(MphRead::NativeRuntime::Rhi::SceneBackendRequest request, bool fromSettings)
    {
        (void)fromSettings;
        if (!_active || _window == nullptr) return;
        // In place: the match, the screens and the pointer carry on.
        _window->RequestRendererSwitch(request);
    }

    void Shell::InstallRendererSwitchHooks()
    {
        MphRead::RenderWindow::ObserveRendererSwitchStage = {};
        MphRead::RenderWindow::ReportRendererSwitchFailure = {};
        if (std::getenv("FRUITY_SWITCHCHECK") && std::getenv("FRUITY_SWITCHCHECK_FAILURES"))
        {
            MphRead::RenderWindow::ObserveRendererSwitchStage = [](MphRead::RenderWindow&, MphRead::RenderWindow::RendererSwitchStage stage)
            {
                using Stage = MphRead::RenderWindow::RendererSwitchStage;
                const std::array checkpoints{Stage::BeforeWindow, Stage::Presentation, Stage::Resources};
                namespace Rhi = MphRead::NativeRuntime::Rhi;
                if (g_switchRecoveryFailureArmed && stage == Stage::BeforeWindow)
                {
                    g_switchRecoveryFailureArmed = false;
                    g_switchInjectedRecoveryFailure = Rhi::BackendFailure{Rhi::SelectedSceneBackend(),
                        Rhi::BackendErrorKind::SurfaceLost, 999, "Injected recovery window failure"};
                    throw Rhi::BackendError(*g_switchInjectedRecoveryFailure);
                }
                if (!g_switchFailureArmed || stage != checkpoints.at(g_switchFailureCycle)) return;
                g_switchFailureArmed = false; // Recovery must use the real backend without another fault.
                if (DoubleFailureCheck() && g_switchFailureCycle == 2) g_switchRecoveryFailureArmed = true;
                const auto backend = Rhi::SelectedSceneBackend();
                const auto kind = g_switchFailureCycle == 0 ? Rhi::BackendErrorKind::Unsupported
                    : g_switchFailureCycle == 1 ? Rhi::BackendErrorKind::DeviceLost : Rhi::BackendErrorKind::OutOfMemory;
                const std::int64_t code = g_switchFailureCycle == 0 ? 321
                    : backend == Rhi::GraphicsBackend::Vulkan ? (g_switchFailureCycle == 1 ? -4 : -2)
                    : (g_switchFailureCycle == 1 ? 0x0507 : 0x0505);
                g_switchInjectedFailure = Rhi::BackendFailure{backend, kind, code,
                    "Injected renderer switch failure at checkpoint " + std::to_string(g_switchFailureCycle)};
                std::cout << "[switch failure] injected stage=" << g_switchFailureCycle << "; native=" << code << '\n';
                if (stage == Stage::BeforeWindow)
                    throw Rhi::SceneBackendUnavailable(g_switchInjectedFailure->message, *g_switchInjectedFailure);
                throw Rhi::BackendError(*g_switchInjectedFailure);
            };
            MphRead::RenderWindow::ReportRendererSwitchFailure = [](std::exception_ptr failure, bool recovered)
            {
                const bool preserved = SameFailure(FailurePayload(failure), g_switchInjectedFailure);
                const bool expectedRecovered = !(DoubleFailureCheck() && g_switchFailureCycle == 2);
                if (recovered != expectedRecovered || !preserved) ++Shell::ShotMissCounter();
                ++g_switchRecoveryReports;
                std::cout << "[switch failure] recovery=" << (recovered ? "PASS" : expectedRecovered ? "FAIL" : "failed as expected")
                    << "; original typed error=" << (preserved ? "PASS" : "FAIL") << '\n';
            };
        }
        if (g_stressCycles)
        {
            MphRead::RenderWindow::ObserveRendererSwitchStage = [](MphRead::RenderWindow&, MphRead::RenderWindow::RendererSwitchStage stage)
            {
                const bool final = stage == MphRead::RenderWindow::RendererSwitchStage::FinalRelease;
                if (!final && stage != MphRead::RenderWindow::RendererSwitchStage::ReleasedResources) return;
                auto& device = MphRead::NativeRuntime::Rhi::SceneDevice();
                device.TrimCaches(); device.WaitIdle(); // Explicit renderer transition boundary.
                const auto stats = device.Statistics();
                const auto errors = MphRead::NativeRuntime::Rhi::SceneValidationErrors();
                std::cout << "[render stress] released cycle=" << g_stressCycle << "; backend="
                    << MphRead::NativeRuntime::Rhi::SceneBackendName(device.GetBackend())
                    << "; live=" << stats.LiveObjects() << "; retired=" << stats.Retired
                    << "; errors=" << errors << '\n';
                if (stats.LiveObjects() || stats.Retired || errors || device.DrainErrors())
                {
                    ++Shell::ShotMissCounter();
                    throw std::runtime_error("Renderer stress left resources or graphics errors at the transition boundary.");
                }
                if (final) { g_stressClosed = true; std::cout << "[render stress] final release PASS; live=0; retired=0; errors=0\n"; }
                else ++g_stressReleases;
            };
        }
        MphRead::RenderWindow::ObserveRendererSwitch = [](MphRead::RenderWindow& window, bool before)
        {
            if (!std::getenv("FRUITY_SWITCHCHECK") && !g_stressCycles) return;
            if (before)
            {
                g_switchWitness.reset();
                if (window.HasScene())
                {
                    g_switchWitness = std::make_unique<Diagnostics::RendererSwitchWitness>(window.Scene());
                    if (!g_switchWitnessSelfChecked && std::getenv("FRUITY_SWITCHCHECK_WITNESS_SELFTEST"))
                    {
                        g_switchWitness->CheckRejections(window.Scene());
                        g_switchWitnessSelfChecked = true;
                    }
                }
            }
            else if (g_switchWitness)
            {
                if (!window.HasScene()) throw std::runtime_error("Renderer switch destroyed the witnessed match.");
                g_switchWitness->Validate(window.Scene());
                g_switchWitness.reset();
            }
        };
        MphRead::RenderWindow::BeforeRendererSwitch = []() { ReleaseWindowGpu(); };
        MphRead::RenderWindow::AfterRendererSwitch = [](MphRead::RenderWindow& window)
        {
            PublishNativeHandle(window);
            NotePointerBasis(window);
            if (const auto surface = UiSurface::Current()) surface->Invalidate();
        };
    }

    void Shell::ShowSettings()
    {
        if (_front != nullptr) _front->OpenSettings();
    }

    void Shell::ReleaseWindowGpu()
    {
        // While the old window's context or device is still the current one.
        if (const auto surface = UiSurface::Current()) surface->ReleaseGpu();
        MphRead::Mods::Render::UiOverlay::Release();
        MphRead::Mods::Render::LauncherPhoto::Release();
        MphRead::Mods::Render::LauncherNoise::Release();
        MphRead::Mods::Render::MapThumbnail::Clear();
    }

    void Shell::BeforeFrame(MphRead::RenderWindow& window)
    {
        if (!_active)
        {
            return;
        }
        if (_quit)
        {
            _quit = false;
            window.Close();
            return;
        }
        if (window.HasScene() && MphRead::Mods::Network::NetSession::PersistentLobby()
            && MphRead::Mods::Network::NetSession::IsInLobby() && !_endMatch)
        {
            EndNetworkMatchToLobby(window);
        }
        if (window.HasScene() && (MphRead::Mods::Network::NetSession::Refused()
            || MphRead::Mods::Network::NetSession::SessionTimedOut()))
        {
            _endMatch = true;
        }
        if (_endMatch)
        {
            _endMatch = false;
            EndMatch(window);
        }
        if (_pending.has_value())
        {
            LaunchPlan plan = *_pending;
            _pending.reset();
            StartMatch(window, std::move(plan));
        }
    }

    void Shell::TickUi(MphRead::RenderWindow& window)
    {
        const std::shared_ptr<UiSurface> surface = UiSurface::Current();
        if (surface == nullptr)
        {
            MphRead::Mods::Render::UiOverlay::Visible(false);
            return;
        }
        const OpenTK::Mathematics::Vector2i framebuffer = window.FramebufferSize();
        surface->Resize(framebuffer.X, framebuffer.Y);
        NotePointerBasis(window);
        if (!surface->Visible())
        {
            MphRead::Mods::Render::UiOverlay::Visible(false);
            return;
        }
        surface->Tick();
    }

    void Shell::NotePointerBasis(MphRead::RenderWindow& window)
    {
        const OpenTK::Mathematics::Vector2i framebuffer = window.FramebufferSize();
        const std::int32_t fw = framebuffer.X;
        const std::int32_t fh = framebuffer.Y;
        if (fw == _basisWidth && fh == _basisHeight)
        {
            return;
        }
        _basisWidth = fw;
        _basisHeight = fh;
        const OpenTK::Mathematics::Vector2i client = window.ClientSize();
        const double sx = client.X > 0 ? fw / static_cast<double>(client.X) : 1;
        const double sy = client.Y > 0 ? fh / static_cast<double>(client.Y) : 1;
        std::string message = "pointer basis: framebuffer " + SizeText(framebuffer)
            + ", client " + SizeText(client) + ", pointer scaled by "
            + Number(sx) + "x" + Number(sy)
            + (std::abs(sx - 1) > 0.001 || std::abs(sy - 1) > 0.001
                ? " -- not 1, so clicks land off by that fraction" : "");
        MphRead::Mods::DebugLog::Line("ui", message);
    }

    void Shell::TickEndPanel()
    {
        // EndScreen::Available() is backed by process-global game state, which
        // outlives RenderWindow::EndScene(). A results panel cannot own the UI
        // once the match scene itself is gone.
        const bool want = _window != nullptr && _window->HasScene()
            && MphRead::Mods::EndScreen::Available() && _menu == nullptr;
        if (want && !EndPanelUp())
        {
            const std::shared_ptr<UiSurface> surface = UiSurface::Ensure();
            if (surface == nullptr)
            {
                return;
            }
            std::shared_ptr<EndPanelView> panel = std::make_shared<EndPanelView>();
            _endPanel = panel;
            MphRead::Mods::EndScreen::PanelUp(true);
            surface->Show(panel);
            return;
        }
        if (!want && EndPanelUp())
        {
            CloseEndPanel();
            return;
        }
        if (_endPanel != nullptr)
        {
            _endPanel->Refresh();
        }
    }

    void Shell::CloseEndPanel()
    {
        // UiSurface is shared by the results panel, pause/settings screens and
        // launcher. Only hide it when the results panel still owns the current
        // view. A delayed results teardown must never hide a replacement view
        // that has already been shown during the match -> launcher transition.
        const std::shared_ptr<EndPanelView> panel = std::move(_endPanel);
        MphRead::Mods::EndScreen::PanelUp(false);
        if (panel == nullptr)
        {
            return;
        }
        const std::shared_ptr<UiSurface> surface = UiSurface::Current();
        if (surface != nullptr && surface->View() == panel)
        {
            surface->Hide();
        }
    }

    void Shell::ShowFrontScreen()
    {
        const std::shared_ptr<UiSurface> surface = UiSurface::Current();
        if (surface == nullptr)
        {
            return;
        }
        MphRead::Mods::PauseMenu::Reset();
        _settings = MphRead::GameState::LoadSettings();
        MphRead::Mods::GameSettings::Apply(_settings);
        LauncherPrefs::Load();
        if (_rooms.empty() && GameFiles::Ready())
        {
            _rooms = MphRead::Mods::ThumbnailGenerator::MultiplayerRooms();
        }
        if (_window != nullptr)
        {
            _window->Title(std::string(MphRead::Mods::Branding::Name));
        }
        if (_front == nullptr)
        {
            Hunters::Reroll();
            _front = StartScreen::Create(_settings, _rooms);
            _front->Done += [](StartScreen&, LaunchPlan plan) { Decided(std::move(plan)); };
            _front->MatchRequested += [](StartScreen&, LaunchPlan plan) { Decided(std::move(plan)); };
        }
        else
        {
            _front->Reset();
        }
        surface->Show(_front);
    }

    void Shell::Decided(LaunchPlan plan)
    {
        // FRUITY_SHOT_ROOM=KEY: the shell loop plays this room, so two builds
        // (or the two backends) photograph the same match.
        if (const char* room = std::getenv("FRUITY_SHOT_ROOM"); room != nullptr && _shotDirectory.has_value()
            && plan.Kind() == LaunchKind::Offline)
        {
            LaunchPlan::Init init{};
            init.Kind = plan.Kind();
            init.Lobby = plan.Lobby();
            init.Hunter = plan.Hunter();
            init.RoomKey = std::string(room);
            init.Mode = plan.Mode();
            init.Bots = plan.Bots();
            init.BotLevel = plan.BotLevel();
            init.Port = plan.Port();
            init.PlayerName = plan.PlayerName();
            init.SaveSlot = plan.SaveSlot();
            init.NewGame = plan.NewGame();
            init.DemoPath = plan.DemoPath();
            plan = LaunchPlan(init);
        }
        if (plan.Kind() == LaunchKind::None)
        {
            RequestQuit();
            return;
        }
        _pending = std::move(plan);
    }

    void Shell::PlayAnother(std::string roomKey)
    {
        if (!CanPlayAnother() || MphRead::NativeRuntime::StringIsNullOrWhiteSpace(roomKey)
            || !_played.has_value())
        {
            return;
        }
        _endMatch = true;
        _pending = WithRoomKey(*_played, std::move(roomKey));
    }

    void Shell::StartMatch(MphRead::RenderWindow& window, LaunchPlan plan)
    {
        _played = plan;
        if (_front != nullptr)
        {
            _front->SuspendLobby();
        }
        const std::shared_ptr<UiSurface> surface = UiSurface::Current();
        if (surface != nullptr)
        {
            surface->Hide();
        }
        try
        {
            if (!MphRead::Mods::Launcher::MatchStart::Begin(window, _settings, plan))
            {
                MphRead::Mods::Network::NetSession::ReportMatchLoadFailed(
                    "The map could not be loaded.");
                EndMatch(window);
            }
        }
        catch (const std::exception&)
        {
            const std::exception_ptr exception = std::current_exception();
            std::cout << '\n';
            std::cout << "The game could not start: " << ExceptionMessage(exception) << '\n';
            const std::optional<std::string> stack = ExceptionStackTrace(exception);
            std::cout << (stack.has_value() ? *stack : std::string{}) << '\n';
            MphRead::Mods::DebugLog::Line("crash", "the match could not start");
            MphRead::Mods::DebugLog::Exception("crash", exception);
            MphRead::Mods::Network::NetSession::ReportMatchLoadFailed(
                ExceptionMessage(exception));
            EndMatch(window);
        }
    }

    void Shell::EndNetworkMatchToLobby(MphRead::RenderWindow& window)
    {
        CloseMenu();
        CloseEndPanel();
        window.EndScene();
        MphRead::Mods::Launcher::MatchStart::AfterMatch();
        MphRead::Mods::Network::NetSession::ResetMatchState();
        MphRead::Mods::PauseMenu::Reset();
        if (_front != nullptr)
        {
            const std::shared_ptr<UiSurface> surface = UiSurface::Current();
            if (surface != nullptr)
            {
                surface->Show(_front);
            }
            _front->ResumeLobby();
        }
    }

    void Shell::EndMatch(MphRead::RenderWindow& window)
    {
        CloseMenu();
        // Tear the results view down while it is still the surface owner.
        // ShowFrontScreen() below must be the next owner, not something a
        // delayed TickEndPanel() can subsequently hide.
        CloseEndPanel();
        window.EndScene();
        MphRead::Mods::Network::NetSession::Stop();
        MphRead::Mods::Network::NetHostSession::Stop();
        MphRead::Mods::Launcher::MatchStart::AfterMatch();
        ShowFrontScreen();
    }

    void Shell::RequestEndMatch()
    {
        if (!_active)
        {
            return;
        }
        _endMatch = true;
    }

    void Shell::RequestQuit()
    {
        _quit = true;
    }

    void Shell::LeaveMatch(MphRead::RenderWindow& window)
    {
        if (_active)
        {
            RequestEndMatch();
            return;
        }
        window.Close();
    }

    void Shell::Quit(MphRead::RenderWindow& window)
    {
        if (_active)
        {
            RequestQuit();
            return;
        }
        window.Close();
    }

    bool Shell::OpenPauseMenu()
    {
        const std::shared_ptr<UiSurface> surface = UiSurface::Ensure();
        if (surface == nullptr)
        {
            return false;
        }
        if (_menu != nullptr)
        {
            return true;
        }
        std::shared_ptr<InGameMenu> menu
            = std::make_shared<InGameMenu>(MphRead::GameState::LoadSettings());
        menu->Emptied += [](InGameMenu&) { CloseMenu(); };
        _menu = menu;
        surface->Show(menu);
        return true;
    }

    void Shell::CloseMenu()
    {
        if (_menu == nullptr)
        {
            return;
        }
        _menu.reset();
        const std::shared_ptr<UiSurface> surface = UiSurface::Current();
        if (surface != nullptr)
        {
            surface->Hide();
        }
        MphRead::Mods::PauseMenu::MarkClosed();
    }

    void Shell::RequestShots(std::string directory)
    {
        _shotScript.clear();
        g_stressCycles = g_stressCycle = g_stressReleases = g_stressResizes = 0;
        g_stressFinished = g_stressClosed = false;
        if (const auto* value = std::getenv("FRUITY_SWITCHSTRESS"))
        {
            std::size_t used = 0; const std::string text(value); g_stressCycles = std::stoi(text, &used);
            if (used != text.size() || g_stressCycles < 1 || g_stressCycles > 1000
                || std::getenv("FRUITY_SWITCHCHECK_FAILURES"))
                throw std::invalid_argument("FRUITY_SWITCHSTRESS must be 1..1000, without fault injection.");
        }
        g_switchWitness.reset();
        g_switchWitnessSelfChecked = false;
        _shotDirectory = std::move(directory);
        _shotStep = 0;
        _shotWait = 0;
        g_shotUiReadyAt = {};
        _shotMisses = 0;
        _shotPreviewGeneration = 0;
        NativeFilePicker::Suppressed(true);
        MphRead::Mods::WindowGeometry::Enabled(false);
    }

    void Shell::AfterDraw(MphRead::RenderWindow& window)
    {
        MphRead::Export::Images::PollReadbacks();
        Diagnostics::LauncherWindowCheck::AfterDraw(window);
        if (!_shotDirectory.has_value())
        {
            return;
        }
        if (_shotWait > 0)
        {
            --_shotWait;
            return;
        }
        if (std::chrono::steady_clock::now() < g_shotUiReadyAt) return;
        if (_shotScript.empty()) _shotScript = Script();
        if (_shotStep >= static_cast<std::int32_t>(_shotScript.size()))
        {
            _shotDirectory.reset();
            window.Close();
            return;
        }
        _shotScript[static_cast<std::size_t>(_shotStep++)](window);
    }

    namespace
    {
        // The renderer the switch check goes to next: whichever this is not.
        MphRead::NativeRuntime::Rhi::SceneBackendRequest Other()
        {
            return MphRead::NativeRuntime::Rhi::SelectedSceneBackend()
                    == MphRead::NativeRuntime::Rhi::GraphicsBackend::Vulkan
                ? MphRead::NativeRuntime::Rhi::SceneBackendRequest::OpenGL
                : MphRead::NativeRuntime::Rhi::SceneBackendRequest::Vulkan;
        }

        void SayBackend(const char* step)
        {
            std::cout << "[switchcheck] " << step << ": "
                << MphRead::NativeRuntime::Rhi::SceneBackendName(MphRead::NativeRuntime::Rhi::SelectedSceneBackend())
                << '\n';
        }

        void CheckWindowVisible(MphRead::RenderWindow& window)
        {
#if !defined(__ANDROID__)
            const bool visible = glfwGetWindowAttrib(static_cast<GLFWwindow*>(window.WindowPtr()), GLFW_VISIBLE) == GLFW_TRUE;
            std::cout << "[switchcheck] platform window visible " << (visible ? "yes" : "NO") << '\n';
            if (!visible) ++Shell::ShotMissCounter();
#endif
        }
    }

    void Shell::StartSwitchMatch()
    {
        const char* room = std::getenv("FRUITY_SHOT_ROOM");
        LaunchPlan::Init init{};
        init.Kind = LaunchKind::Offline;
        init.RoomKey = room != nullptr ? std::string(room) : (_rooms.empty() ? std::string("MP10 OVERLOAD") : _rooms.front());
        init.Mode = MphRead::GameMode::Battle;
        init.Hunter = MphRead::Hunter::Sylux;
        init.Bots = 7;
        Decided(LaunchPlan(init));
    }

    // The switch at the moments a person makes it: from the front screen,
    // during a match, and from Settings, there and back twice. Every stop
    // is photographed and says which renderer drew it.
    namespace
    {
        const void* g_switchScene = nullptr;
        std::uint64_t g_switchFrame = 0;
        MphRead::NativeRuntime::Rhi::GraphicsBackend g_switchBackend{};
        OpenTK::Mathematics::Vector2i g_switchSize{};
        OpenTK::Mathematics::Vector2i g_switchLocation{};
        std::int32_t g_switchBorder = 0;
        std::vector<std::pair<int, std::weak_ptr<MphRead::Effect>>> g_switchEffects{};
        std::shared_ptr<MphRead::Effects::EffectEntry> g_impactProbe{};
        std::weak_ptr<MphRead::Entities::BombEntity> g_bombProbe{};
        std::weak_ptr<MphRead::Entities::BombEntity> g_existingBombProbe{};
        std::weak_ptr<MphRead::Effects::EffectEntry> g_existingBombEffect{};
        std::weak_ptr<MphRead::Entities::PlayerEntity> g_bombOwner{};
        std::weak_ptr<MphRead::Entities::PlayerEntity> g_existingBombOwner{};
        bool g_expectExistingBomb = false;
        std::uint64_t g_effectProbeFrame = 0;

        void LogSwitchProbeContext(MphRead::RenderWindow& window, const char* step)
        {
            const auto player = MphRead::Entities::PlayerEntity::Main();
            const auto existing = g_existingBombProbe.lock();
            const auto expected = g_existingBombEffect.lock();
            const auto owner = existing ? existing->Owner() : nullptr;
            std::cout << "[switchcheck context] " << step << "; frame=" << window.Scene().FrameCount()
                << "; main-health=" << (player ? player->Health() : -1)
                << "; owned-bombs=" << (player ? static_cast<int>(player->SyluxBombCount()) : -1)
                << "; existing-owner-health=" << (owner ? owner->Health() : -1)
                << "; existing-flags=" << (existing ? static_cast<int>(existing->Flags()) : -1)
                << "; existing-countdown=" << (existing ? existing->Countdown() : -1)
                << "; original-effect-alive=" << (expected ? "yes" : "no")
                << "; original-effect-still-owned=" << (existing && expected && existing->Effect() == expected ? "yes" : "no")
                << '\n';
        }

        void SpawnSwitchEffects(MphRead::RenderWindow& window)
        {
            auto& scene = window.Scene();
            g_effectProbeFrame = scene.FrameCount();
            if (g_impactProbe) scene.UnlinkEffectEntry(g_impactProbe);
            const auto player = MphRead::Entities::PlayerEntity::Main();
            LogSwitchProbeContext(window, "before probe placement");
            g_bombOwner = player;
            const auto camera = player->CameraInfo();
            const auto facing = camera->Facing.Normalized();
            const auto right = OpenTK::Mathematics::Vector3::Cross(facing, camera->UpVector).Normalized();
            const auto position = camera->Position + OpenTK::Mathematics::ScaleVector(facing, 3.0F);
            // Effect 2 is the Power Beam impact without a splat. Use the same
            // emitter as BeamEffectEntity::Create, and the real bomb placement.
            g_impactProbe = scene.SpawnEffectGetEntry(2, right, -facing,
                position - OpenTK::Mathematics::ScaleVector(right, 0.7F));
            g_bombProbe = MphRead::Entities::BombEntity::Spawn(player.get(),
                MphRead::Entities::EntityBase::GetTransformMatrix(facing, camera->UpVector,
                    position + OpenTK::Mathematics::ScaleVector(right, 0.7F)), &scene);
            const auto bomb = g_bombProbe.lock();
            std::cout << "[switchcheck context] placed bomb; owner-health=" << player->Health()
                << "; frame=" << scene.FrameCount() << "; countdown=" << (bomb ? bomb->Countdown() : -1) << '\n';
        }

        // Place the fixtures immediately before effect processing, after all
        // simulation catch-up steps. This keeps a one-shot burst's initial
        // emission window independent of renderer-startup hitches. AfterDraw
        // would run too late, after this frame's effect processing.
        class SwitchEffectPlacement final : public MphRead::Entities::EntityBase
        {
        public:
            explicit SwitchEffectPlacement(MphRead::RenderWindow& window)
                : EntityBase(MphRead::EntityType::Model, &window.Scene()), _window(window)
            {}
            bool Process() override { _ready = true; return !_placed; }
            void GetDrawInfo() override
            {
                if (_ready && !_placed) { SpawnSwitchEffects(_window); _placed = true; }
            }
        private:
            MphRead::RenderWindow& _window;
            bool _placed = false;
            bool _ready = false;
        };

        void QueueSwitchEffects(MphRead::RenderWindow& window)
        {
            g_effectProbeFrame = window.Scene().FrameCount() + 1;
            window.Scene().AddEntity(std::make_shared<SwitchEffectPlacement>(window));
        }

        void CheckSwitchEffects(MphRead::RenderWindow& window, const char* step)
        {
            LogSwitchProbeContext(window, step);
            int retained = 0;
            for (const auto& [id, source] : g_switchEffects)
                if (auto effect = source.lock(); effect && MphRead::Read::GetEffect(id) == effect) ++retained;
            const auto drawable = [](const std::shared_ptr<MphRead::Effects::EffectEntry>& entry)
            {
                int count = 0;
                if (entry)
                    for (const auto& element : *entry->Elements)
                        for (const auto& particle : *element->Particles)
                            if (particle->ShouldDraw()) ++count;
                return count;
            };
            const auto bomb = g_bombProbe.lock();
            const int impactParticles = drawable(g_impactProbe);
            const int bombParticles = drawable(bomb ? bomb->Effect() : nullptr);
            const auto existingBomb = g_existingBombProbe.lock();
            const int existingBombParticles = drawable(existingBomb ? existingBomb->Effect() : nullptr);
            const auto owner = g_bombOwner.lock();
            const auto existingOwner = g_existingBombOwner.lock();
            const bool ownerDead = owner && owner->Health() == 0;
            const bool existingOwnerDead = existingOwner && existingOwner->Health() == 0;
            const auto exploded = [](const auto& entity, int particles) {
                return entity && entity->Flags() == MphRead::Entities::BombFlags::Exploded
                    && entity->Countdown() == 0 && !entity->Effect() && particles == 0;
            };
            // C# and native BombEntity both explode Lockjaw when its owner dies.
            // This future-time probe must require that lifecycle, while the
            // exact-transition witness separately forbids changes during switch.
            const bool newBombValid = ownerDead ? exploded(bomb, bombParticles) : bombParticles > 0;
            const auto originalEffect = g_existingBombEffect.lock();
            const bool existingBombValid = existingOwnerDead ? exploded(existingBomb, existingBombParticles)
                : existingBombParticles > 0 && originalEffect && existingBomb && existingBomb->Effect() == originalEffect;
            std::cout << "[switchcheck] " << step << ": effect definitions " << retained << '/'
                << g_switchEffects.size() << ", impact particles " << impactParticles
                << ", Lockjaw particles " << bombParticles
                << ", existing Lockjaw particles " << existingBombParticles
                << ", impact elements " << (g_impactProbe ? g_impactProbe->Elements->size() : 0)
                << ", bomb elements " << (bomb && bomb->Effect() ? bomb->Effect()->Elements->size() : 0)
                << ", bomb entity alive " << (bomb ? "yes" : "no")
                << ", bomb flags " << (bomb ? static_cast<int>(bomb->Flags()) : -1)
                << ", bomb countdown " << (bomb ? bomb->Countdown() : -1)
                << ", existing bomb entity alive " << (existingBomb ? "yes" : "no")
                << ", existing bomb flags " << (existingBomb ? static_cast<int>(existingBomb->Flags()) : -1)
                << ", existing bomb countdown " << (existingBomb ? existingBomb->Countdown() : -1)
                << ", owner death expected " << (ownerDead ? "yes" : "no")
                << ", bomb lifecycle " << (newBombValid ? "PASS" : "FAIL")
                << ", existing lifecycle " << (!g_expectExistingBomb || existingBombValid ? "PASS" : "FAIL")
                << ", state " << static_cast<int>(MphRead::GameState::MatchState())
                << ", elapsed " << window.Scene().ElapsedTime() << '\n';
            if (g_switchEffects.empty() || retained != static_cast<int>(g_switchEffects.size())
                || impactParticles == 0 || !newBombValid
                || (g_expectExistingBomb && !existingBombValid)) ++Shell::ShotMissCounter();
            if (g_expectExistingBomb && bomb) bomb->SetCountdown(1);
            else
            {
                g_existingBombProbe = bomb;
                g_existingBombEffect = bomb ? bomb->Effect() : nullptr;
                g_existingBombOwner = g_bombOwner;
                g_expectExistingBomb = true;
            }
            if (g_impactProbe) window.Scene().UnlinkEffectEntry(g_impactProbe);
            g_impactProbe.reset();
        }

        void NoteMatch(MphRead::RenderWindow& window)
        {
            LogSwitchProbeContext(window, "before settings switch");
            g_switchScene = window.HasScene() ? &window.Scene() : nullptr;
            g_switchFrame = window.HasScene() ? window.Scene().FrameCount() : 0;
            g_switchBackend = MphRead::NativeRuntime::Rhi::SelectedSceneBackend();
            g_switchSize = window.ClientSize();
            g_switchLocation = window.Location();
            g_switchBorder = window.WindowBorder();
        }

        // The match the switch was made in is still the one running, and
        // it went on simulating.
        void CheckMatchKept(MphRead::RenderWindow& window, const char* step)
        {
            CheckWindowVisible(window);
            const bool kept = window.HasScene() && &window.Scene() == g_switchScene
                && window.Scene().FrameCount() > g_switchFrame;
            std::cout << "[switchcheck] " << step << ": match kept " << (kept ? "yes" : "NO")
                << " (frame " << g_switchFrame << " -> "
                << (window.HasScene() ? window.Scene().FrameCount() : 0) << ")\n";
            if (!kept) ++Shell::ShotMissCounter();
            const bool switched = MphRead::NativeRuntime::Rhi::SelectedSceneBackend() != g_switchBackend;
            const bool recoveryExpected = std::getenv("FRUITY_SWITCHCHECK_FAILURES") != nullptr;
            const auto size = window.ClientSize();
            const auto location = window.Location();
            const bool geometry = size.X == g_switchSize.X && size.Y == g_switchSize.Y
                && location.X == g_switchLocation.X && location.Y == g_switchLocation.Y
                && window.WindowBorder() == g_switchBorder;
            std::cout << "[switchcheck] backend changed " << (switched ? "yes" : "NO")
                << ", window geometry kept " << (geometry ? "yes" : "NO") << '\n';
            if (!geometry) std::cout << "[switchcheck geometry] expected=" << g_switchSize.X << ',' << g_switchSize.Y
                << '@' << g_switchLocation.X << ',' << g_switchLocation.Y << "/" << g_switchBorder
                << " actual=" << size.X << ',' << size.Y << '@' << location.X << ',' << location.Y << '/' << window.WindowBorder() << '\n';
            if (recoveryExpected)
            {
                const bool restored = !switched && !g_switchFailureArmed && g_switchRecoveryReports == g_switchFailureCycle + 1;
                std::cout << "[switch failure] original backend restored " << (restored ? "PASS" : "FAIL") << '\n';
                if (!restored || !geometry) ++Shell::ShotMissCounter();
            }
            else if (!switched || !geometry) ++Shell::ShotMissCounter();
            const bool sourceAlive = !g_textureOnlySource.expired();
            std::cout << "[switchcheck] texture-only source retained " << (sourceAlive ? "yes" : "NO") << '\n';
            if (!sourceAlive) ++Shell::ShotMissCounter();
        }
    }

    namespace
    {
        const void* g_latencyDevice = nullptr;
        const void* g_latencyScene = nullptr;
        std::uint64_t g_latencyFrame = 0;
        OpenTK::Mathematics::Vector2i g_latencySize{}, g_latencyLocation{};
        std::int32_t g_latencyBorder = 0;
    }
    void Shell::AppendLatencyChecks(std::vector<ShotAction>& script)
    {
        if (!std::getenv("FRUITY_LATENCYCHECK")) return;
        using namespace NativeRuntime::Rhi;
        for (const auto mode : {LowLatencyMode::Off, LowLatencyMode::On, LowLatencyMode::OnBoost})
        {
            script.push_back([mode](MphRead::RenderWindow& window)
            {
                g_latencyDevice = &SceneDevice(); g_latencyScene = &window.Scene();
                g_latencyFrame = window.Scene().FrameCount();
                if (mode == LowLatencyMode::Off)
                { g_latencySize = window.ClientSize(); g_latencyLocation = window.Location(); g_latencyBorder = window.WindowBorder(); }
                LauncherPrefs::LowLatency(mode);
                LauncherPrefs::Save(); LauncherPrefs::LowLatency(LowLatencyMode::Off); LauncherPrefs::Load();
                if (LauncherPrefs::LowLatency() != mode) ++Shell::ShotMissCounter();
                Render::FrameTiming::SetFrameRateCap(mode == LowLatencyMode::Off ? 0 : mode == LowLatencyMode::On ? 144 : 500);
                if (mode == LowLatencyMode::On) WindowMode::Enter(window);
                else WindowMode::Leave(window);
                if (mode == LowLatencyMode::OnBoost)
                { window.WindowBorder(g_latencyBorder); window.ClientSize(g_latencySize); window.Location(g_latencyLocation); }
                Shell::Wait(30);
            });
            script.push_back([mode](MphRead::RenderWindow& window)
            {
                const auto state = ResolveLowLatency(LauncherPrefs::LowLatency(), SceneDevice().LowLatencyCaps());
                const bool okay = &SceneDevice() == g_latencyDevice && &window.Scene() == g_latencyScene
                    && window.Scene().FrameCount() > g_latencyFrame && state.requested == mode
                    && state.effective == (mode == LowLatencyMode::Off ? LowLatencyMode::Off
                        : mode == LowLatencyMode::OnBoost && state.boostSupported ? LowLatencyMode::OnBoost : LowLatencyMode::On)
                    && (mode != LowLatencyMode::OnBoost || state.boostSupported == state.fallbackReason.empty())
                    && WindowMode::IsFullscreen() == (mode == LowLatencyMode::On);
                std::cout << "[latencycheck] " << (okay ? "PASS" : "FAIL") << " backend=" << static_cast<int>(SelectedSceneBackend())
                    << " requested=" << static_cast<int>(state.requested) << " effective=" << static_cast<int>(state.effective)
                    << " same-device=" << (&SceneDevice() == g_latencyDevice) << " frame=" << g_latencyFrame << "->" << window.Scene().FrameCount()
                    << " fullscreen=" << WindowMode::IsFullscreen() << " cap=" << Render::FrameTiming::FrameRateCap() << '\n';
                if (!okay) ++Shell::ShotMissCounter();
            });
        }
        // Settings is a transaction: the row applies live, and leaving
        // without saving puts back both the running mode and the file.
        struct CancelCase { LowLatencyMode start; bool escape; };
        static std::string g_latencyFile;
        static LowLatencyMode g_latencyPreview = LowLatencyMode::Off;
        const auto readPrefs = [] {
            std::ifstream in(std::filesystem::path(LauncherPrefs::Directory()) / "launcher.txt", std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(in), {});
        };
        for (const CancelCase item : {CancelCase{LowLatencyMode::Off, false}, CancelCase{LowLatencyMode::On, false},
                 CancelCase{LowLatencyMode::OnBoost, true}})
        {
            script.push_back([item, readPrefs](MphRead::RenderWindow& window)
            {
                LauncherPrefs::LowLatency(item.start); LauncherPrefs::Save();
                g_latencyFile = readPrefs();
                WindowKey(window, KeyValue(256)); Wait(20);
            });
            script.push_back([](MphRead::RenderWindow&)
            {
                Click([](Av::Controls::Control& control)
                {
                    const auto* button = dynamic_cast<DeckButton*>(&control);
                    return button && button->Text() == "Settings";
                }); Wait(20);
            });
            script.push_back([](MphRead::RenderWindow&)
            {
                const auto surface = UiSurface::Current();
                const bool clicked = surface && surface->ClickOn([](Av::Controls::Control& control)
                {
                    const auto* row = dynamic_cast<ChoiceRow*>(&control);
                    return row && (row->Value() == "Off" || row->Value() == "On" || row->Value() == "On + Boost")
                        && row->Label() == "Low Latency";
                });
                if (!clicked) ++Shell::ShotMissCounter();
                g_latencyPreview = LauncherPrefs::LowLatency();
                Wait(10);
            });
            script.push_back([item](MphRead::RenderWindow&)
            {
                if (item.escape) Key(KeyValue(256));
                else Click([](Av::Controls::Control& control)
                {
                    const auto* mark = dynamic_cast<UiMark*>(&control);
                    return mark && mark->Label() == "cancel";
                });
                Wait(10);
            });
            script.push_back([item, readPrefs](MphRead::RenderWindow&)
            {
                const auto state = ResolveLowLatency(LauncherPrefs::LowLatency(), SceneDevice().LowLatencyCaps());
                const bool okay = g_latencyPreview != item.start && LauncherPrefs::LowLatency() == item.start
                    && readPrefs() == g_latencyFile;
                std::cout << "[latencycheck] " << (okay ? "PASS" : "FAIL") << " rollback via="
                    << (item.escape ? "escape" : "cancel") << " start=" << static_cast<int>(item.start)
                    << " preview=" << static_cast<int>(g_latencyPreview) << " after=" << static_cast<int>(LauncherPrefs::LowLatency())
                    << " effective=" << static_cast<int>(state.effective) << " file-unchanged=" << (readPrefs() == g_latencyFile) << '\n';
                if (!okay) ++Shell::ShotMissCounter();
                Click([](Av::Controls::Control& control)
                {
                    const auto* button = dynamic_cast<DeckButton*>(&control);
                    return button && button->Text() == "Resume";
                });
                Wait(20);
            });
        }
    }

    // The switch where a person makes it: on the front screen, and in a
    // running match, which has to carry on through it. Every stop is
    // photographed and says which renderer drew it.
    std::vector<Shell::ShotAction> Shell::SwitchScript(int matchSwitches)
    {
        std::vector<ShotAction> script{
            [](MphRead::RenderWindow&) { WaitUi(30); },
            [](MphRead::RenderWindow& window)
            {
                SayBackend("front, before"); Shot(window, "switch-0-front");
                RequestRenderer(Other(), false); WaitUi(30);
            },
            [](MphRead::RenderWindow& window)
            {
                SayBackend("front, switched"); Shot(window, "switch-1-front");
                CheckWindowVisible(window);
                Click([](Av::Controls::Control& control)
                {
                    const auto* button = dynamic_cast<DeckButton*>(&control);
                    return button && button->Text() == "PLAY";
                }); WaitUi(30);
            },
            [](MphRead::RenderWindow&) { Key(KeyValue(262)); WaitUi(20); },
            [](MphRead::RenderWindow&)
            {
                Click([](Av::Controls::Control& control) { return dynamic_cast<DeckTile*>(&control) != nullptr; });
                WaitUi(30);
            },
            [](MphRead::RenderWindow& window)
            {
                const bool preview = MphRead::Mods::Render::LauncherHunter::Drawn();
                std::cout << "[switchcheck] pre-match production hunter preview " << (preview ? "yes" : "NO") << '\n';
                if (!preview) ++_shotMisses;
                StartSwitchMatch(); Wait(240);
            },
            [](MphRead::RenderWindow& window)
            {
                const auto player = MphRead::Entities::PlayerEntity::Main();
                auto spawns = window.Scene().GetPlayerSpawnEntities().GetEnumerator();
                if (player && spawns.MoveNext())
                {
                    const auto spawn = spawns.Current();
                    player->Spawn(spawn->Position, spawn->FacingVector(), spawn->UpVector(), spawn->NodeRef, true);
                }
                else ++_shotMisses;
                // Optional deterministic effect fixture. Keep all eight
                // actors and the real simulation, but hold the non-main
                // controls so a bot cannot legitimately detonate the probe
                // while renderer startup time changes its approach timing.
                // The default switch stress retains all seven active bots.
                if (std::getenv("FRUITY_SWITCHCHECK_HOLD_ACTORS"))
                {
                    int held = 0;
                    auto actors = window.Scene().GetPlayerEntities().GetEnumerator();
                    while (actors.MoveNext())
                    {
                        const auto actor = actors.Current();
                        if (actor && actor != player)
                        {
                            actor->SetIsBot(false);
                            actor->Controls().ClearAll();
                            ++held;
                        }
                    }
                    std::cout << "[switchcheck] effect fixture held non-main controls " << held << '\n';
                }
                // A texture can outlive the entity/model instance that first
                // uploaded it, and need not have a cached mesh at all. Drop
                // this uncached instance before switching to exercise that
                // lifetime explicitly, rather than relying on bot timing.
                const auto instance = MphRead::Read::GetModelInstance("hud_icon_arrow", false,
                    MphRead::MetaDir::Hud, true);
                g_textureOnlySource = instance->Model();
                (void)window.Scene().BindGetTexture(instance->Model(), 0, 0, 0);
                // Eight spawn bursts share the game's fixed 200-particle
                // pool. Wait in simulation ticks before adding one-shot
                // probes; 240 draw callbacks are shorter at uncapped FPS.
                g_switchFixtureReadyFrame = window.Scene().FrameCount() + 240;
                Wait(240);
            },
            [](MphRead::RenderWindow& window)
            {
                if (window.Scene().FrameCount() < g_switchFixtureReadyFrame) { --_shotStep; Wait(1); return; }
                if (WaitForFpsMeasurement()) { --_shotStep; Wait(1); return; }
                g_switchEffects.clear();
                g_existingBombProbe.reset();
                g_expectExistingBomb = false;
                for (int id = 1; id < static_cast<int>(MphRead::Metadata::Effects.size()); ++id)
                    if (auto effect = MphRead::Read::GetEffect(id)) g_switchEffects.emplace_back(id, effect);
                QueueSwitchEffects(window); Wait(4);
            },
            [](MphRead::RenderWindow& window)
            {
                if (window.Scene().FrameCount() < g_effectProbeFrame + 6) { --_shotStep; Wait(1); return; }
                Shot(window, "switch-effects-0-before");
                CheckSwitchEffects(window, "before match switches");
            },
        };
        if (g_stressCycles) AppendStressResize(script);
        AppendLatencyChecks(script);
        for (int cycle = 0; cycle < matchSwitches; ++cycle)
        {
            script.push_back([cycle](MphRead::RenderWindow& window)
            {
                if (cycle == 0)
                {
                    SayBackend("match"); Shot(window, "switch-2-match");
                }
                NoteMatch(window);
                if (std::getenv("FRUITY_SWITCHCHECK_FAILURES"))
                {
                    g_switchFailureCycle = cycle;
                    g_switchFailureArmed = true;
                }
                WindowKey(window, KeyValue(256)); Wait(20);
            });
            script.push_back(
            [](MphRead::RenderWindow&)
            {
                Click([](Av::Controls::Control& control)
                {
                    const auto* button = dynamic_cast<DeckButton*>(&control);
                    return button && button->Text() == "Settings";
                }); Wait(20);
            });
            script.push_back(
            [](MphRead::RenderWindow&)
            {
                const std::string wanted = Other() == MphRead::NativeRuntime::Rhi::SceneBackendRequest::OpenGL
                    ? "OpenGL" : "Vulkan";
                for (int attempt = 0; attempt < 3; ++attempt)
                {
                    bool ready = false;
                    const auto surface = UiSurface::Current();
                    if (!surface) { ++_shotMisses; break; }
                    const bool clicked = surface->ClickOn([&](Av::Controls::Control& control)
                    {
                        const auto* row = dynamic_cast<ChoiceRow*>(&control);
                        if (!row || (row->Value() != "OpenGL" && row->Value() != "Vulkan" && row->Value() != "Auto")) return false;
                        ready = row->Value() == wanted;
                        return !ready;
                    });
                    if (ready) break;
                    if (!clicked) { ++_shotMisses; break; }
                }
                Wait(10);
            });
            script.push_back(
            [](MphRead::RenderWindow&)
            {
                Click([](Av::Controls::Control& control)
                {
                    const auto* mark = dynamic_cast<UiMark*>(&control);
                    return mark && mark->Label() == "apply";
                }); Wait(90);
            });
            script.push_back([cycle](MphRead::RenderWindow& window)
            {
                SayBackend("match, settings applied");
                if (std::getenv("FRUITY_LATENCYCHECK") && LauncherPrefs::LowLatency() != NativeRuntime::Rhi::LowLatencyMode::OnBoost)
                    ++_shotMisses;
                CheckMatchKept(window, ("settings switch " + std::to_string(cycle + 1)).c_str());
                Click([](Av::Controls::Control& control)
                {
                    const auto* button = dynamic_cast<DeckButton*>(&control);
                    return button && button->Text() == "Resume";
                });
                Wait(90);
            });
            script.push_back([cycle](MphRead::RenderWindow& window)
            {
                CheckMatchKept(window, "resumed after settings");
                if (UiVisible()) ++_shotMisses;
                Wait(2);
            });
            if (g_stressCycles) AppendStressResize(script);
            AppendLatencyChecks(script);
            script.push_back([](MphRead::RenderWindow& window)
            {
                if (WaitForFpsMeasurement()) { --_shotStep; Wait(1); return; }
                QueueSwitchEffects(window); Wait(4);
            });
            script.push_back([cycle](MphRead::RenderWindow& window)
            {
                if (window.Scene().FrameCount() < g_effectProbeFrame + 6) { --_shotStep; Wait(1); return; }
                Shot(window, "switch-" + std::to_string(cycle + 3) + "-match");
                CheckSwitchEffects(window, ("effects after switch " + std::to_string(cycle + 1)).c_str());
                Wait(2);
            });
        }
        script.push_back([](MphRead::RenderWindow&)
        {
            if (WaitForFpsMeasurement()) { --_shotStep; Wait(1); }
        });
        script.push_back([](MphRead::RenderWindow&)
        {
            // End through the production queue, before the next frame; the
            // current frame still calls Scene::AfterRenderFrame after us.
            RequestEndMatch();
            Wait(2);
        });
        script.push_back([](MphRead::RenderWindow& window)
        {
            const bool released = g_textureOnlySource.expired();
            std::cout << "[switchcheck] texture-only source released with scene " << (released ? "yes" : "NO") << '\n';
            if (!released || window.HasScene()) ++_shotMisses;
        });
        return script;
    }

    void Shell::AppendStressResize(std::vector<ShotAction>& script)
    {
        script.push_back([](MphRead::RenderWindow& window)
        {
            g_stressScale = MphRead::Mods::RenderOptions::ResolutionScale();
            g_stressOriginalSize = window.ClientSize();
            g_stressResizeWaits = MphRead::NativeRuntime::Rhi::SceneDevice().Statistics().DeviceWideWaits;
            MphRead::Mods::RenderOptions::ResolutionScale(75);
            window.ClientSize(OpenTK::Mathematics::Vector2i{g_stressCycle % 2 ? 1280 : 1400, 800});
            window.Scene().OnResize(); Wait(20);
        });
        script.push_back([](MphRead::RenderWindow& window)
        {
            int width = 0, height = 0; const auto pixels = window.Scene().ReadSceneTarget(width, height);
            const auto extent = window.FramebufferSize();
            const auto stats = MphRead::NativeRuntime::Rhi::SceneDevice().Statistics();
            const bool valid = pixels && !pixels->empty() && width == MphRead::Mods::RenderOptions::Scaled(extent.X)
                && height == MphRead::Mods::RenderOptions::Scaled(extent.Y) && stats.DeviceWideWaits == g_stressResizeWaits;
            std::cout << "[render stress] resize cycle=" << g_stressCycle << "; backend="
                << MphRead::NativeRuntime::Rhi::SceneBackendName(MphRead::NativeRuntime::Rhi::SelectedSceneBackend())
                << "; target=" << width << 'x' << height << "; device-wide-waits-delta="
                << (stats.DeviceWideWaits - g_stressResizeWaits) << "; " << (valid ? "PASS" : "FAIL") << '\n';
            if (!valid) ++_shotMisses; else ++g_stressResizes;
            Shot(window, "stress-resized-" + std::string(MphRead::NativeRuntime::Rhi::SceneBackendName(MphRead::NativeRuntime::Rhi::SelectedSceneBackend())));
            MphRead::Mods::RenderOptions::ResolutionScale(g_stressScale);
            window.ClientSize(g_stressOriginalSize); window.Scene().OnResize(); Wait(20);
        });
    }

    std::vector<Shell::ShotAction> Shell::StressScript()
    {
        std::vector<ShotAction> script;
        for (int cycle = 1; cycle <= g_stressCycles; ++cycle)
        {
            script.push_back([cycle](MphRead::RenderWindow& window)
            {
                if (window.HasScene()) throw std::runtime_error("Renderer stress cycle started with the previous scene still live.");
                g_stressCycle = cycle;
                std::cout << "[render stress] begin cycle=" << cycle << '/' << g_stressCycles << '\n';
            });
            auto pass = SwitchScript(1); // Front-screen switch, then a live-match Settings switch.
            for (auto& action : pass) script.push_back(std::move(action));
            script.push_back([cycle](MphRead::RenderWindow& window)
            {
                if (window.HasScene() || g_stressReleases != cycle * 2 || g_stressResizes != cycle * 2)
                    throw std::runtime_error("Renderer stress did not unload its scene or release both backend generations.");
                std::cout << "[render stress] cycle=" << cycle << " PASS; scene unloaded; backend generations released=" << g_stressReleases << '\n';
            });
        }
        script.push_back([](MphRead::RenderWindow&) { g_stressFinished = true;
            std::cout << "[render stress] " << (_shotMisses ? "FAIL" : "PASS") << "; cycles=" << g_stressCycles
                << "; renderer switches=" << g_stressReleases << "; misses=" << _shotMisses << '\n'; });
        return script;
    }

    std::vector<Shell::ShotAction> Shell::Script()
    {
        if (g_stressCycles) return StressScript();
        if (std::getenv("FRUITY_SWITCHCHECK") != nullptr) return SwitchScript();
        return {
            [](MphRead::RenderWindow&) { Wait(20); },
            [](MphRead::RenderWindow& window) { Shot(window, "shell-start"); Escape(); Wait(15); },
            [](MphRead::RenderWindow& window) { Shot(window, "shell-escape"); Escape(); Wait(10); },
            [](MphRead::RenderWindow&) { HoverFront(); Wait(15); },
            [](MphRead::RenderWindow& window) { Shot(window, "shell-hover"); Wait(2); },
            [](MphRead::RenderWindow&) { ClickSettings(); Wait(15); },
            [](MphRead::RenderWindow& window) { Shot(window, "shell-click"); Escape(); Wait(10); },
            [](MphRead::RenderWindow& window)
            {
                window.ClientSize(OpenTK::Mathematics::Vector2i{1000, 620});
                Wait(20);
            },
            [](MphRead::RenderWindow& window)
            {
                Shot(window, "shell-resized");
                window.WindowStateMinimized();
                Wait(20);
            },
            [](MphRead::RenderWindow& window)
            {
                if (window.WindowState() != MphRead::RendererPlatform::WindowStateValue::Minimized)
                {
                    ++_shotMisses;
                    std::cout << "[shellshot] shell-resized minimize failed\n";
                }
                window.WindowStateNormal();
                Wait(20);
            },
            [](MphRead::RenderWindow& window)
            {
                if (window.WindowState() != MphRead::RendererPlatform::WindowStateValue::Normal)
                {
                    ++_shotMisses;
                    std::cout << "[shellshot] shell-resized restore failed\n";
                }
                Shot(window, "shell-resized-restored");
                window.WindowStateMaximized();
                Wait(20);
            },
            [](MphRead::RenderWindow& window)
            {
                Shot(window, "shell-maximized");
                window.WindowStateNormal();
                Wait(20);
            },
            [](MphRead::RenderWindow& window)
            {
                // ShellShot may inherit a saved fullscreen launcher preference.
                // This screenshot has a named state, so establish it instead of
                // assuming Toggle() always means windowed -> fullscreen.
                MphRead::Mods::WindowMode::Enter(window);
                Wait(25);
            },
            [](MphRead::RenderWindow& window)
            {
                Shot(window, "shell-fullscreen");
                ClickSettings();
                Wait(15);
            },
            [](MphRead::RenderWindow& window)
            {
                Shot(window, "shell-fullscreen-click");
                Escape();
                // Establish the precondition for the F11 delivery gate below.
                // The gate itself still has to enter fullscreen through
                // RenderWindow::FeedKey(); no direct mode call satisfies it.
                MphRead::Mods::WindowMode::Leave(window);
                Wait(25);
            },
            [](MphRead::RenderWindow& window)
            {
                Shot(window, "shell-windowed");
                if (MphRead::Mods::WindowMode::IsFullscreen())
                {
                    ++_shotMisses;
                    std::cout << "[shellshot] could not establish windowed state before F11\n";
                    _shotDirectory.reset();
                    window.Close();
                    return;
                }
                Wait(5);
            },
            [](MphRead::RenderWindow& window) { WindowKey(window, KeyValue(300)); Wait(20); },
            [](MphRead::RenderWindow& window)
            {
                Shot(window, "shell-launcher-fullscreen");
                if (!MphRead::Mods::WindowMode::IsFullscreen())
                {
                    ++_shotMisses;
                    std::cout << "[shellshot] F11 did not reach the window on the front screen\n";
                }
                WindowKey(window, KeyValue(300));
                Wait(20);
            },
            [](MphRead::RenderWindow&) { ClickIfReady([](Av::Controls::Control& control)
            {
                const auto* button = dynamic_cast<DeckButton*>(&control);
                return button != nullptr && button->Text() == "PLAY";
            }); Wait(20); },
            [](MphRead::RenderWindow& window)
            {
                Shot(window, "shell-play");
                // The Phase 4 gate must not depend on an external live server.
                // Move deterministically from Online to Offline; the required
                // DeckTile click below proves that this transition succeeded.
                Key(KeyValue(262));
                Wait(15);
            },
            [](MphRead::RenderWindow& window)
            {
                Shot(window, "shell-play-offline");
                const std::shared_ptr<UiSurface> surface = UiSurface::Current();
                if (surface != nullptr)
                {
                    const OpenTK::Mathematics::Vector2i client = window.ClientSize();
                    surface->PointerMoved(client.X / 2.0, client.Y / 2.0);
                }
                Scroll(60);
                Scroll(60, 1);
                Wait(10);
            },
            [](MphRead::RenderWindow&) { ClickIfReady([](Av::Controls::Control& control)
            {
                return dynamic_cast<DeckTile*>(&control) != nullptr;
            }); WaitUi(25); },
            [](MphRead::RenderWindow& window)
            {
                Shot(window, "shell-play-selected");
                if (window.HasScene() || !MphRead::Mods::Render::LauncherHunter::Drawn()
                    || MphRead::Mods::Render::LauncherHunter::SceneGeneration() == 0)
                {
                    ++_shotMisses;
                    std::cout << "[shellshot] pre-match production hunter preview did not draw\n";
                    _shotDirectory.reset();
                    window.Close();
                    return;
                }
                _shotPreviewGeneration = MphRead::Mods::Render::LauncherHunter::SceneGeneration();
                std::cout << "[shellshot] pre-match production hunter preview drawn; side-scene generation="
                    << _shotPreviewGeneration << '\n';
                ClickIfReady([](Av::Controls::Control& control)
                {
                    const auto* button = dynamic_cast<DeckButton*>(&control);
                    return button != nullptr && button->Text() == "START";
                });
                Wait(40);
            },
            [](MphRead::RenderWindow& window)
            {
                if (!window.HasScene())
                {
                    ++_shotMisses;
                    std::cout << "[shellshot] Play/START did not create a real match scene\n";
                    _shotDirectory.reset();
                    window.Close();
                    return;
                }
                std::cout << "[shellshot] real match scene started through Play/START\n";
                window.Scene().BeginModelReloadDrawProbe();
                std::cout << "[shellshot] production model unload/reload/draw probe armed\n";
                Wait(8);
            },
            [](MphRead::RenderWindow& window)
            {
                // FRUITY_SHOT_FRAME=N holds the match shot until simulation
                // frame N, so two runs (OpenGL and Vulkan) photograph the
                // same instant and can be compared pixel for pixel.
                if (const char* at = std::getenv("FRUITY_SHOT_FRAME"); at != nullptr
                    && window.Scene().FrameCount() < static_cast<std::uint64_t>(std::atoi(at)))
                {
                    --_shotStep;
                    return;
                }
                if (!window.Scene().ModelReloadDrawProbePassed())
                {
                    ++_shotMisses;
                    std::cout << "[shellshot] production model unload/reload/draw probe failed: "
                        << window.Scene().ModelReloadDrawProbeStatus() << '\n';
                    _shotDirectory.reset();
                    window.Close();
                    return;
                }
                std::cout << "[shellshot] production model unload/reload/draw probe passed: "
                    << window.Scene().ModelReloadDrawProbeStatus() << '\n';
                Shot(window, "shell-match");
                window.WindowStateMinimized();
                Wait(20);
            },
            [](MphRead::RenderWindow& window)
            {
                if (window.WindowState() != MphRead::RendererPlatform::WindowStateValue::Minimized)
                {
                    ++_shotMisses;
                    std::cout << "[shellshot] shell-match minimize failed\n";
                }
                window.WindowStateNormal();
                Wait(20);
            },
            [](MphRead::RenderWindow& window)
            {
                if (window.WindowState() != MphRead::RendererPlatform::WindowStateValue::Normal)
                {
                    ++_shotMisses;
                    std::cout << "[shellshot] shell-match restore failed\n";
                }
                Shot(window, "shell-match-restored");
                if (!MphRead::Mods::Diagnostics::CheckRhiImageExports(*_shotDirectory))
                    ++_shotMisses;
                window.WindowStateMaximized();
                Wait(30);
            },
            [](MphRead::RenderWindow& window)
            {
                MphRead::Mods::PauseMenu::HandleEscape(window);
                Wait(20);
            },
            [](MphRead::RenderWindow& window)
            {
                Shot(window, "shell-pause-maximized");
                MphRead::Mods::PauseMenu::HandleEscape(window);
                window.WindowStateNormal();
                Wait(30);
            },
            [](MphRead::RenderWindow& window)
            {
                MphRead::Mods::WindowMode::Toggle(window);
                Wait(30);
            },
            [](MphRead::RenderWindow& window)
            {
                Shot(window, "shell-match-fullscreen");
                MphRead::Mods::PauseMenu::HandleEscape(window);
                Wait(20);
            },
            [](MphRead::RenderWindow& window)
            {
                Shot(window, "shell-pause-fullscreen");
                MphRead::Mods::WindowMode::Toggle(window);
                Wait(30);
            },
            [](MphRead::RenderWindow& window)
            {
                Shot(window, "shell-pause");
                Click([](Av::Controls::Control& control)
                {
                    const auto* button = dynamic_cast<DeckButton*>(&control);
                    return button != nullptr && button->Text() == "Settings";
                });
                Wait(20);
            },
            [](MphRead::RenderWindow& window)
            {
                Shot(window, "shell-settings-ingame");
                Escape();
                Wait(15);
            },
            [](MphRead::RenderWindow& window)
            {
                Escape();
                MphRead::Mods::WindowMode::Toggle(window);
                Wait(30);
            },
            [](MphRead::RenderWindow&) { HoldResults(); Wait(60); },
            [](MphRead::RenderWindow& window)
            {
                Shot(window, "shell-endgame");
                Click([](Av::Controls::Control& control)
                {
                    const auto* button = dynamic_cast<DeckButton*>(&control);
                    return button != nullptr && button->Text() == "Change hunter";
                });
                Wait(30);
            },
            [](MphRead::RenderWindow& window)
            {
                Shot(window, "shell-endgame-hunter");
                Click([](Av::Controls::Control& control)
                {
                    const auto* button = dynamic_cast<DeckButton*>(&control);
                    return button != nullptr && button->Text() == "READY";
                });
                Wait(15);
            },
            [](MphRead::RenderWindow& window)
            {
                if (!MphRead::Mods::EndScreen::Ready())
                {
                    ++_shotMisses;
                    std::cout << "[shellshot] results READY did not take effect\n";
                    _shotDirectory.reset();
                    window.Close();
                    return;
                }
                std::cout << "[shellshot] results READY accepted; leaving through the shell match path\n";
                LeaveMatch(window);
                Wait(60);
            },
            [](MphRead::RenderWindow& window)
            {
                if (window.HasScene())
                {
                    ++_shotMisses;
                    std::cout << "[shellshot] READY/leave did not return to the launcher\n";
                    _shotDirectory.reset();
                    window.Close();
                    return;
                }
                if (EndPanelUp())
                {
                    ++_shotMisses;
                    std::cout << "[shellshot] results panel still marked up after launcher return\n";
                    _shotDirectory.reset();
                    window.Close();
                    return;
                }
                Shot(window, "shell-back-fullscreen");
                MphRead::Mods::WindowMode::Toggle(window);
                Wait(25);
            },
            [](MphRead::RenderWindow& window)
            {
                Shot(window, "shell-back");
                Click([](Av::Controls::Control& control)
                {
                    const auto* button = dynamic_cast<DeckButton*>(&control);
                    return button != nullptr && button->Text() == "PLAY";
                });
                WaitUi(20);
            },
            [](MphRead::RenderWindow&)
            {
                Key(KeyValue(262));
                WaitUi(20);
            },
            [](MphRead::RenderWindow&)
            {
                ClickIfReady([](Av::Controls::Control& control)
                {
                    return dynamic_cast<DeckTile*>(&control) != nullptr;
                });
                WaitUi(30);
            },
            [](MphRead::RenderWindow& window)
            {
                Shot(window, "shell-return-hunter");
                const std::uint64_t generation
                    = MphRead::Mods::Render::LauncherHunter::SceneGeneration();
                if (window.HasScene() || !MphRead::Mods::Render::LauncherHunter::Drawn()
                    || generation <= _shotPreviewGeneration)
                {
                    ++_shotMisses;
                    std::cout << "[shellshot] post-match production hunter preview did not reload/draw; before="
                        << _shotPreviewGeneration << " after=" << generation << '\n';
                    _shotDirectory.reset();
                    window.Close();
                    return;
                }
                std::cout << "[shellshot] post-match production hunter preview drawn after reload; before="
                    << _shotPreviewGeneration << " after=" << generation << '\n';
                Wait(5);
            }
        };
    }

    void Shell::Wait(std::int32_t frames)
    {
        _shotWait = frames;
    }

    void Shell::WaitUi(std::int32_t frames)
    {
        // UI redraw/dispatcher timers use wall time. Uncapped GL draw counts
        // can expire before the next layout/render even though input worked.
        // Keep both the draw-count gate and the intended 60 Hz settling time.
        Wait(frames);
        g_shotUiReadyAt = std::chrono::steady_clock::now()
            + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(frames / 60.0));
    }

    void Shell::ClickSettings()
    {
        if (GameFiles::Ready())
        {
            Click([](Av::Controls::Control& control)
            {
                const auto* button = dynamic_cast<DeckButton*>(&control);
                return button != nullptr && button->Text() == "SETTINGS";
            });
            return;
        }
        Click([](Av::Controls::Control& control)
        {
            const auto* mark = dynamic_cast<UiMark*>(&control);
            return mark != nullptr && mark->Label() == "choose your .nds file";
        });
    }

    void Shell::HoverFront()
    {
        if (GameFiles::Ready())
        {
            Hover([](Av::Controls::Control& control)
            {
                const auto* button = dynamic_cast<DeckButton*>(&control);
                return button != nullptr && button->Text() == "SETTINGS";
            });
            return;
        }
        Hover([](Av::Controls::Control& control)
        {
            const auto* mark = dynamic_cast<UiMark*>(&control);
            return mark != nullptr && mark->Label() == "choose your .nds file";
        });
    }

    void Shell::ClickIfReady(const ControlPredicate& match)
    {
        if (GameFiles::Ready())
        {
            Click(match);
        }
    }

    void Shell::Hover(const ControlPredicate& match)
    {
        const std::shared_ptr<UiSurface> surface = UiSurface::Current();
        if (surface == nullptr || !surface->HoverOn(match))
        {
            ++_shotMisses;
            std::cout << "[shellshot] nothing on screen matched the hover\n";
        }
    }

    void Shell::Click(const ControlPredicate& match)
    {
        const std::shared_ptr<UiSurface> surface = UiSurface::Current();
        if (surface == nullptr || !surface->ClickOn(match))
        {
            ++_shotMisses;
            if (surface && surface->View())
                for (auto* visual : surface->View()->GetVisualDescendants())
                    if (auto* control = dynamic_cast<Av::Controls::Control*>(visual); control && match(*control))
                        std::cout << "[shellshot] missed candidate bounds " << control->Bounds().Width << "x" << control->Bounds().Height << '\n';
            std::cout << "[shellshot] nothing on screen matched the click at step " << _shotStep
                << "; " << (surface ? surface->Describe() : "no UI surface") << '\n';
            if (_window && _shotDirectory)
                Shot(*_window, "miss-step-" + std::to_string(_shotStep));
        }
    }

    void Shell::Key(Keys key)
    {
        const std::shared_ptr<UiSurface> surface = UiSurface::Current();
        if (surface != nullptr)
        {
            surface->KeyDown(key, Av::Input::RawInputModifiers::None);
            surface->KeyUp(key, Av::Input::RawInputModifiers::None);
        }
    }

    void Shell::Scroll(std::int32_t frames, double notches)
    {
        const std::shared_ptr<UiSurface> surface = UiSurface::Current();
        if (surface == nullptr)
        {
            return;
        }
        for (std::int32_t i = 0; i < frames; ++i)
        {
            surface->PointerWheel(0, notches);
            Wait(1);
        }
    }

    void Shell::WindowKey(MphRead::RenderWindow& window, Keys key)
    {
        OpenTK::Windowing::Common::KeyboardKeyEventArgs args{};
        args.Key = key;
        window.FeedKey(args);
    }

    void Shell::Escape()
    {
        Key(KeyValue(256));
    }

    void Shell::HoldResults()
    {
        MphRead::GameState::MatchState(MphRead::MatchState::Ending);
        MphRead::GameState::MatchTime(30);
        try
        {
            MphRead::Mods::MapPick::Begin(
                MphRead::NativeRuntime::RequireReference(_settings).RoomKey, true);
        }
        catch (const std::exception&)
        {
            std::cout << "[shellshot] no ballot to show: "
                << ExceptionMessage(std::current_exception()) << '\n';
        }
    }

    void Shell::Shot(MphRead::RenderWindow& window, const std::string& name)
    {
        const std::string directory = g_stressCycles ? MphRead::NativeRuntime::PathCombine(_shotDirectory.value(),
            "cycle-" + std::to_string(g_stressCycle)) : _shotDirectory.value();
        const std::string path = MphRead::NativeRuntime::PathCombine(
            directory, name + ".png");
        MphRead::NativeRuntime::DirectoryCreateDirectory(directory);
        const OpenTK::Mathematics::Vector2i framebuffer = window.FramebufferSize();
        std::string description = "[shellshot] " + name + ": pixels=" + SizeText(framebuffer)
            + " scene=" + (window.HasScene() ? SizeText(window.Scene().Size()) : "no match")
            + " ";
        const std::shared_ptr<UiSurface> surface = UiSurface::Current();
        if (surface != nullptr)
        {
            description += surface->Describe();
        }
        std::cout << description << '\n';
        const bool saved = MphRead::Mods::ScreenCapture::SaveWindow(
            framebuffer.X, framebuffer.Y, path);
        std::cout << (saved ? "[shellshot] " + path
            : "[shellshot] " + name + " could not be read from the window") << '\n';
    }

    Keys Shell::KeyValue(std::int32_t value) noexcept
    {
        return static_cast<Keys>(value);
    }

    Av::Input::MouseButton Shell::Translate(MouseButton button) noexcept
    {
        switch (button)
        {
        case MouseButton::Button2: return Av::Input::MouseButton::Right;
        case MouseButton::Button3: return Av::Input::MouseButton::Middle;
        default: return Av::Input::MouseButton::Left;
        }
    }

    Av::Input::RawInputModifiers Shell::Modifiers(
        const OpenTK::Windowing::Common::KeyboardKeyEventArgs& e) noexcept
    {
        Av::Input::RawInputModifiers modifiers = Av::Input::RawInputModifiers::None;
        if (e.Shift)
        {
            modifiers |= Av::Input::RawInputModifiers::Shift;
        }
        if (e.Control)
        {
            modifiers |= Av::Input::RawInputModifiers::Control;
        }
        if (e.Alt)
        {
            modifiers |= Av::Input::RawInputModifiers::Alt;
        }
        return modifiers;
    }

    void Shell::PointerMoved(double x, double y)
    {
        const std::shared_ptr<UiSurface> surface = UiSurface::Current();
        if (surface != nullptr)
        {
            surface->PointerMoved(x, y);
        }
    }

    void Shell::PointerButton(MouseButton button, double x, double y, bool down)
    {
        const std::shared_ptr<UiSurface> surface = UiSurface::Current();
        if (surface == nullptr)
        {
            return;
        }
        surface->PointerMoved(x, y);
        surface->PointerButton(Translate(button), down);
    }

    void Shell::PointerWheel(double deltaX, double deltaY)
    {
        const std::shared_ptr<UiSurface> surface = UiSurface::Current();
        if (surface != nullptr)
        {
            surface->PointerWheel(deltaX, deltaY);
        }
    }

    void Shell::KeyDown(const OpenTK::Windowing::Common::KeyboardKeyEventArgs& e)
    {
        const std::shared_ptr<UiSurface> surface = UiSurface::Current();
        if (surface != nullptr)
        {
            surface->KeyDown(e.Key, Modifiers(e));
        }
    }

    void Shell::KeyUp(const OpenTK::Windowing::Common::KeyboardKeyEventArgs& e)
    {
        const std::shared_ptr<UiSurface> surface = UiSurface::Current();
        if (surface != nullptr)
        {
            surface->KeyUp(e.Key, Modifiers(e));
        }
    }

    void Shell::TextInput(const std::string& text)
    {
        const std::shared_ptr<UiSurface> surface = UiSurface::Current();
        if (surface != nullptr)
        {
            surface->TextInput(text);
        }
    }
}
