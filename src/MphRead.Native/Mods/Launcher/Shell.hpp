#pragma once

#include "Portable/LaunchPlan.hpp"
#include "../../NativeRuntime/Rhi/SceneBackend.hpp"

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
        // A match decided before the window opened (-connect): started on the
        // first frame, as a press of JOIN would have started it.
        static void LaunchOnStart(LaunchPlan plan);

        static void PointerMoved(double x, double y);
        static void PointerButton(OpenTK::Windowing::GraphicsLibraryFramework::MouseButton button,
            double x, double y, bool down);
        static void PointerWheel(double deltaX, double deltaY);
        static void KeyDown(const OpenTK::Windowing::Common::KeyboardKeyEventArgs& e);
        static void KeyUp(const OpenTK::Windowing::Common::KeyboardKeyEventArgs& e);
        static void TextInput(const std::string& text);

    };
}
