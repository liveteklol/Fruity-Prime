#pragma once

#include "../Portable/LaunchPlan.hpp"
#include "../../../NativeRuntime/Rhi/SceneBackend.hpp"
#if defined(MPHREAD_AVALONIA_SHELL)
#include "../../../NativeRuntime/Avalonia/Avalonia.hpp"
#endif

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace OpenTK::Windowing::Common
{
    struct KeyboardKeyEventArgs;
}

namespace OpenTK::Windowing::GraphicsLibraryFramework
{
    enum class Keys : std::int32_t;
    enum class MouseButton : std::int32_t;
}

namespace MphRead
{
    class MenuSettings;
    class RenderWindow;
}

namespace MphRead::Mods::Launcher::Gui
{
    class EndPanelView;
    class InGameMenu;
    class StartScreen;
    class UiSurface;

    class Shell final
    {
    public:
        Shell() = delete;

        [[nodiscard]] static bool Active() noexcept;
        [[nodiscard]] static bool UiVisible();
        [[nodiscard]] static MphRead::RenderWindow* Window() noexcept;
        [[nodiscard]] static bool EndPanelUp() noexcept;
        [[nodiscard]] static bool CanPlayAnother();
        [[nodiscard]] static std::int32_t ShotMisses() noexcept;
        // The scripted checks' miss count, which every shell keeps.
        [[nodiscard]] static std::int32_t& ShotMissCounter() noexcept;

        [[nodiscard]] static bool Run();
        // Settings switched the renderer: the window is remade on it at the
        // next frame (the running match is retained), and Settings shown again when
        // that is where it was asked from.
        static void RequestRenderer(MphRead::NativeRuntime::Rhi::SceneBackendRequest request, bool fromSettings);
        static void BeforeFrame(MphRead::RenderWindow& window);
        static void TickUi(MphRead::RenderWindow& window);
        static void TickEndPanel();
        static void RequestEndMatch();
        static void RequestQuit();
        static void LeaveMatch(MphRead::RenderWindow& window);
        static void Quit(MphRead::RenderWindow& window);
        [[nodiscard]] static bool OpenPauseMenu();
        static void CloseMenu();
        static void RequestShots(std::string directory);
        static void AfterDraw(MphRead::RenderWindow& window);
        static void PlayAnother(std::string roomKey);

        static void PointerMoved(double x, double y);
        static void PointerButton(OpenTK::Windowing::GraphicsLibraryFramework::MouseButton button,
            double x, double y, bool down);
        static void PointerWheel(double deltaX, double deltaY);
        static void KeyDown(const OpenTK::Windowing::Common::KeyboardKeyEventArgs& e);
        static void KeyUp(const OpenTK::Windowing::Common::KeyboardKeyEventArgs& e);
        static void TextInput(const std::string& text);

#if defined(MPHREAD_AVALONIA_SHELL)
    private:
        using ControlPredicate = std::function<bool(
            MphRead::NativeRuntime::Avalonia::Controls::Control&)>;
        using ShotAction = std::function<void(MphRead::RenderWindow&)>;

        static void PublishNativeHandle(MphRead::RenderWindow& window);
        static void NotePointerBasis(MphRead::RenderWindow& window);
        static void ShowFrontScreen();
        static void Decided(MphRead::Mods::Launcher::LaunchPlan plan);
        static void StartMatch(MphRead::RenderWindow& window,
            MphRead::Mods::Launcher::LaunchPlan plan);
        static void EndNetworkMatchToLobby(MphRead::RenderWindow& window);
        static void EndMatch(MphRead::RenderWindow& window);
        static void CloseEndPanel();
        static void ClickSettings();
        static void HoverFront();
        static void ClickIfReady(const ControlPredicate& match);
        static void Hover(const ControlPredicate& match);
        static void Click(const ControlPredicate& match);
        static void Key(OpenTK::Windowing::GraphicsLibraryFramework::Keys key);
        static void Wait(std::int32_t frames);
        static void WaitUi(std::int32_t frames);
        static void Scroll(std::int32_t frames, double notches = -1);
        static void WindowKey(MphRead::RenderWindow& window,
            OpenTK::Windowing::GraphicsLibraryFramework::Keys key);
        static void Escape();
        static void HoldResults();
        static void Shot(MphRead::RenderWindow& window, const std::string& name);
        [[nodiscard]] static std::vector<ShotAction> Script();
        [[nodiscard]] static OpenTK::Windowing::GraphicsLibraryFramework::Keys KeyValue(
            std::int32_t value) noexcept;
        [[nodiscard]] static MphRead::NativeRuntime::Avalonia::Input::MouseButton Translate(
            OpenTK::Windowing::GraphicsLibraryFramework::MouseButton button) noexcept;
        [[nodiscard]] static MphRead::NativeRuntime::Avalonia::Input::RawInputModifiers Modifiers(
            const OpenTK::Windowing::Common::KeyboardKeyEventArgs& e) noexcept;

        static bool _active;
        static MphRead::RenderWindow* _window;
        static std::shared_ptr<StartScreen> _front;
        static std::shared_ptr<InGameMenu> _menu;
        static std::shared_ptr<MphRead::MenuSettings> _settings;
        static std::vector<std::string> _rooms;
        static std::optional<MphRead::Mods::Launcher::LaunchPlan> _pending;
        static bool _endMatch;
        static bool _quit;
        static std::int32_t _basisWidth;
        static std::int32_t _basisHeight;
        static std::shared_ptr<EndPanelView> _endPanel;
        static std::optional<MphRead::Mods::Launcher::LaunchPlan> _played;
        static std::optional<std::string> _shotDirectory;
        static void ReleaseWindowGpu();
        static void InstallRendererSwitchHooks();
        static void ShowSettings();
        static void StartSwitchMatch();
        [[nodiscard]] static std::vector<ShotAction> SwitchScript(int matchSwitches = 3);
        [[nodiscard]] static std::vector<ShotAction> StressScript();
        static void AppendStressResize(std::vector<ShotAction>& script);
        static void AppendLatencyChecks(std::vector<ShotAction>& script);
        inline static std::vector<ShotAction> _shotScript;
        static std::int32_t _shotStep;
        static std::int32_t _shotWait;
        static std::int32_t _shotMisses;
        static std::uint64_t _shotPreviewGeneration;
#endif
    };
}
