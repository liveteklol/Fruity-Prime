#pragma once

#include <QtCore/QObject>
#include <QtCore/QSize>

#include "../../MphRead.Native/Mods/Input/GamepadUiRouter.hpp"

#include <memory>

class QEvent;
class QQmlEngine;
class QQuickItem;
class QQuickRenderControl;
class QQuickWindow;
class QVulkanInstance;
class QWindow;

namespace MphRead::NativeRuntime::Rhi
{
    class Texture;
}

namespace MphRead::Qt
{
    class ShellBridge;

    // The menus: one Qt Quick scene rendered offscreen by QQuickRenderControl
    // into a texture on the game's own graphics device, which UiOverlay
    // composites over the frame. On the GPU whichever renderer is running:
    // on OpenGL into a GL texture of the game's context, on Vulkan into an RHI
    // texture of the game's own VkDevice, drawn by Qt Quick on that device and
    // queue. Nothing is rendered unless the scene changed, and nothing is
    // composited while no page is showing, so a match with the menus closed
    // pays nothing for them. A host belongs to one window and one device: a
    // renderer switch destroys it before the device goes and makes another.
    class UiHost final : public QObject
    {
    public:
        UiHost(QWindow& gameWindow, ShellBridge& bridge);
        ~UiHost() override;

        // Once per frame, with the game's context current. Renders if dirty
        // and hands the result to UiOverlay.
        void Tick(int framebufferWidth, int framebufferHeight);

        // An input event from the game window, in its coordinates.
        void Deliver(QEvent& event);

        void MarkDirty() noexcept { _dirty = true; }

    private:
        bool Initialise();
        bool FinishInitialise();
        void Resized(QSize pixels);
        void EnsureTarget(QSize pixels);
        void ReleaseTarget();
        void DumpOnce();
        // The pad in the menus: GamepadNavigation's actions, as keys.
        void PadActions();
        void SendKey(int key);
        [[nodiscard]] bool PadAccept();
        void Navigated(int key, bool accepted);

        QWindow& _gameWindow;
        ShellBridge& _bridge;
        [[nodiscard]] bool InitialiseVulkan();
        void EnsureVulkanTarget(QSize pixels);
        void RenderVulkan();

        // Vulkan: Qt's view of the RHI's own instance, and the texture drawn into.
        bool _vulkan = false;
        std::unique_ptr<QVulkanInstance> _vulkanInstance;
        std::unique_ptr<::MphRead::NativeRuntime::Rhi::Texture> _rhiTexture;
        std::unique_ptr<QQuickRenderControl> _control;
        std::unique_ptr<QQuickWindow> _window;
        std::unique_ptr<QQmlEngine> _engine;
        QQuickItem* _root = nullptr;
        bool _initialised = false;
        bool _failed = false;
        bool _dirty = true;
        unsigned _texture = 0;
        QSize _targetSize{};
        ::MphRead::Mods::Input::GamepadUiRouter _router;
        bool _menuVisible = false;
    };
}
