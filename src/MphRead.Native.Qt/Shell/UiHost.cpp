#include "UiHost.hpp"

#include "FocusNav.hpp"
#include "HunterStandItem.hpp"
#include "QmlTypes.hpp"

#include "../../MphRead.Native/Mods/Render/LauncherHunter.hpp"

#include "ShellBridge.hpp"

#include "../../MphRead.Native/Mods/Render/UiOverlay.hpp"
#include "../../MphRead.Native/NativeRuntime/OpenTK/GlStateGuard.hpp"
#include "../../MphRead.Native/NativeRuntime/Rhi/Resources.hpp"
#include "../../MphRead.Native/NativeRuntime/Rhi/SceneBackend.hpp"
#if defined(FRUITY_HAS_VULKAN)
#include "../../MphRead.Native/NativeRuntime/Rhi/Vulkan/VulkanGraphicsDevice.hpp"
#include <QtGui/QVulkanInstance>
#include <vulkan/vulkan.h>
#endif

#include <QtCore/QCoreApplication>
#include <QtCore/QUrl>
#include <QtGui/QImage>
#include <QtGui/QInputMethodEvent>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QOpenGLContext>
#include <QtGui/QOpenGLFunctions>
#include <QtGui/QWheelEvent>
#include <QtGui/QWindow>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngine>
#include <QtQuick/QQuickGraphicsDevice>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickOpenGLUtils>
#include <QtQuick/QQuickRenderControl>
#include <QtQuick/QQuickRenderTarget>
#include <QtQuick/QQuickWindow>
#include <QtQuick/QSGRendererInterface>

#include "../../MphRead.Native/Mods/Input/GamepadManager.hpp"
#include "../../MphRead.Native/NativeRuntime/System/Runtime.hpp"

#include <iostream>

namespace MphRead::Qt
{
    namespace
    {
        // The offscreen scene borrows the game window for its screen, device
        // pixel ratio and input method (the Android/iOS keyboard for chat).
        class RenderControl final : public QQuickRenderControl
        {
        public:
            explicit RenderControl(QWindow& window) : _window(window) {}

            QWindow* renderWindow(QPoint* offset) override
            {
                if (offset != nullptr)
                {
                    *offset = QPoint(0, 0);
                }
                return &_window;
            }

        private:
            QWindow& _window;
        };
    }

    UiHost::UiHost(QWindow& gameWindow, ShellBridge& bridge)
        : _gameWindow(gameWindow), _bridge(bridge)
    {
        using ::MphRead::Mods::Input::UiAction;
        _router.Action.Add([this](UiAction action)
        {
            switch (action)
            {
            case UiAction::Up: SendKey(::Qt::Key_Up); break;
            case UiAction::Down: SendKey(::Qt::Key_Down); break;
            case UiAction::Left: SendKey(::Qt::Key_Left); break;
            case UiAction::Right: SendKey(::Qt::Key_Right); break;
            case UiAction::Accept:
                if (!PadAccept())
                {
                    SendKey(::Qt::Key_Return);
                }
                break;
            case UiAction::Back: SendKey(::Qt::Key_Escape); break;
            case UiAction::PreviousTab: _bridge.StepTabs(-1); break;
            case UiAction::NextTab: _bridge.StepTabs(1); break;
            case UiAction::PageUp: FocusNav::Page(*_window, false); break;
            case UiAction::PageDown: FocusNav::Page(*_window, true); break;
            }
        });
    }

    void UiHost::SendKey(int key)
    {
        if (!_initialised)
        {
            return;
        }
        QKeyEvent press(QEvent::KeyPress, key, ::Qt::NoModifier);
        QCoreApplication::sendEvent(_window.get(), &press);
        Navigated(key, press.isAccepted());
        QKeyEvent release(QEvent::KeyRelease, key, ::Qt::NoModifier);
        QCoreApplication::sendEvent(_window.get(), &release);
    }

    bool UiHost::PadAccept()
    {
        // A control with its own answer to the pad's A (a text field opens
        // the on-screen keyboard, a key row hands over to its pad row).
        if (!_initialised)
        {
            return false;
        }
        const bool handled = FocusNav::PadAccept(*_window);
        if (handled)
        {
            _bridge.KeyboardDriving();
        }
        return handled;
    }

    void UiHost::Navigated(int key, bool accepted)
    {
        _bridge.KeyboardDriving();
        if (accepted)
        {
            return;
        }
        FocusNav::Unhandled(*_window, key);
    }

    void UiHost::PadActions()
    {
        namespace Input = ::MphRead::Mods::Input;
        if (!Input::GamepadContexts::Focused())
        {
            _router.Reset();
            return;
        }
        _router.Update(Input::GamepadManager::Snapshot(),
            Input::GamepadContexts::Capturing() ? Input::GamepadContext::BindingCapture : Input::GamepadContext::Menu,
            ::MphRead::NativeRuntime::EnvironmentTickCount64());
    }

    UiHost::~UiHost()
    {
        _root = nullptr;
        _engine.reset();
        ReleaseTarget();
        // Qt's own resources on the device go with the window and the
        // control, while the device they were made on still exists.
        _window.reset();
        _control.reset();
        _vulkanInstance.reset();
    }

    bool UiHost::Initialise()
    {
        if (_initialised || _failed)
        {
            return _initialised;
        }
        namespace Rhi = ::MphRead::NativeRuntime::Rhi;
        _vulkan = Rhi::ScenePresentsWindow()
            && Rhi::SceneDevice().GetBackend() == Rhi::GraphicsBackend::Vulkan;
        if (_vulkan)
        {
            if (!InitialiseVulkan())
            {
                _failed = true;
                return false;
            }
            return FinishInitialise();
        }
        QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
        QOpenGLContext* const context = QOpenGLContext::currentContext();
        if (context == nullptr)
        {
            _failed = true;
            std::cout << "[ui] no current OpenGL context for the menus\n";
            return false;
        }
        _control = std::make_unique<RenderControl>(_gameWindow);
        _window = std::make_unique<QQuickWindow>(_control.get());
        _window->setColor(::Qt::transparent);
        _window->setGraphicsDevice(QQuickGraphicsDevice::fromOpenGLContext(context));
        if (!_control->initialize())
        {
            _failed = true;
            std::cout << "[ui] Qt Quick could not start on the game's context\n";
            return false;
        }
        return FinishInitialise();
    }

    bool UiHost::FinishInitialise()
    {
        QObject::connect(_control.get(), &QQuickRenderControl::renderRequested, this,
            [this]() { _dirty = true; });
        QObject::connect(_control.get(), &QQuickRenderControl::sceneChanged, this,
            [this]() { _dirty = true; });

        _engine = std::make_unique<QQmlEngine>();
        RegisterQmlTypes();
        _engine->rootContext()->setContextProperty(QStringLiteral("shell"), &_bridge);
        QQmlComponent component(_engine.get(),
            QUrl(QStringLiteral("qrc:/qt/qml/FruityPrime/Ui/Main.qml")));
        QObject* const object = component.create();
        _root = qobject_cast<QQuickItem*>(object);
        if (_root == nullptr)
        {
            _failed = true;
            std::cout << "[ui] the menus could not be loaded: "
                      << component.errorString().toStdString() << '\n';
            delete object;
            return false;
        }
        _root->setParentItem(_window->contentItem());
        _initialised = true;
        return true;
    }

    void UiHost::Resized(QSize pixels)
    {
        const qreal ratio = _gameWindow.devicePixelRatio();
        const QSize logical(qRound(pixels.width() / ratio), qRound(pixels.height() / ratio));
        _window->resize(logical);
        _window->contentItem()->setSize(logical);
        _root->setSize(logical);
        _dirty = true;
    }

    void UiHost::EnsureTarget(QSize pixels)
    {
        if (_vulkan)
        {
            EnsureVulkanTarget(pixels);
            return;
        }
        if (_rhiTexture != nullptr && pixels == _targetSize)
        {
            return;
        }
        ReleaseTarget();
        // A texture of the renderer's own device, composited by UiOverlay the
        // way every UI texture is; on OpenGL its handle is the GL name Qt
        // Quick draws into, on the context the two share.
        namespace Rhi = ::MphRead::NativeRuntime::Rhi;
        _rhiTexture = Rhi::SceneDevice().CreateTexture(Rhi::TextureDesc{static_cast<std::uint32_t>(pixels.width()),
            static_cast<std::uint32_t>(pixels.height()), 1, 1, 1, 1, Rhi::TextureFormat::RGBA8Unorm,
            Rhi::TextureUsage::Sampled | Rhi::TextureUsage::ColorAttachment
                | Rhi::TextureUsage::TransferSrc | Rhi::TextureUsage::TransferDst});
        _texture = static_cast<unsigned>(_rhiTexture->Handle().value);
        _targetSize = pixels;
        QQuickRenderTarget target = QQuickRenderTarget::fromOpenGLTexture(_texture, pixels);
        // GL's rows run bottom up; the overlay takes every texture top row first.
        target.setMirrorVertically(true);
        _window->setRenderTarget(target);
        Resized(pixels);
    }

    void UiHost::ReleaseTarget()
    {
        if (_rhiTexture != nullptr)
        {
            // Qt lets go of the image before the RHI retires it.
            if (_window != nullptr)
            {
                _window->setRenderTarget(QQuickRenderTarget());
            }
            ::MphRead::Mods::Render::UiOverlay::Release();
            // The GL name, when there is one, is the RHI texture's own.
            _rhiTexture.reset();
            _texture = 0;
            _targetSize = QSize();
        }
    }

    void UiHost::Tick(int framebufferWidth, int framebufferHeight)
    {
        using ::MphRead::Mods::Render::UiOverlay;
        const bool showing = _bridge.Showing() && framebufferWidth > 0 && framebufferHeight > 0;
        if (showing != _menuVisible)
        {
            _menuVisible = showing;
            ::MphRead::Mods::Input::GamepadContexts::MenuVisible(showing);
            _router.Reset();
        }
        if (!showing)
        {
            UiOverlay::Visible(false);
            ::MphRead::Mods::Render::LauncherHunter::Wanted(false);
            return;
        }
        if (!Initialise())
        {
            UiOverlay::Visible(false);
            return;
        }
        EnsureTarget(QSize(framebufferWidth, framebufferHeight));
        PadActions();
        if (_dirty && _vulkan)
        {
            _dirty = false;
            RenderVulkan();
        }
        else if (_dirty)
        {
            _dirty = false;
            QOpenGLContext* const context = QOpenGLContext::currentContext();
            QSurface* const surface = context->surface();
            {
                // Qt Quick binds its own GL state; the game's renderer tracks
                // what it bound, so that comes back exactly as it was rather
                // than as GL's defaults.
                const ::OpenTK::Graphics::OpenGL::GlStateGuard guard;
                _control->polishItems();
                _control->beginFrame();
                _control->sync();
                _control->render();
                _control->endFrame();
                // An offscreen frame leaves the context current on Qt's own
                // fallback surface; the game draws to its window.
                context->makeCurrent(surface);
            }
            DumpOnce();
            UiOverlay::UseTexture(*_rhiTexture, _targetSize.width(), _targetSize.height());
        }
        UiOverlay::Visible(true);
        HunterStandItem::Publish(_window->width(), _window->height());
    }

    void UiHost::DumpOnce()
    {
        // FP_QT_UI_DUMP=path: the menus' texture as rendered, once, to tell a
        // scene problem from a compositing one.
        static const QString path = qEnvironmentVariable("FP_QT_UI_DUMP");
        static bool done = false;
        if (path.isEmpty() || done || _vulkan)
        {
            return;
        }
        done = true;
        QOpenGLFunctions* const gl = QOpenGLContext::currentContext()->functions();
        GLuint fbo = 0;
        gl->glGenFramebuffers(1, &fbo);
        gl->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _texture, 0);
        QImage image(_targetSize, QImage::Format_RGBA8888);
        gl->glReadPixels(0, 0, _targetSize.width(), _targetSize.height(), GL_RGBA,
            GL_UNSIGNED_BYTE, image.bits());
        gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
        gl->glDeleteFramebuffers(1, &fbo);
        image.save(path);
        std::cout << "[ui] dumped " << path.toStdString() << " status "
                  << gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) << '\n';
    }

    void UiHost::Deliver(QEvent& event)
    {
        if (!_initialised)
        {
            return;
        }
        // The offscreen window's coordinates are the game window's own. The
        // event is the game window's, so the scene gets its own copy.
        const std::unique_ptr<QEvent> copy(event.clone());
        QCoreApplication::sendEvent(_window.get(), copy.get());
        if (copy->type() == QEvent::KeyPress)
        {
            Navigated(static_cast<QKeyEvent&>(*copy).key(), copy->isAccepted());
        }
    }

    // ---------------------------------------------------------------- Vulkan

#if defined(FRUITY_HAS_VULKAN)
    bool UiHost::InitialiseVulkan()
    {
        namespace Rhi = ::MphRead::NativeRuntime::Rhi;
        const Rhi::Vulkan::InteropDevice device = Rhi::Vulkan::DescribeDevice(Rhi::SceneDevice());
        // Qt Quick on the RHI's own instance, device and graphics queue: one
        // device, one queue, one thread, so nothing needs sharing across
        // queues, and its offscreen frames have finished when endFrame returns.
        _vulkanInstance = std::make_unique<QVulkanInstance>();
        _vulkanInstance->setVkInstance(reinterpret_cast<VkInstance>(device.Instance));
        if (!_vulkanInstance->create())
        {
            std::cout << "[ui] Qt could not adopt the renderer's Vulkan instance\n";
            return false;
        }
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);
        _control = std::make_unique<RenderControl>(_gameWindow);
        _window = std::make_unique<QQuickWindow>(_control.get());
        _window->setColor(::Qt::transparent);
        _window->setVulkanInstance(_vulkanInstance.get());
        _window->setGraphicsDevice(QQuickGraphicsDevice::fromDeviceObjects(
            reinterpret_cast<VkPhysicalDevice>(device.PhysicalDevice), reinterpret_cast<VkDevice>(device.Device),
            static_cast<int>(device.QueueFamily)));
        if (!_control->initialize())
        {
            std::cout << "[ui] Qt Quick could not start on the renderer's Vulkan device\n";
            return false;
        }
        std::cout << "[ui] Qt Quick menus on the renderer's Vulkan device\n";
        return true;
    }

    void UiHost::EnsureVulkanTarget(QSize pixels)
    {
        namespace Rhi = ::MphRead::NativeRuntime::Rhi;
        if (_rhiTexture != nullptr && pixels == _targetSize)
        {
            return;
        }
        ReleaseTarget();
        auto& gpu = Rhi::SceneDevice();
        _rhiTexture = gpu.CreateTexture(Rhi::TextureDesc{static_cast<std::uint32_t>(pixels.width()),
            static_cast<std::uint32_t>(pixels.height()), 1, 1, 1, 1, Rhi::TextureFormat::RGBA8Unorm,
            Rhi::TextureUsage::Sampled | Rhi::TextureUsage::ColorAttachment
                | Rhi::TextureUsage::TransferSrc | Rhi::TextureUsage::TransferDst});
        const Rhi::Vulkan::InteropImage image
            = Rhi::Vulkan::PrepareForExternal(gpu, *_rhiTexture, Rhi::ResourceState::ColorAttachment);
        _window->setRenderTarget(QQuickRenderTarget::fromVulkanImage(reinterpret_cast<VkImage>(image.Image),
            static_cast<VkImageLayout>(image.Layout), static_cast<VkFormat>(image.Format), pixels));
        _targetSize = pixels;
        Resized(pixels);
    }

    void UiHost::RenderVulkan()
    {
        namespace Rhi = ::MphRead::NativeRuntime::Rhi;
        auto& gpu = Rhi::SceneDevice();
        // Back to a colour attachment (the composite left it shader-read),
        // with every earlier RHI submission ahead of Qt's.
        (void)Rhi::Vulkan::PrepareForExternal(gpu, *_rhiTexture, Rhi::ResourceState::ColorAttachment);
        Rhi::Vulkan::BeginExternalSubmit(gpu);
        _control->polishItems();
        _control->beginFrame();
        _control->sync();
        _control->render();
        _control->endFrame();
        // Qt's colour pass leaves its target a colour attachment.
        Rhi::Vulkan::AdoptExternalState(*_rhiTexture, Rhi::ResourceState::ColorAttachment);
        ::MphRead::Mods::Render::UiOverlay::UseTexture(*_rhiTexture, _targetSize.width(), _targetSize.height());
    }
#else
    bool UiHost::InitialiseVulkan()
    {
        std::cout << "[ui] this build has no Vulkan backend\n";
        return false;
    }

    void UiHost::EnsureVulkanTarget(QSize pixels)
    {
        (void)pixels;
    }

    void UiHost::RenderVulkan()
    {
    }
#endif
}
