#include "UiSurface.hpp"

#include "Deck.hpp"
#include "GuiLauncher.hpp"
#include "UiLayout.hpp"
#include "../../Render/UiOverlay.hpp"
#include "../../DebugLog.hpp"
#include "../../../Entities/Players/PlayerInput.hpp"
#include "../../Input/GamepadDesktop.hpp"
#include "../../Input/GamepadUiRouter.hpp"
#include "../../../NativeRuntime/System/Console.hpp"
#include "../../../NativeRuntime/System/ExceptionText.hpp"
#include "../../../NativeRuntime/System/Globalization.hpp"
#include "../../../NativeRuntime/System/Number.hpp"
#include "../../../NativeRuntime/System/Stopwatch.hpp"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <utility>
#include <vector>

namespace MphRead::Mods::Launcher::Gui
{
    namespace
    {
        constexpr double RasterCapWidth = 1920.0;
        constexpr double RasterCapHeight = 1080.0;
        constexpr double IdleGap = 50.0;
        constexpr double RestingGap = 250.0;
        constexpr double SettleMs = 3000.0;
        constexpr double BusyGap = 16.0;
        constexpr double AnimGap = 16.0;
        constexpr double IdleAnimGap = 66.0;

        std::shared_ptr<UiSurface> CurrentSurface;
        bool UseNativeRaster = false;
        std::vector<std::function<void()>> PendingFrames;
        std::vector<std::function<void()>> RunningFrames;
        std::mutex PendingFramesLock;

        ::MphRead::NativeRuntime::Stopwatch& FrameClock()
        {
            static ::MphRead::NativeRuntime::Stopwatch clock
                = ::MphRead::NativeRuntime::Stopwatch::StartNew();
            return clock;
        }

        [[nodiscard]] std::int32_t KeyValue(
            ::OpenTK::Windowing::GraphicsLibraryFramework::Keys key) noexcept
        {
            return static_cast<std::int32_t>(key);
        }
    }

    std::shared_ptr<UiSurface> UiSurface::Current()
    {
        (void)FrameClock();
        return CurrentSurface;
    }

    std::shared_ptr<UiSurface> UiSurface::Ensure()
    {
        (void)FrameClock();
        if (CurrentSurface != nullptr)
        {
            return CurrentSurface;
        }
        if (!GuiLauncher::EnsureSetup())
        {
            return nullptr;
        }
        try
        {
            CurrentSurface = std::shared_ptr<UiSurface>(new UiSurface());
        }
        catch (const std::exception& ex)
        {
            ::MphRead::NativeRuntime::ConsoleWriteLine(
                "[launcher] the screens could not be built: " + std::string(ex.what()));
            ::MphRead::Mods::DebugLog::Exception("ui", ex);
            return nullptr;
        }
        catch (...)
        {
            ::MphRead::NativeRuntime::ConsoleWriteLine(
                "[launcher] the screens could not be built: "
                    + ::MphRead::NativeRuntime::ExceptionMessage(std::current_exception()));
            ::MphRead::Mods::DebugLog::Exception("ui", std::current_exception());
            return nullptr;
        }
        return CurrentSurface;
    }

    void UiSurface::ReleaseGpu()
    {
        _impl.ReleaseGpu();
        Invalidate();
    }

    UiSurface::UiSurface()
    {
        _gamepad.Changed += [this] { Invalidate(); };
        _host = std::make_shared<Av::Controls::LayoutTransformControl>();
        _host->LayoutTransform(std::make_shared<Av::Media::ScaleTransform>(1.0, 1.0));
        _host->HorizontalAlignment(Av::Layout::HorizontalAlignment::Stretch);
        _host->VerticalAlignment(Av::Layout::VerticalAlignment::Stretch);

        _impl.SetClientSize(Av::Size{static_cast<double>(_pixelWidth), static_cast<double>(_pixelHeight)});
        Av::EmbeddableControlRoot& window = _impl.Root();
        window.Background(Av::Media::Brushes::Transparent());
        window.Content(_host);
        _impl.Prepare();
        _impl.StartRendering();
    }

    std::string UiSurface::Describe() const
    {
        const Av::Media::TransformPtr transform = _host->LayoutTransform();
        const auto* current = dynamic_cast<const Av::Media::ScaleTransform*>(transform.get());
        const double scale = current != nullptr ? current->ScaleY : -1.0;
        std::string result = "scale=" + ::MphRead::NativeRuntime::ToString(_factor, "0.###")
            + " drawn=" + (scale < 0.0 ? "none" : ::MphRead::NativeRuntime::ToString(scale, "0.###"))
            + " raster=" + ::MphRead::NativeRuntime::ToString(_pixelWidth)
            + "x" + ::MphRead::NativeRuntime::ToString(_pixelHeight);
        if (_raster < 1.0)
        {
            result += " (" + ::MphRead::NativeRuntime::ToString(_raster, "0.###") + " of the window)";
        }
        result += " screen=";
        if (_view == nullptr)
        {
            result += "none";
        }
        else
        {
            const Av::Rect bounds = _view->Bounds();
            result += ::MphRead::NativeRuntime::ToString(bounds.Width, "0")
                + "x" + ::MphRead::NativeRuntime::ToString(bounds.Height, "0");
        }
        return result;
    }

    void UiSurface::Show(const Av::Controls::ControlPtr& view)
    {
        Invalidate();
        _view = view;
        ::MphRead::Mods::Input::GamepadContexts::MenuVisible(true);
        view->ClearValue(Av::Layout::Layoutable::WidthProperty);
        view->ClearValue(Av::Layout::Layoutable::HeightProperty);
        view->HorizontalAlignment(Av::Layout::HorizontalAlignment::Stretch);
        view->VerticalAlignment(Av::Layout::VerticalAlignment::Stretch);
        _host->Child(view);
        ApplyScale();
        const Av::Controls::ControlPtr focusView = view;
        ::MphRead::NativeRuntime::Avalonia::Threading::Dispatcher::UIThread().Post(
            [focusView] { (void)focusView->Focus(); },
            ::MphRead::NativeRuntime::Avalonia::Threading::DispatcherPriority::Input);
        Tick();
    }

    void UiSurface::ApplyScale()
    {
        UiLayout::BakeScale = _factor;
        const Av::Media::TransformPtr transform = _host->LayoutTransform();
        const auto* current = dynamic_cast<const Av::Media::ScaleTransform*>(transform.get());
        if (current != nullptr && std::abs(current->ScaleY - _factor) < 0.0001
            && std::abs(current->ScaleX - _factor) < 0.0001)
        {
            return;
        }
        _host->LayoutTransform(std::make_shared<Av::Media::ScaleTransform>(_factor, _factor));
    }

    void UiSurface::Hide()
    {
        _view.reset();
        ::MphRead::Mods::Input::GamepadContexts::MenuVisible(false);
        _host->Child(nullptr);
        ::MphRead::Mods::Render::UiOverlay::Visible(false);
        ::MphRead::NativeRuntime::Avalonia::Threading::Dispatcher::UIThread().RunJobs();
    }

    void UiSurface::Resize(std::int32_t width, std::int32_t height)
    {
        width = std::max(width, 1);
        height = std::max(height, 1);
        const double raster = Raster(width, height);
        const std::int32_t surfaceWidth = std::max(
            ::MphRead::NativeRuntime::MathRoundToInt32(width * raster), 1);
        const std::int32_t surfaceHeight = std::max(
            ::MphRead::NativeRuntime::MathRoundToInt32(height * raster), 1);
        const double factor = Factor(width, height) * raster;
        if (surfaceWidth == _pixelWidth && surfaceHeight == _pixelHeight && factor == _factor)
        {
            return;
        }
        Invalidate();
        _pixelWidth = surfaceWidth;
        _pixelHeight = surfaceHeight;
        _raster = raster;
        if (factor != _factor)
        {
            std::string message = "screens at " + ::MphRead::NativeRuntime::ToString(factor, "0.###")
                + "x (" + ::MphRead::NativeRuntime::ToString(width) + "x"
                + ::MphRead::NativeRuntime::ToString(height) + " pixels";
            if (raster < 1.0)
            {
                message += ", rasterised at " + ::MphRead::NativeRuntime::ToString(surfaceWidth)
                    + "x" + ::MphRead::NativeRuntime::ToString(surfaceHeight);
            }
            message += ")";
            ::MphRead::Mods::DebugLog::Line("ui", message);
            _factor = factor;
        }
        ApplyScale();
        _impl.SetClientSize(Av::Size{static_cast<double>(surfaceWidth), static_cast<double>(surfaceHeight)});
        ::MphRead::NativeRuntime::Avalonia::Threading::Dispatcher::UIThread().RunJobs();
    }

    double UiSurface::Factor(std::int32_t width, std::int32_t height)
    {
        return UiLayout::Factor(width, height);
    }

    double UiSurface::Raster(std::int32_t width, std::int32_t height)
    {
        if (UseNativeRaster)
        {
            return 1.0;
        }
        const double fits = std::min(RasterCapWidth / std::max(width, 1),
            RasterCapHeight / std::max(height, 1));
        if (fits >= 1.0)
        {
            return 1.0;
        }
        return std::max(std::floor(fits * 16.0) / 16.0, 0.25);
    }

    bool UiSurface::NativeRaster() noexcept
    {
        (void)FrameClock();
        return UseNativeRaster;
    }

    void UiSurface::NativeRaster(bool value) noexcept
    {
        (void)FrameClock();
        UseNativeRaster = value;
    }

    void UiSurface::RequestFrame(std::function<void()> step, bool idling)
    {
        (void)FrameClock();
        std::shared_ptr<UiSurface> surface = CurrentSurface;
        if (surface == nullptr)
        {
            ::MphRead::NativeRuntime::Avalonia::Threading::Dispatcher::UIThread().Post(
                std::move(step),
                ::MphRead::NativeRuntime::Avalonia::Threading::DispatcherPriority::Render);
            return;
        }
        {
            const std::lock_guard<std::mutex> guard(PendingFramesLock);
            PendingFrames.push_back(step);
        }
        surface->Invalidate(true, idling);
    }

    void UiSurface::RunPending()
    {
        {
            const std::lock_guard<std::mutex> guard(PendingFramesLock);
            if (PendingFrames.empty())
            {
                return;
            }
            RunningFrames.insert(RunningFrames.end(), PendingFrames.begin(), PendingFrames.end());
            PendingFrames.clear();
        }
        for (const std::function<void()>& step : RunningFrames)
        {
            step();
        }
        RunningFrames.clear();
    }

    void UiSurface::Invalidate(bool animation, bool idling)
    {
        _dirty = true;
        if (animation && _animOnly && !idling)
        {
            _animIdle = false;
        }
        if (!animation)
        {
            _touchedAt = FrameClock().Elapsed().TotalMilliseconds();
            _animOnly = false;
            return;
        }
        if (!_animOnly)
        {
            _animOnly = true;
            _animIdle = idling;
        }
        else if (!idling)
        {
            _animIdle = false;
        }
    }

    void UiSurface::Report(double now)
    {
        if (_ticks == 0 || now - _reportedAt < 1000.0)
        {
            return;
        }
        const std::string message = ::MphRead::NativeRuntime::ToString(_ticks) + " frames, "
            + ::MphRead::NativeRuntime::ToString(_redraws) + " redraws, jobs "
            + ::MphRead::NativeRuntime::ToString(_jobs, "0.0") + " ms, draw "
            + ::MphRead::NativeRuntime::ToString(_drawMs, "0.0") + " ms, upload "
            + ::MphRead::NativeRuntime::ToString(_uploadMs, "0.0") + " ms (per second)";
        ::MphRead::Mods::DebugLog::Line("ui", message);
        _ticks = 0;
        _redraws = 0;
        _jobs = 0.0;
        _drawMs = 0.0;
        _uploadMs = 0.0;
        _reportedAt = now;
    }

    void UiSurface::Tick()
    {
        if (_view == nullptr)
        {
            ::MphRead::Mods::Render::UiOverlay::Visible(false);
            return;
        }
        ::MphRead::Mods::Input::GamepadDesktop::Poll();
        _gamepad.Update(*_view);
        RunPending();
        ApplyScale();

        const bool measuring = ::MphRead::Mods::DebugLog::Active();
        ::MphRead::NativeRuntime::Stopwatch clock = ::MphRead::NativeRuntime::Stopwatch::StartNew();
        ::MphRead::NativeRuntime::Avalonia::Threading::Dispatcher::UIThread().RunJobs();
        if (measuring)
        {
            _jobs += clock.Elapsed().TotalMilliseconds();
            _ticks++;
        }

        const double now = FrameClock().Elapsed().TotalMilliseconds();
        const double since = now - _drawnAt;
        const double gap = _dirty ? (_animOnly ? (_animIdle ? IdleAnimGap : AnimGap) : BusyGap)
            : now - _touchedAt < SettleMs ? IdleGap : RestingGap;
        if (since < gap)
        {
            ::MphRead::Mods::Render::UiOverlay::Visible(true);
            return;
        }
        _dirty = false;
        _animOnly = false;
        _animIdle = false;
        _drawnAt = now;
        _redraws++;
        Report(now);

        clock.Restart();
        const std::int32_t drawn = _impl.Drawn();
        UiRenderTimer::Pump(_impl);
        _drawMs += clock.Elapsed().TotalMilliseconds();
        clock.Restart();
        if (_impl.Drawn() != drawn && _impl.RhiTexture() != nullptr)
        {
            ::MphRead::Mods::Render::UiOverlay::UseTexture(
                *_impl.RhiTexture(), _impl.PixelWidth(), _impl.PixelHeight());
            _uploadMs += clock.Elapsed().TotalMilliseconds();
        }
        else if (_impl.Drawn() != drawn && _impl.TextureId() != 0)
        {
            ::MphRead::Mods::Render::UiOverlay::UseTexture(
                _impl.TextureId(), _impl.PixelWidth(), _impl.PixelHeight());
            _uploadMs += clock.Elapsed().TotalMilliseconds();
        }
        ::MphRead::Mods::Render::UiOverlay::Visible(true);
    }

    void UiSurface::PointerMoved(double x, double y)
    {
        Deck::DrivingByPointer();
        Invalidate();
        if (_view == nullptr)
        {
            return;
        }
        _pointer = Av::Point{x * _raster, y * _raster};
        _impl.MouseMove(_pointer, _modifiers);
    }

    void UiSurface::PointerButton(Av::Input::MouseButton button, bool down)
    {
        Deck::DrivingByPointer();
        Invalidate();
        if (_view == nullptr)
        {
            return;
        }
        Av::Input::RawInputModifiers flag = Av::Input::RawInputModifiers::LeftMouseButton;
        if (button == Av::Input::MouseButton::Right)
        {
            flag = Av::Input::RawInputModifiers::RightMouseButton;
        }
        else if (button == Av::Input::MouseButton::Middle)
        {
            flag = Av::Input::RawInputModifiers::MiddleMouseButton;
        }
        if (down)
        {
            _modifiers |= flag;
            _impl.MouseDown(_pointer, button, _modifiers);
        }
        else
        {
            _modifiers &= ~flag;
            _impl.MouseUp(_pointer, button, _modifiers);
        }
    }

    bool UiSurface::ClickOn(const ControlPredicate& match)
    {
        if (_view == nullptr)
        {
            return false;
        }
        for (Av::Visual* visual : _view->GetVisualDescendants())
        {
            auto* control = dynamic_cast<Av::Controls::Control*>(visual);
            if (control == nullptr || !match(*control))
            {
                continue;
            }
            const Av::Rect bounds = control->Bounds();
            const std::optional<Av::Point> centre = control->TranslatePoint(
                Av::Point{bounds.Width / 2.0, bounds.Height / 2.0}, _view.get());
            if (!centre.has_value() || !Covers(*control, *centre))
            {
                continue;
            }
            PointerMoved(centre->X * _factor / _raster, centre->Y * _factor / _raster);
            PointerButton(Av::Input::MouseButton::Left, true);
            PointerButton(Av::Input::MouseButton::Left, false);
            return true;
        }
        return false;
    }

    bool UiSurface::HoverOn(const ControlPredicate& match)
    {
        if (_view == nullptr)
        {
            return false;
        }
        for (Av::Visual* visual : _view->GetVisualDescendants())
        {
            auto* control = dynamic_cast<Av::Controls::Control*>(visual);
            if (control == nullptr || !match(*control))
            {
                continue;
            }
            const Av::Rect bounds = control->Bounds();
            const std::optional<Av::Point> centre = control->TranslatePoint(
                Av::Point{bounds.Width / 2.0, bounds.Height / 2.0}, _view.get());
            if (!centre.has_value() || !Covers(*control, *centre))
            {
                continue;
            }
            PointerMoved(centre->X * _factor / _raster, centre->Y * _factor / _raster);
            return true;
        }
        return false;
    }

    bool UiSurface::Covers(Av::Controls::Control& control, Av::Point point)
    {
        if (_view == nullptr)
        {
            return false;
        }
        const std::optional<Av::Point> rootPoint = _view->TranslatePoint(point, &_impl.Root());
        if (!rootPoint.has_value())
        {
            return false;
        }
        Av::Input::InputElement* hit = _impl.Root().InputHitTest(*rootPoint);
        for (Av::Visual* visual = dynamic_cast<Av::Visual*>(hit); visual != nullptr;
            visual = visual->GetVisualParent())
        {
            if (visual == &control)
            {
                return true;
            }
        }
        return false;
    }

    void UiSurface::PointerWheel(double deltaX, double deltaY)
    {
        Deck::DrivingByPointer();
        Invalidate();
        if (_view == nullptr)
        {
            return;
        }
        _impl.MouseWheel(_pointer, Av::Vector{deltaX, deltaY}, _modifiers);
    }

    Av::Input::Key UiSurface::Translate(::OpenTK::Windowing::GraphicsLibraryFramework::Keys key)
    {
        const std::int32_t value = KeyValue(key);
        if (value >= 65 && value <= 90)
        {
            return static_cast<Av::Input::Key>(static_cast<std::int32_t>(Av::Input::Key::A) + value - 65);
        }
        if (value >= 48 && value <= 57)
        {
            return static_cast<Av::Input::Key>(static_cast<std::int32_t>(Av::Input::Key::D0) + value - 48);
        }
        if (value >= 290 && value <= 301)
        {
            return static_cast<Av::Input::Key>(static_cast<std::int32_t>(Av::Input::Key::F1) + value - 290);
        }
        if (value >= 320 && value <= 329)
        {
            return static_cast<Av::Input::Key>(static_cast<std::int32_t>(Av::Input::Key::NumPad0) + value - 320);
        }
        switch (value)
        {
        case 256: return Av::Input::Key::Escape;
        case 257:
        case 335: return Av::Input::Key::Return;
        case 258: return Av::Input::Key::Tab;
        case 259: return Av::Input::Key::Back;
        case 261: return Av::Input::Key::Delete;
        case 260: return Av::Input::Key::Insert;
        case 268: return Av::Input::Key::Home;
        case 269: return Av::Input::Key::End;
        case 266: return Av::Input::Key::PageUp;
        case 267: return Av::Input::Key::PageDown;
        case 263: return Av::Input::Key::Left;
        case 262: return Av::Input::Key::Right;
        case 265: return Av::Input::Key::Up;
        case 264: return Av::Input::Key::Down;
        case 32: return Av::Input::Key::Space;
        case 45: return Av::Input::Key::OemMinus;
        case 61: return Av::Input::Key::OemPlus;
        case 44: return Av::Input::Key::OemComma;
        case 46: return Av::Input::Key::OemPeriod;
        case 47: return Av::Input::Key::Oem2;
        case 92: return Av::Input::Key::Oem5;
        case 59: return Av::Input::Key::Oem1;
        case 39: return Av::Input::Key::OemQuotes;
        case 96: return Av::Input::Key::OemTilde;
        case 91: return Av::Input::Key::Oem4;
        case 93: return Av::Input::Key::Oem6;
        case 340: return Av::Input::Key::LeftShift;
        case 344: return Av::Input::Key::RightShift;
        case 341: return Av::Input::Key::LeftCtrl;
        case 345: return Av::Input::Key::RightCtrl;
        case 342: return Av::Input::Key::LeftAlt;
        case 346: return Av::Input::Key::RightAlt;
        default: return Av::Input::Key::None;
        }
    }

    void UiSurface::KeyDown(::OpenTK::Windowing::GraphicsLibraryFramework::Keys key,
        Av::Input::RawInputModifiers modifiers)
    {
        Deck::DrivingByKeyboard();
        Invalidate();
        if (_view == nullptr)
        {
            return;
        }
        _modifiers = modifiers | (_modifiers & Av::Input::RawInputModifiers::LeftMouseButton);
        const Av::Input::Key mapped = Translate(key);
        if (mapped != Av::Input::Key::None)
        {
            _impl.KeyPress(mapped, _modifiers, 0, std::string{});
        }
    }

    void UiSurface::KeyUp(::OpenTK::Windowing::GraphicsLibraryFramework::Keys key,
        Av::Input::RawInputModifiers modifiers)
    {
        Invalidate();
        if (_view == nullptr)
        {
            return;
        }
        _modifiers = modifiers | (_modifiers & Av::Input::RawInputModifiers::LeftMouseButton);
        const Av::Input::Key mapped = Translate(key);
        if (mapped != Av::Input::Key::None)
        {
            _impl.KeyRelease(mapped, _modifiers, 0, std::string{});
        }
    }

    void UiSurface::TextInput(const std::string& text)
    {
        Invalidate();
        if (_view == nullptr || text.empty())
        {
            return;
        }
        _impl.TextInput(text);
    }
}
