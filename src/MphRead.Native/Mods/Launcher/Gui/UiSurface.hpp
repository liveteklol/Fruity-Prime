#pragma once

#include "GamepadNavigation.hpp"
#include "UiTopLevel.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace OpenTK::Windowing::GraphicsLibraryFramework
{
    enum class Keys : std::int32_t;
}

namespace MphRead::Mods::Launcher::Gui
{
    namespace Av = ::MphRead::NativeRuntime::Avalonia;

    class UiSurface final
    {
    public:
        using ControlPredicate = std::function<bool(Av::Controls::Control&)>;

        [[nodiscard]] static std::shared_ptr<UiSurface> Current();
        [[nodiscard]] static std::shared_ptr<UiSurface> Ensure();
        static void RequestFrame(std::function<void()> step, bool idling = false);

        [[nodiscard]] std::string Describe() const;
        [[nodiscard]] Av::Controls::ControlPtr View() const { return _view; }
        [[nodiscard]] bool Visible() const noexcept { return _view != nullptr; }
        [[nodiscard]] double Scale() const noexcept { return _factor; }
        [[nodiscard]] std::int32_t WindowWidth() const noexcept { return _pixelWidth; }
        [[nodiscard]] std::int32_t WindowHeight() const noexcept { return _pixelHeight; }
        [[nodiscard]] Av::Visual& Root() noexcept { return _impl.Root(); }

        void Show(const Av::Controls::ControlPtr& view);
        void Hide();
        void Resize(std::int32_t width, std::int32_t height);
        void Invalidate(bool animation = false, bool idling = false);
        // The window is going for a renderer switch: drop Ganesh's context.
        void ReleaseGpu();
        void Tick();
        void PointerMoved(double x, double y);
        void PointerButton(Av::Input::MouseButton button, bool down);
        [[nodiscard]] bool ClickOn(const ControlPredicate& match);
        [[nodiscard]] bool HoverOn(const ControlPredicate& match);
        void PointerWheel(double deltaX, double deltaY);
        void KeyDown(::OpenTK::Windowing::GraphicsLibraryFramework::Keys key,
            Av::Input::RawInputModifiers modifiers);
        void KeyUp(::OpenTK::Windowing::GraphicsLibraryFramework::Keys key,
            Av::Input::RawInputModifiers modifiers);
        void TextInput(const std::string& text);

        [[nodiscard]] static bool NativeRaster() noexcept;
        static void NativeRaster(bool value) noexcept;

    private:
        UiSurface();

        [[nodiscard]] static double Factor(std::int32_t width, std::int32_t height);
        [[nodiscard]] static double Raster(std::int32_t width, std::int32_t height);
        [[nodiscard]] static Av::Input::Key Translate(
            ::OpenTK::Windowing::GraphicsLibraryFramework::Keys key);
        static void RunPending();
        [[nodiscard]] bool Covers(Av::Controls::Control& control, Av::Point point);
        void ApplyScale();
        void Report(double now);

        UiTopLevelImpl _impl;
        std::shared_ptr<Av::Controls::LayoutTransformControl> _host;
        std::int32_t _pixelWidth = 1280;
        std::int32_t _pixelHeight = 768;
        double _factor = 1.0;
        double _raster = 1.0;
        Av::Controls::ControlPtr _view;
        GamepadNavigation _gamepad;
        Av::Point _pointer{};
        Av::Input::RawInputModifiers _modifiers = Av::Input::RawInputModifiers::None;

        bool _dirty = true;
        double _drawnAt = -1000.0;
        double _touchedAt = 0.0;
        std::int32_t _redraws = 0;
        std::int32_t _ticks = 0;
        double _jobs = 0.0;
        double _drawMs = 0.0;
        double _uploadMs = 0.0;
        double _reportedAt = 0.0;
        bool _animIdle = false;
        bool _animOnly = false;
    };
}
