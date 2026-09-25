#pragma once

#include "render/SceneRenderer.h"

#include <QObject>
#include <QSize>
#include <QUrl>

#include <memory>

class QEvent;
class QQmlEngine;
class QQmlComponent;
class QQuickItem;
class QQuickRenderControl;
class QQuickWindow;

namespace fp {

// The screens (QML), drawn inside the game window over whatever is behind
// them -- a room, a match, nothing -- the way the C#'s launcher is drawn
// inside its game window. Qt Quick renders offscreen through a render
// control, on this window's own Vulkan device and queue, into the image the
// SceneRenderer composites last.
class UiOverlay final : public QObject, public UiLayer {
    Q_OBJECT
public:
    explicit UiOverlay(VulkanWindow* window);
    ~UiOverlay() override;

    // Qt Quick on the window's device. False (with the reason logged) when it cannot be.
    bool initialize();
    bool initialized() const { return m_initialized; }
    // Before load(): the objects QML sees by name.
    QQmlEngine* engine();
    bool load(const QUrl& url);
    // The module's type (qt_add_qml_module), e.g. ("FruityPrimeUi", "Main").
    bool loadFromModule(const QString& uri, const QString& type);
    QQuickItem* root() const { return m_root; }

    // Input the screens take: pointer, wheel and key events, in window coordinates.
    bool sendEvent(QEvent* event);
    // Qt Quick lets go of the device (the window is about to destroy it).
    void shutdown();

    // UiLayer
    bool renderUi(VkImage image, const QSize& size) override;
    void releaseUi() override;

private:
    VulkanWindow* m_window;
    std::unique_ptr<QQuickRenderControl> m_control;
    std::unique_ptr<QQuickWindow> m_quickWindow;
    std::unique_ptr<QQmlEngine> m_engine;
    std::unique_ptr<QQmlComponent> m_component;
    QQuickItem* m_root = nullptr;
    VkImage m_image = VK_NULL_HANDLE;
    QSize m_size;
    bool m_initialized = false, m_dirty = true, m_rendered = false;
};

} // namespace fp
