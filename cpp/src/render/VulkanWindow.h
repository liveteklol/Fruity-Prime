#pragma once

#include <QImage>
#include <QMatrix4x4>
#include <QSize>
#include <QTimer>
#include <QVulkanInstance>
#include <QWindow>

#include <array>
#include <chrono>
#include <vector>

class QVulkanDeviceFunctions;
class QVulkanFunctions;

namespace fp {

class VulkanRenderer {
public:
    virtual ~VulkanRenderer() = default;
    virtual void initResources() {}
    virtual void initSwapChainResources() {}
    virtual void releaseSwapChainResources() {}
    virtual void releaseResources() {}
    // Record into currentCommandBuffer(), then call frameReady().
    virtual void startNextFrame() = 0;
};

// A QWindow that owns its Vulkan device and swapchain. Unlike QVulkanWindow
// it drives its own frame loop, so the frame rate is set by the present mode
// and the frame cap rather than by the platform's frame callbacks.
class VulkanWindow : public QWindow {
    Q_OBJECT
public:
    static constexpr int MAX_CONCURRENT_FRAME_COUNT = 2;

    VulkanWindow();
    ~VulkanWindow() override;

    virtual VulkanRenderer* createRenderer() = 0;
    // The device is about to be destroyed: whatever else holds it lets go.
    virtual void deviceAboutToBeDestroyed() {}

    // true: FIFO. false: MAILBOX, else IMMEDIATE, else FIFO.
    void setVSync(bool on) { m_vsync = on; }
    // Frames per second, 0 for none. Applied on top of the present mode.
    void setFrameCap(int fps) { m_frameCap = fps; }

    VkPhysicalDevice physicalDevice() const { return m_physicalDevice; }
    VkDevice device() const { return m_device; }
    VkQueue graphicsQueue() const { return m_queue; }
    uint32_t graphicsQueueFamily() const { return m_queueFamily; }
    VkCommandPool graphicsCommandPool() const { return m_commandPool; }
    uint32_t hostVisibleMemoryIndex() const { return m_hostVisibleMemoryIndex; }
    uint32_t deviceLocalMemoryIndex() const { return m_deviceLocalMemoryIndex; }
    VkRenderPass defaultRenderPass() const { return m_renderPass; }
    VkFormat colorFormat() const { return m_colorFormat; }
    VkSampleCountFlagBits sampleCountFlagBits() const { return VK_SAMPLE_COUNT_1_BIT; }
    VkCommandBuffer currentCommandBuffer() const { return m_frames[m_frame].commandBuffer; }
    VkFramebuffer currentFramebuffer() const { return m_framebuffers[m_imageIndex]; }
    int currentFrame() const { return m_frame; }
    int concurrentFrameCount() const { return MAX_CONCURRENT_FRAME_COUNT; }
    QSize swapChainImageSize() const { return m_swapchainSize; }
    QMatrix4x4 clipCorrectionMatrix() const;
    QString deviceName() const { return m_deviceName; }
    const char* presentModeName() const;

    void frameReady();
    void requestUpdate();

    // Renders one frame now and returns what it put on screen.
    QImage grab();

    // Renders to offscreen images at `size`, never shown or presented: the
    // frame rate is then bound only by the device. Call instead of show().
    bool startOffscreen(QSize size);
    bool isOffscreen() const { return m_offscreen; }

protected:
    void exposeEvent(QExposeEvent* event) override;
    bool event(QEvent* event) override;

private:
    struct Frame {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
    };

    bool initDevice();
    bool createSwapchain();
    bool createOffscreenTargets();
    bool createRenderPassAndFramebuffers(uint32_t width, uint32_t height);
    void releaseSwapchain();
    void releaseAll();
    void renderFrame();
    uint32_t findMemoryType(uint32_t bits, VkMemoryPropertyFlags flags) const;

    QVulkanFunctions* m_f = nullptr;
    QVulkanDeviceFunctions* m_df = nullptr;
    VulkanRenderer* m_renderer = nullptr;
    bool m_vsync = true;
    bool m_offscreen = false;
    QSize m_offscreenSize;
    std::vector<VkDeviceMemory> m_offscreenMemory;
    VkImageLayout m_finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    int m_frameCap = 0;
    double m_displayHz = 0;
    QTimer m_frameTimer;
    std::chrono::steady_clock::time_point m_nextFrameAt{};

    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    QString m_deviceName;
    VkDevice m_device = VK_NULL_HANDLE;
    uint32_t m_queueFamily = 0;
    VkQueue m_queue = VK_NULL_HANDLE;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    uint32_t m_hostVisibleMemoryIndex = 0;
    uint32_t m_deviceLocalMemoryIndex = 0;
    VkFormat m_depthFormat = VK_FORMAT_UNDEFINED;

    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    VkFormat m_colorFormat = VK_FORMAT_UNDEFINED;
    VkPresentModeKHR m_presentMode = VK_PRESENT_MODE_FIFO_KHR;
    QSize m_swapchainSize;
    bool m_canGrab = false;
    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    std::vector<VkImage> m_images;
    std::vector<VkImageView> m_imageViews;
    std::vector<VkFramebuffer> m_framebuffers;
    std::vector<VkSemaphore> m_renderFinished; // one per swapchain image
    VkImage m_depthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_depthMemory = VK_NULL_HANDLE;
    VkImageView m_depthView = VK_NULL_HANDLE;

    std::array<Frame, MAX_CONCURRENT_FRAME_COUNT> m_frames{};
    int m_frame = 0;
    uint32_t m_imageIndex = 0;
    bool m_inFrame = false;
    bool m_grabRequested = false;
    QImage m_grabbed;

    PFN_vkGetPhysicalDeviceSurfaceSupportKHR m_getSurfaceSupport = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR m_getSurfaceCaps = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR m_getSurfaceFormats = nullptr;
    PFN_vkGetPhysicalDeviceSurfacePresentModesKHR m_getPresentModes = nullptr;
    PFN_vkCreateSwapchainKHR m_createSwapchain = nullptr;
    PFN_vkDestroySwapchainKHR m_destroySwapchain = nullptr;
    PFN_vkGetSwapchainImagesKHR m_getSwapchainImages = nullptr;
    PFN_vkAcquireNextImageKHR m_acquireNextImage = nullptr;
    PFN_vkQueuePresentKHR m_queuePresent = nullptr;
};

} // namespace fp
