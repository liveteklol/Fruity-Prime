#pragma once

#include "../../../NativeRuntime/Avalonia/Avalonia.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace MphRead::Mods::Launcher::Gui
{
    namespace Av = ::MphRead::NativeRuntime::Avalonia;

    class UiTopLevelImpl;

    // The native TopLevel renders only when the game frame pumps it. This is
    // the headless backend's compositor timer adapted to that explicit loop.
    class UiRenderTimer final
    {
    public:
        UiRenderTimer() = delete;

        static void Install() noexcept;
        static void Pump(UiTopLevelImpl& topLevel);
    };

    // Avalonia's platform TopLevelImpl backed by the in-tree native TopLevel.
    class UiTopLevelImpl final
    {
    public:
        UiTopLevelImpl() = default;

        [[nodiscard]] static std::unique_ptr<UiTopLevelImpl> TryCreate();

        [[nodiscard]] Av::EmbeddableControlRoot& Root() noexcept { return _root; }
        [[nodiscard]] Av::Size ClientSize() const noexcept { return _root.ClientSize(); }
        void SetClientSize(Av::Size size) { _root.SetClientSize(size); }
        void Prepare() { _root.Prepare(); }
        void StartRendering() { _root.StartRendering(); }
        [[nodiscard]] bool Render() { return _root.Render(); }
        void GpuRendering(bool value) noexcept { _root.GpuRendering(value); }
        void ReleaseGpu() { _root.ReleaseGpu(); }

        [[nodiscard]] std::int32_t TextureId() const noexcept { return _root.TextureId(); }
        [[nodiscard]] const ::MphRead::NativeRuntime::Rhi::Texture* RhiTexture() const noexcept
        {
            return _root.RhiTexture();
        }
        [[nodiscard]] const std::uint8_t* Pixels() const noexcept { return _root.Pixels(); }
        [[nodiscard]] std::int32_t PixelWidth() const noexcept { return _root.PixelWidth(); }
        [[nodiscard]] std::int32_t PixelHeight() const noexcept { return _root.PixelHeight(); }
        [[nodiscard]] std::int32_t Drawn() const noexcept { return _root.Drawn(); }
        void Painted(std::function<void()> callback) { _root.Painted = std::move(callback); }

        void MouseMove(Av::Point point, Av::Input::RawInputModifiers modifiers);
        void MouseDown(Av::Point point, Av::Input::MouseButton button, Av::Input::RawInputModifiers modifiers);
        void MouseUp(Av::Point point, Av::Input::MouseButton button, Av::Input::RawInputModifiers modifiers);
        void MouseWheel(Av::Point point, Av::Vector delta, Av::Input::RawInputModifiers modifiers);
        void TouchBegin(Av::Point point, std::int64_t id);
        void TouchUpdate(Av::Point point, std::int64_t id);
        void TouchEnd(Av::Point point, std::int64_t id);
        void KeyPress(Av::Input::Key key, Av::Input::RawInputModifiers modifiers, std::int32_t physicalKey,
            std::optional<std::string> keySymbol);
        void KeyRelease(Av::Input::Key key, Av::Input::RawInputModifiers modifiers, std::int32_t physicalKey,
            std::optional<std::string> keySymbol);
        void TextInput(const std::string& text);

    private:
        Av::EmbeddableControlRoot _root;
    };
}
