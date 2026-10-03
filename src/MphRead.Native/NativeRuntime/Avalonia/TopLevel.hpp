#pragma once

// A top level with no window behind it: Avalonia's EmbeddableControlRoot over
// the launcher's own ITopLevelImpl. It lays its content out at the size it is
// given, draws it with the Skia canvas into a buffer of premultiplied RGBA
// that it keeps across frames, and takes its input as the raw events a
// windowing system would have delivered.

#include "Panels.hpp"
#include "../Skia/Skia.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>

namespace MphRead::NativeRuntime::Avalonia
{
    namespace Controls
    {
        enum class WindowTransparencyLevel : std::int32_t
        {
            Transparent
        };
    }

    namespace Styling
    {
        enum class ThemeVariant : std::int32_t
        {
            Dark
        };
    }

    class TopLevel;

    namespace Input
    {
        class FocusManager final
        {
        public:
            explicit FocusManager(TopLevel& owner)
                : _owner(owner)
            {
            }
            [[nodiscard]] IInputElement* GetFocusedElement() const;
            void ClearFocus();

        private:
            TopLevel& _owner;
        };
    }

    class TopLevel : public Controls::ContentControl
    {
    public:
        TopLevel();
        ~TopLevel() override;

        // TopLevel.GetTopLevel(visual).
        [[nodiscard]] static TopLevel* GetTopLevel(const Visual* visual);

        [[nodiscard]] Size ClientSize() const noexcept { return _clientSize; }
        void SetClientSize(Size size);
        [[nodiscard]] double RenderScaling() const noexcept { return 1.0; }

        // Window.Prepare: the first layout pass. StartRendering: draw from
        // now on.
        void Prepare();
        void StartRendering();

        // A popup stays in this tree so it is composited into the same surface.
        void AddOverlay(const Controls::ControlPtr& overlay);
        void RemoveOverlay(const Controls::Control* overlay);

        void InvalidateRender() noexcept
        {
            _renderDirty = true;
            _fullRenderDirty = true;
        }
        void InvalidateRender(const Rect& bounds) noexcept;
        void InvalidateLayout() noexcept
        {
            _layoutDirty = true;
            _renderDirty = true;
            _fullRenderDirty = true;
        }
        [[nodiscard]] bool NeedsRender() const noexcept { return _renderDirty || _layoutDirty; }
        void ExecuteLayoutPass();

        // Lay out what changed and draw it if anything did. True when a pass
        // ran into the retained Ganesh surface.
        bool Render();

        void GpuRendering(bool value) noexcept
        {
            if (_gpuRendering != value)
            {
                _gpuRendering = value;
                InvalidateRender();
            }
        }
        [[nodiscard]] bool GpuRendering() const noexcept { return _gpuRendering; }
        // The window is going for a renderer switch: Ganesh's context and
        // surface go with it (destroyed as a GpuSurface is, in order), and
        // the next frame makes them again on whatever the new window is.
        void ReleaseGpu()
        {
            {
                Skia::GpuSurface old(std::move(_surface));
            }
            _surface = Skia::GpuSurface();
            InvalidateRender();
        }
        [[nodiscard]] std::int32_t TextureId() const noexcept
        {
            return _gpuRendering ? _surface.TextureId() : 0;
        }
        [[nodiscard]] const ::MphRead::NativeRuntime::Rhi::Texture* RhiTexture() const noexcept
        {
            return _gpuRendering ? _surface.RhiTexture() : nullptr;
        }
        [[nodiscard]] const std::uint8_t* Pixels() const noexcept
        {
            return _gpuRendering ? nullptr : _pixels.Pixels();
        }
        [[nodiscard]] std::int32_t PixelWidth() const noexcept
        {
            return _gpuRendering ? _surface.Width() : _pixels.Width();
        }
        [[nodiscard]] std::int32_t PixelHeight() const noexcept
        {
            return _gpuRendering ? _surface.Height() : _pixels.Height();
        }
        [[nodiscard]] std::int32_t Drawn() const noexcept { return _drawn; }
        // Called when a pass has just finished into the GPU surface.
        std::function<void()> Painted{};

        // ---- input, as the windowing system would deliver it
        void MouseMove(Point point, Input::RawInputModifiers modifiers);
        void MouseDown(Point point, Input::MouseButton button, Input::RawInputModifiers modifiers);
        void MouseUp(Point point, Input::MouseButton button, Input::RawInputModifiers modifiers);
        void MouseWheel(Point point, Vector delta, Input::RawInputModifiers modifiers);
        void TouchBegin(Point point, std::int64_t id);
        void TouchUpdate(Point point, std::int64_t id);
        void TouchEnd(Point point, std::int64_t id);
        void KeyPress(Input::Key key, Input::RawInputModifiers modifiers, std::optional<std::string> keySymbol = std::nullopt);
        void KeyRelease(Input::Key key, Input::RawInputModifiers modifiers, std::optional<std::string> keySymbol = std::nullopt);
        void KeyPress(Input::Key key, Input::RawInputModifiers modifiers, std::int32_t physicalKey,
            std::optional<std::string> keySymbol);
        void KeyRelease(Input::Key key, Input::RawInputModifiers modifiers, std::int32_t physicalKey,
            std::optional<std::string> keySymbol);
        void TextInput(const std::string& text);

        // ---- focus
        [[nodiscard]] Input::InputElement* FocusedElement() const noexcept { return _focused; }
        void SetFocusedElement(Input::InputElement* element, Input::NavigationMethod method,
            Input::KeyModifiers modifiers);
        [[nodiscard]] Input::FocusManager* FocusManager() noexcept { return &_focusManager; }

        // The cursor the pointer is over, for the host to show.
        [[nodiscard]] Input::StandardCursorType CurrentCursor() const noexcept { return _cursor; }
        [[nodiscard]] Input::IPointer& Mouse() noexcept { return *_mouse; }

        // The deepest input element under a point, as InputHitTest finds it.
        [[nodiscard]] Input::InputElement* InputHitTest(Point point) const;

        // Called by the tree.
        void ElementStateChanged(Input::InputElement& element);
        void ElementDetached(Input::InputElement& element);

        // Draw a subtree into a canvas, as RenderTargetBitmap.Render does.
        static void RenderVisual(Visual& visual, Media::DrawingContext& context, bool isRoot,
            const Rect* damage = nullptr, bool updateRenderedContent = true, Matrix parentToRoot = Matrix::Identity(),
            const Rect* visibleRegion = nullptr);

        std::any TransparencyLevelHint{};
        std::any RequestedThemeVariant{};

    private:
        void UpdatePointerOver(Input::IPointer& pointer, Point point, Input::RawInputModifiers modifiers);
        [[nodiscard]] Input::InputElement* PointerTarget(Input::IPointer& pointer, Point point) const;
        void RaisePointer(Input::IPointer& pointer, const Interactivity::RoutedEvent& routedEvent, Point point,
            Input::RawInputModifiers modifiers, Input::PointerUpdateKind kind);
        void RaiseKey(const Interactivity::RoutedEvent& routedEvent, Input::Key key, Input::RawInputModifiers modifiers,
            std::int32_t physicalKey, std::optional<std::string> keySymbol);
        [[nodiscard]] std::uint64_t Timestamp() const;

        Size _clientSize{1280, 768};
        Skia::GpuSurface _surface;
        Skia::Bitmap _pixels;
        bool _gpuRendering = true;
        std::int32_t _drawn = 0;
        bool _renderDirty = true;
        bool _fullRenderDirty = true;
        bool _layoutDirty = true;
        bool _rendering = false;
        Rect _renderDamage{};
        Input::InputElement* _focused = nullptr;
        Input::FocusManager _focusManager{*this};
        std::unique_ptr<Input::IPointer> _mouse;
        std::map<std::int64_t, std::unique_ptr<Input::IPointer>> _touches;
        std::vector<Controls::ControlPtr> _overlays;
        // The chain the pointer is over, innermost first.
        std::vector<Input::InputElement*> _pointerOver;
        Input::StandardCursorType _cursor = Input::StandardCursorType::Arrow;
        // Double clicks: when and where the last press landed.
        std::chrono::steady_clock::time_point _lastPress{};
        Point _lastPressPoint{};
        std::int32_t _clickCount = 0;
        // TouchDevice tracks click count independently and permits the
        // platform's larger double-tap rectangle (50 DIP by default).
        std::chrono::steady_clock::time_point _lastTouchPress{};
        Point _lastTouchPressPoint{};
        std::int32_t _touchClickCount = 0;
        std::chrono::steady_clock::time_point _start = std::chrono::steady_clock::now();
        // Gestures: what the last press landed on, for Tapped and DoubleTapped.
        Input::InputElement* _pressedOn = nullptr;
        Input::InputElement* _firstTapOn = nullptr;
        Input::InputElement* _doubleTappedOn = nullptr;
    };

    // Avalonia.Controls.Embedding.EmbeddableControlRoot.
    class EmbeddableControlRoot : public TopLevel
    {
    };

    namespace Media::Imaging
    {
        // RenderTargetBitmap: a visual drawn into a bitmap of its own.
        class RenderTargetBitmap final : public Bitmap
        {
        public:
            RenderTargetBitmap(Avalonia::PixelSize size, Vector dpi = {96, 96});
            void Render(Visual& visual);
            // A context drawing straight into this bitmap.
            [[nodiscard]] std::unique_ptr<DrawingContext> CreateDrawingContext();
        };
    }
}
