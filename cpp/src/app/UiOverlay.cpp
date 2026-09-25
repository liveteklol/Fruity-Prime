#include "UiOverlay.h"

#include <QCoreApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickGraphicsDevice>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>

namespace fp {

UiOverlay::UiOverlay(VulkanWindow* window)
    : m_window(window)
{
}

UiOverlay::~UiOverlay() { shutdown(); }

QQmlEngine* UiOverlay::engine()
{
    if (!m_engine) {
        m_engine = std::make_unique<QQmlEngine>();
        if (!m_engine->incubationController() && m_quickWindow) {
            m_engine->setIncubationController(m_quickWindow->incubationController());
        }
    }
    return m_engine.get();
}

bool UiOverlay::initialize()
{
    if (m_initialized) {
        return true;
    }
    if (m_window->device() == VK_NULL_HANDLE) {
        return false;
    }
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);
    m_control = std::make_unique<QQuickRenderControl>();
    m_quickWindow = std::make_unique<QQuickWindow>(m_control.get());
    m_quickWindow->setVulkanInstance(m_window->vulkanInstance());
    m_quickWindow->setGraphicsDevice(QQuickGraphicsDevice::fromDeviceObjects(m_window->physicalDevice(), m_window->device(),
        static_cast<int>(m_window->graphicsQueueFamily()), 0));
    m_quickWindow->setColor(Qt::transparent);
    if (!m_control->initialize()) {
        qWarning("[ui] Qt Quick could not start on this window's Vulkan device");
        m_quickWindow.reset();
        m_control.reset();
        return false;
    }
    // Anything that changes -- a hover, an animation, a new line -- asks for another pass.
    connect(m_control.get(), &QQuickRenderControl::sceneChanged, this, [this] { m_dirty = true; });
    connect(m_control.get(), &QQuickRenderControl::renderRequested, this, [this] { m_dirty = true; });
    if (m_engine && !m_engine->incubationController()) {
        m_engine->setIncubationController(m_quickWindow->incubationController());
    }
    m_initialized = true;
    return true;
}

bool UiOverlay::loadFromModule(const QString& uri, const QString& type)
{
    if (!m_initialized) {
        return false;
    }
    m_component = std::make_unique<QQmlComponent>(engine());
    m_component->loadFromModule(uri, type);
    return load(QUrl());
}

bool UiOverlay::load(const QUrl& url)
{
    if (!m_initialized) {
        return false;
    }
    if (!url.isEmpty()) {
        m_component = std::make_unique<QQmlComponent>(engine(), url);
    }
    if (m_component->isError()) {
        for (const QQmlError& error : m_component->errors()) {
            qWarning("[ui] %s", qPrintable(error.toString()));
        }
        return false;
    }
    QObject* object = m_component->create();
    if (m_component->isError()) {
        for (const QQmlError& error : m_component->errors()) {
            qWarning("[ui] %s", qPrintable(error.toString()));
        }
    }
    m_root = qobject_cast<QQuickItem*>(object);
    if (m_root == nullptr) {
        qWarning("[ui] %s: the root is not an Item", qPrintable(m_component->url().toString()));
        delete object;
        return false;
    }
    m_root->setParentItem(m_quickWindow->contentItem());
    m_root->setParent(m_quickWindow.get());
    if (m_size.isValid()) {
        m_root->setSize(m_size);
    }
    m_dirty = true;
    return true;
}

bool UiOverlay::sendEvent(QEvent* event)
{
    if (!m_initialized || m_root == nullptr) {
        return false;
    }
    QCoreApplication::sendEvent(m_quickWindow.get(), event);
    return event->isAccepted();
}

bool UiOverlay::renderUi(VkImage image, const QSize& size)
{
    if (!m_initialized || m_root == nullptr) {
        return false;
    }
    if (image != m_image || size != m_size) {
        // A new image (the window was resized): the target, and the scene's size, follow.
        m_image = image;
        m_size = size;
        m_quickWindow->setGeometry(0, 0, size.width(), size.height());
        m_quickWindow->contentItem()->setSize(size);
        m_root->setSize(size);
        m_quickWindow->setRenderTarget(QQuickRenderTarget::fromVulkanImage(image, VK_IMAGE_LAYOUT_UNDEFINED, size));
        m_dirty = true;
        m_rendered = false;
    }
    if (!m_root->isVisible()) {
        return false;
    }
    if (m_dirty) {
        m_dirty = false;
        m_control->polishItems();
        m_control->beginFrame();
        m_control->sync();
        m_control->render();
        m_control->endFrame(); // submitted on the shared queue, before this frame's own commands
        m_rendered = true;
    }
    return m_rendered;
}

void UiOverlay::releaseUi()
{
    if (m_quickWindow && m_image != VK_NULL_HANDLE) {
        m_quickWindow->setRenderTarget(QQuickRenderTarget());
    }
    m_image = VK_NULL_HANDLE;
    m_size = {};
    m_rendered = false;
}

void UiOverlay::shutdown()
{
    if (!m_control) {
        return;
    }
    releaseUi();
    delete m_root;
    m_root = nullptr;
    m_component.reset();
    m_quickWindow.reset();
    m_engine.reset();
    m_control.reset();
    m_initialized = false;
}

} // namespace fp
