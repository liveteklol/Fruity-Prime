#include "VulkanWindow.h"

#include <QExposeEvent>
#include <QScreen>
#include <QPlatformSurfaceEvent>
#include <QVulkanDeviceFunctions>
#include <QVulkanFunctions>

#include <algorithm>
#include <cstring>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
#endif

namespace fp {

namespace {

int deviceTypeRank(VkPhysicalDeviceType type)
{
    switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        return 0;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        return 1;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        return 2;
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
        return 3;
    default:
        return 4;
    }
}

// Sleeps to within a fraction of a millisecond of `deadline` without burning
// a core: a high-resolution waitable timer on Windows (the default sleep there
// rounds to 15.6 ms), then a short yield loop for the remainder.
void sleepUntil(std::chrono::steady_clock::time_point deadline)
{
    using namespace std::chrono;
    constexpr auto spinWindow = microseconds(500);
    auto now = steady_clock::now();
    if (deadline - now > spinWindow) {
#ifdef _WIN32
        static HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        if (timer != nullptr) {
            LARGE_INTEGER due;
            due.QuadPart = -static_cast<LONGLONG>(duration_cast<nanoseconds>(deadline - now - spinWindow).count() / 100);
            if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
                WaitForSingleObject(timer, INFINITE);
            }
        } else {
            std::this_thread::sleep_until(deadline - spinWindow);
        }
#else
        std::this_thread::sleep_until(deadline - spinWindow);
#endif
    }
    while (steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
}

} // namespace

VulkanWindow::VulkanWindow()
{
    setSurfaceType(QSurface::VulkanSurface);
    m_frameTimer.setSingleShot(true);
    m_frameTimer.setInterval(0);
    m_frameTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_frameTimer, &QTimer::timeout, this, &VulkanWindow::renderFrame);
}

VulkanWindow::~VulkanWindow() { releaseAll(); }

QMatrix4x4 VulkanWindow::clipCorrectionMatrix() const
{
    // Y down, depth 0..1: the same correction QVulkanWindow applies.
    return QMatrix4x4(1.0f, 0.0f, 0.0f, 0.0f, //
        0.0f, -1.0f, 0.0f, 0.0f, //
        0.0f, 0.0f, 0.5f, 0.5f, //
        0.0f, 0.0f, 0.0f, 1.0f);
}

const char* VulkanWindow::presentModeName() const
{
    if (m_offscreen) {
        return "none (offscreen)";
    }
    switch (m_presentMode) {
    case VK_PRESENT_MODE_IMMEDIATE_KHR:
        return "immediate";
    case VK_PRESENT_MODE_MAILBOX_KHR:
        return "mailbox";
    case VK_PRESENT_MODE_FIFO_RELAXED_KHR:
        return "fifo-relaxed";
    default:
        return "fifo";
    }
}

uint32_t VulkanWindow::findMemoryType(uint32_t bits, VkMemoryPropertyFlags flags) const
{
    VkPhysicalDeviceMemoryProperties props;
    m_f->vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; i++) {
        if ((bits & (1u << i)) && (props.memoryTypes[i].propertyFlags & flags) == flags) {
            return i;
        }
    }
    for (uint32_t i = 0; i < props.memoryTypeCount; i++) {
        if (bits & (1u << i)) {
            return i;
        }
    }
    return 0;
}

bool VulkanWindow::initDevice()
{
    QVulkanInstance* inst = vulkanInstance();
    if (inst == nullptr) {
        qCritical("VulkanWindow: no QVulkanInstance set");
        return false;
    }
    m_f = inst->functions();
    m_surface = m_offscreen ? VK_NULL_HANDLE : QVulkanInstance::surfaceForWindow(this);
    if (m_surface == VK_NULL_HANDLE && !m_offscreen) {
        qCritical("VulkanWindow: no Vulkan surface for this window");
        return false;
    }
    auto instProc = [&](const char* name) { return inst->getInstanceProcAddr(name); };
    m_getSurfaceSupport = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceSupportKHR>(instProc("vkGetPhysicalDeviceSurfaceSupportKHR"));
    m_getSurfaceCaps
        = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(instProc("vkGetPhysicalDeviceSurfaceCapabilitiesKHR"));
    m_getSurfaceFormats = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceFormatsKHR>(instProc("vkGetPhysicalDeviceSurfaceFormatsKHR"));
    m_getPresentModes
        = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfacePresentModesKHR>(instProc("vkGetPhysicalDeviceSurfacePresentModesKHR"));

    uint32_t count = 0;
    m_f->vkEnumeratePhysicalDevices(inst->vkInstance(), &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    m_f->vkEnumeratePhysicalDevices(inst->vkInstance(), &count, devices.data());

    struct Candidate {
        VkPhysicalDevice device;
        uint32_t family;
        VkPhysicalDeviceProperties props;
    };
    std::vector<Candidate> candidates;
    for (VkPhysicalDevice pd : devices) {
        uint32_t familyCount = 0;
        m_f->vkGetPhysicalDeviceQueueFamilyProperties(pd, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        m_f->vkGetPhysicalDeviceQueueFamilyProperties(pd, &familyCount, families.data());
        for (uint32_t i = 0; i < familyCount; i++) {
            VkBool32 present = VK_TRUE;
            if (!m_offscreen) {
                m_getSurfaceSupport(pd, i, m_surface, &present);
            }
            if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
                VkPhysicalDeviceProperties props;
                m_f->vkGetPhysicalDeviceProperties(pd, &props);
                candidates.push_back({pd, i, props});
                break;
            }
        }
    }
    if (candidates.empty()) {
        qCritical("VulkanWindow: no device can draw to this window");
        return false;
    }
    std::stable_sort(candidates.begin(), candidates.end(),
        [](const Candidate& a, const Candidate& b) { return deviceTypeRank(a.props.deviceType) < deviceTypeRank(b.props.deviceType); });
    size_t chosen = 0;
    bool ok = false;
    const int forced = qEnvironmentVariableIntValue("FP_GPU", &ok);
    if (ok && forced >= 0 && static_cast<size_t>(forced) < candidates.size()) {
        chosen = static_cast<size_t>(forced);
    }
    m_physicalDevice = candidates[chosen].device;
    m_queueFamily = candidates[chosen].family;
    m_deviceName = QString::fromUtf8(candidates[chosen].props.deviceName);

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = m_queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;
    const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = 1;
    deviceInfo.ppEnabledExtensionNames = extensions;
    if (m_f->vkCreateDevice(m_physicalDevice, &deviceInfo, nullptr, &m_device) != VK_SUCCESS) {
        qCritical("VulkanWindow: vkCreateDevice failed");
        return false;
    }
    m_df = inst->deviceFunctions(m_device);
    m_df->vkGetDeviceQueue(m_device, m_queueFamily, 0, &m_queue);

    auto devProc = [&](const char* name) { return m_f->vkGetDeviceProcAddr(m_device, name); };
    m_createSwapchain = reinterpret_cast<PFN_vkCreateSwapchainKHR>(devProc("vkCreateSwapchainKHR"));
    m_destroySwapchain = reinterpret_cast<PFN_vkDestroySwapchainKHR>(devProc("vkDestroySwapchainKHR"));
    m_getSwapchainImages = reinterpret_cast<PFN_vkGetSwapchainImagesKHR>(devProc("vkGetSwapchainImagesKHR"));
    m_acquireNextImage = reinterpret_cast<PFN_vkAcquireNextImageKHR>(devProc("vkAcquireNextImageKHR"));
    m_queuePresent = reinterpret_cast<PFN_vkQueuePresentKHR>(devProc("vkQueuePresentKHR"));

    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_queueFamily;
    m_df->vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool);

    VkPhysicalDeviceMemoryProperties memProps;
    m_f->vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProps);
    const uint32_t allTypes = (1u << memProps.memoryTypeCount) - 1;
    m_hostVisibleMemoryIndex
        = findMemoryType(allTypes, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    m_deviceLocalMemoryIndex = findMemoryType(allTypes, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    for (VkFormat candidate : {VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D32_SFLOAT}) {
        VkFormatProperties fp;
        m_f->vkGetPhysicalDeviceFormatProperties(m_physicalDevice, candidate, &fp);
        if (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            m_depthFormat = candidate;
            break;
        }
    }

    for (Frame& frame : m_frames) {
        VkCommandBufferAllocateInfo cbInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        cbInfo.commandPool = m_commandPool;
        cbInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbInfo.commandBufferCount = 1;
        m_df->vkAllocateCommandBuffers(m_device, &cbInfo, &frame.commandBuffer);
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        m_df->vkCreateFence(m_device, &fenceInfo, nullptr, &frame.fence);
        VkSemaphoreCreateInfo semInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        m_df->vkCreateSemaphore(m_device, &semInfo, nullptr, &frame.imageAvailable);
    }
    return true;
}

bool VulkanWindow::createSwapchain()
{
    VkSurfaceCapabilitiesKHR caps;
    m_getSurfaceCaps(m_physicalDevice, m_surface, &caps);
    QSize size = this->size() * devicePixelRatio();
    if (caps.currentExtent.width != 0xFFFFFFFFu) {
        size = QSize(static_cast<int>(caps.currentExtent.width), static_cast<int>(caps.currentExtent.height));
    }
    size = size.boundedTo(QSize(static_cast<int>(caps.maxImageExtent.width), static_cast<int>(caps.maxImageExtent.height)))
               .expandedTo(QSize(static_cast<int>(caps.minImageExtent.width), static_cast<int>(caps.minImageExtent.height)));
    if (size.isEmpty()) {
        return false;
    }

    uint32_t formatCount = 0;
    m_getSurfaceFormats(m_physicalDevice, m_surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    m_getSurfaceFormats(m_physicalDevice, m_surface, &formatCount, formats.data());
    VkSurfaceFormatKHR format = formats.empty() ? VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}
                                                : formats[0];
    for (VkFormat wanted : {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM}) {
        auto it = std::find_if(formats.begin(), formats.end(), [&](const VkSurfaceFormatKHR& f) {
            return f.format == wanted && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        });
        if (it != formats.end()) {
            format = *it;
            break;
        }
    }
    if (m_renderPass != VK_NULL_HANDLE && format.format != m_colorFormat) {
        m_df->vkDestroyRenderPass(m_device, m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }
    m_colorFormat = format.format;

    uint32_t modeCount = 0;
    m_getPresentModes(m_physicalDevice, m_surface, &modeCount, nullptr);
    std::vector<VkPresentModeKHR> modes(modeCount);
    m_getPresentModes(m_physicalDevice, m_surface, &modeCount, modes.data());
    m_presentMode = VK_PRESENT_MODE_FIFO_KHR;
    if (!m_vsync) {
        for (VkPresentModeKHR wanted : {VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR}) {
            if (std::find(modes.begin(), modes.end(), wanted) != modes.end()) {
                m_presentMode = wanted;
                break;
            }
        }
    }

    uint32_t imageCount = std::max<uint32_t>(caps.minImageCount + 1, 3);
    if (caps.maxImageCount > 0) {
        imageCount = std::min(imageCount, caps.maxImageCount);
    }
    m_canGrab = caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(caps.supportedCompositeAlpha & alpha)) {
        for (VkCompositeAlphaFlagBitsKHR a : {VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                 VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR}) {
            if (caps.supportedCompositeAlpha & a) {
                alpha = a;
                break;
            }
        }
    }

    VkSwapchainKHR old = m_swapchain;
    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = m_surface;
    info.minImageCount = imageCount;
    info.imageFormat = format.format;
    info.imageColorSpace = format.colorSpace;
    info.imageExtent = {static_cast<uint32_t>(size.width()), static_cast<uint32_t>(size.height())};
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | (m_canGrab ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR
                                                                                            : caps.currentTransform;
    info.compositeAlpha = alpha;
    info.presentMode = m_presentMode;
    info.clipped = VK_TRUE;
    info.oldSwapchain = old;
    if (m_createSwapchain(m_device, &info, nullptr, &m_swapchain) != VK_SUCCESS) {
        m_swapchain = old;
        qCritical("VulkanWindow: vkCreateSwapchainKHR failed");
        return false;
    }
    if (old != VK_NULL_HANDLE) {
        m_destroySwapchain(m_device, old, nullptr);
    }
    m_swapchainSize = size;
    m_displayHz = screen() != nullptr ? screen()->refreshRate() : 0;
    uint32_t count = 0;
    m_getSwapchainImages(m_device, m_swapchain, &count, nullptr);
    m_images.resize(count);
    m_getSwapchainImages(m_device, m_swapchain, &count, m_images.data());
    return createRenderPassAndFramebuffers(info.imageExtent.width, info.imageExtent.height);
}

bool VulkanWindow::createRenderPassAndFramebuffers(uint32_t width, uint32_t height)
{
    if (m_renderPass == VK_NULL_HANDLE) {
        VkAttachmentDescription attachments[2]{};
        attachments[0].format = m_colorFormat;
        attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout = m_finalLayout;
        attachments[1].format = m_depthFormat;
        attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;
        subpass.pDepthStencilAttachment = &depthRef;
        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo rpInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        rpInfo.attachmentCount = 2;
        rpInfo.pAttachments = attachments;
        rpInfo.subpassCount = 1;
        rpInfo.pSubpasses = &subpass;
        rpInfo.dependencyCount = 1;
        rpInfo.pDependencies = &dependency;
        m_df->vkCreateRenderPass(m_device, &rpInfo, nullptr, &m_renderPass);
    }

    VkImageCreateInfo depthInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    depthInfo.imageType = VK_IMAGE_TYPE_2D;
    depthInfo.format = m_depthFormat;
    depthInfo.extent = {width, height, 1};
    depthInfo.mipLevels = 1;
    depthInfo.arrayLayers = 1;
    depthInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    depthInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    depthInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    m_df->vkCreateImage(m_device, &depthInfo, nullptr, &m_depthImage);
    VkMemoryRequirements req;
    m_df->vkGetImageMemoryRequirements(m_device, m_depthImage, &req);
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    m_df->vkAllocateMemory(m_device, &alloc, nullptr, &m_depthMemory);
    m_df->vkBindImageMemory(m_device, m_depthImage, m_depthMemory, 0);
    VkImageViewCreateInfo depthViewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    depthViewInfo.image = m_depthImage;
    depthViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    depthViewInfo.format = m_depthFormat;
    depthViewInfo.subresourceRange
        = {static_cast<VkImageAspectFlags>(VK_IMAGE_ASPECT_DEPTH_BIT
               | (m_depthFormat == VK_FORMAT_D32_SFLOAT ? 0 : VK_IMAGE_ASPECT_STENCIL_BIT)),
            0, 1, 0, 1};
    m_df->vkCreateImageView(m_device, &depthViewInfo, nullptr, &m_depthView);

    const uint32_t count = static_cast<uint32_t>(m_images.size());
    m_imageViews.resize(count);
    m_framebuffers.resize(count);
    m_renderFinished.resize(count);
    for (uint32_t i = 0; i < count; i++) {
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = m_images[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_colorFormat;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        m_df->vkCreateImageView(m_device, &viewInfo, nullptr, &m_imageViews[i]);
        VkImageView views[] = {m_imageViews[i], m_depthView};
        VkFramebufferCreateInfo fbInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fbInfo.renderPass = m_renderPass;
        fbInfo.attachmentCount = 2;
        fbInfo.pAttachments = views;
        fbInfo.width = width;
        fbInfo.height = height;
        fbInfo.layers = 1;
        m_df->vkCreateFramebuffer(m_device, &fbInfo, nullptr, &m_framebuffers[i]);
        VkSemaphoreCreateInfo semInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        m_df->vkCreateSemaphore(m_device, &semInfo, nullptr, &m_renderFinished[i]);
    }
    return true;
}

bool VulkanWindow::createOffscreenTargets()
{
    m_colorFormat = VK_FORMAT_B8G8R8A8_UNORM;
    m_finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    m_canGrab = true;
    m_swapchainSize = m_offscreenSize;
    m_images.assign(3, VK_NULL_HANDLE);
    m_offscreenMemory.assign(3, VK_NULL_HANDLE);
    for (size_t i = 0; i < m_images.size(); i++) {
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = m_colorFormat;
        info.extent = {static_cast<uint32_t>(m_offscreenSize.width()), static_cast<uint32_t>(m_offscreenSize.height()), 1};
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        m_df->vkCreateImage(m_device, &info, nullptr, &m_images[i]);
        VkMemoryRequirements req;
        m_df->vkGetImageMemoryRequirements(m_device, m_images[i], &req);
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex = findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        m_df->vkAllocateMemory(m_device, &alloc, nullptr, &m_offscreenMemory[i]);
        m_df->vkBindImageMemory(m_device, m_images[i], m_offscreenMemory[i], 0);
    }
    return createRenderPassAndFramebuffers(
        static_cast<uint32_t>(m_offscreenSize.width()), static_cast<uint32_t>(m_offscreenSize.height()));
}

bool VulkanWindow::startOffscreen(QSize size)
{
    m_offscreen = true;
    m_offscreenSize = size;
    if (!initDevice() || !createOffscreenTargets()) {
        return false;
    }
    m_renderer = createRenderer();
    m_renderer->initResources();
    m_renderer->initSwapChainResources();
    requestUpdate();
    return true;
}

void VulkanWindow::releaseSwapchain()
{
    if (m_device == VK_NULL_HANDLE) {
        return;
    }
    for (VkFramebuffer fb : m_framebuffers) {
        m_df->vkDestroyFramebuffer(m_device, fb, nullptr);
    }
    for (VkImageView view : m_imageViews) {
        m_df->vkDestroyImageView(m_device, view, nullptr);
    }
    for (VkSemaphore sem : m_renderFinished) {
        m_df->vkDestroySemaphore(m_device, sem, nullptr);
    }
    m_framebuffers.clear();
    m_imageViews.clear();
    m_renderFinished.clear();
    if (m_offscreen) {
        for (size_t i = 0; i < m_images.size(); i++) {
            m_df->vkDestroyImage(m_device, m_images[i], nullptr);
            m_df->vkFreeMemory(m_device, m_offscreenMemory[i], nullptr);
        }
        m_offscreenMemory.clear();
    }
    m_images.clear();
    if (m_depthView) {
        m_df->vkDestroyImageView(m_device, m_depthView, nullptr);
    }
    if (m_depthImage) {
        m_df->vkDestroyImage(m_device, m_depthImage, nullptr);
    }
    if (m_depthMemory) {
        m_df->vkFreeMemory(m_device, m_depthMemory, nullptr);
    }
    m_depthView = VK_NULL_HANDLE;
    m_depthImage = VK_NULL_HANDLE;
    m_depthMemory = VK_NULL_HANDLE;
}

void VulkanWindow::releaseAll()
{
    m_frameTimer.stop();
    if (m_device == VK_NULL_HANDLE) {
        return;
    }
    m_df->vkDeviceWaitIdle(m_device);
    if (m_renderer) {
        m_renderer->releaseSwapChainResources();
        m_renderer->releaseResources();
        delete m_renderer;
        m_renderer = nullptr;
    }
    releaseSwapchain();
    if (m_swapchain) {
        m_destroySwapchain(m_device, m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }
    if (m_renderPass) {
        m_df->vkDestroyRenderPass(m_device, m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }
    for (Frame& frame : m_frames) {
        m_df->vkDestroyFence(m_device, frame.fence, nullptr);
        m_df->vkDestroySemaphore(m_device, frame.imageAvailable, nullptr);
        frame = {};
    }
    m_df->vkDestroyCommandPool(m_device, m_commandPool, nullptr);
    m_commandPool = VK_NULL_HANDLE;
    deviceAboutToBeDestroyed();
    m_df->vkDestroyDevice(m_device, nullptr);
    vulkanInstance()->resetDeviceFunctions(m_device);
    m_device = VK_NULL_HANDLE;
    m_surface = VK_NULL_HANDLE;
}

void VulkanWindow::exposeEvent(QExposeEvent*)
{
    if (!isExposed()) {
        return;
    }
    if (m_device == VK_NULL_HANDLE) {
        if (!initDevice() || !createSwapchain()) {
            return;
        }
        m_renderer = createRenderer();
        m_renderer->initResources();
        m_renderer->initSwapChainResources();
    }
    requestUpdate();
}

bool VulkanWindow::event(QEvent* e)
{
    if (e->type() == QEvent::PlatformSurface
        && static_cast<QPlatformSurfaceEvent*>(e)->surfaceEventType() == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed) {
        releaseAll();
    } else if (e->type() == QEvent::UpdateRequest) {
        return true; // frames come from m_frameTimer, not from the platform
    }
    return QWindow::event(e);
}

void VulkanWindow::requestUpdate()
{
    if (m_device != VK_NULL_HANDLE && (m_offscreen || isExposed()) && !m_frameTimer.isActive() && !m_inFrame) {
        m_frameTimer.start();
    }
}

void VulkanWindow::renderFrame()
{
    if (m_device == VK_NULL_HANDLE || (!m_offscreen && !isExposed()) || m_inFrame) {
        return;
    }
    // With FIFO, some drivers busy-wait for the vblank inside acquire or
    // present, burning a core to draw 60 frames. Pacing to the display rate
    // ourselves means the driver only ever waits out the remainder.
    double pacedHz = m_frameCap;
    if (pacedHz <= 0 && !m_offscreen && m_presentMode == VK_PRESENT_MODE_FIFO_KHR && m_displayHz > 0) {
        pacedHz = m_displayHz;
    }
    if (pacedHz > 0) {
        const auto now = std::chrono::steady_clock::now();
        const auto period = std::chrono::nanoseconds(static_cast<long long>(1e9 / pacedHz));
        if (m_nextFrameAt > now) {
            sleepUntil(m_nextFrameAt);
        }
        // A frame that ran late restarts the schedule rather than rushing to catch up.
        m_nextFrameAt = (m_nextFrameAt + period < now) ? now + period : std::max(m_nextFrameAt, now - period) + period;
    }

    const QSize wanted = size() * devicePixelRatio();
    if (!m_offscreen && (wanted != m_swapchainSize || m_swapchain == VK_NULL_HANDLE)) {
        m_df->vkDeviceWaitIdle(m_device);
        m_renderer->releaseSwapChainResources();
        releaseSwapchain();
        if (!createSwapchain()) {
            return;
        }
        m_renderer->initSwapChainResources();
    }

    Frame& frame = m_frames[m_frame];
    m_df->vkWaitForFences(m_device, 1, &frame.fence, VK_TRUE, UINT64_MAX);
    VkResult acquired = VK_SUCCESS;
    if (m_offscreen) {
        m_imageIndex = (m_imageIndex + 1) % static_cast<uint32_t>(m_images.size());
    } else {
        acquired = m_acquireNextImage(m_device, m_swapchain, UINT64_MAX, frame.imageAvailable, VK_NULL_HANDLE, &m_imageIndex);
    }
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
        m_swapchainSize = QSize();
        requestUpdate();
        return;
    }
    if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) {
        qCritical("VulkanWindow: vkAcquireNextImageKHR failed: %d", acquired);
        return;
    }
    m_df->vkResetFences(m_device, 1, &frame.fence);
    m_df->vkResetCommandBuffer(frame.commandBuffer, 0);
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    m_df->vkBeginCommandBuffer(frame.commandBuffer, &begin);
    m_inFrame = true;
    m_renderer->startNextFrame();
}

void VulkanWindow::frameReady()
{
    Frame& frame = m_frames[m_frame];
    VkBuffer grabBuffer = VK_NULL_HANDLE;
    VkDeviceMemory grabMemory = VK_NULL_HANDLE;
    const bool grabbing = m_grabRequested && m_canGrab;
    if (grabbing) {
        const VkDeviceSize bytes = static_cast<VkDeviceSize>(m_swapchainSize.width()) * m_swapchainSize.height() * 4;
        VkBufferCreateInfo bufInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bufInfo.size = bytes;
        bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        m_df->vkCreateBuffer(m_device, &bufInfo, nullptr, &grabBuffer);
        VkMemoryRequirements req;
        m_df->vkGetBufferMemoryRequirements(m_device, grabBuffer, &req);
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex = findMemoryType(req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        m_df->vkAllocateMemory(m_device, &alloc, nullptr, &grabMemory);
        m_df->vkBindBufferMemory(m_device, grabBuffer, grabMemory, 0);

        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = m_images[m_imageIndex];
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        barrier.oldLayout = m_finalLayout;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        m_df->vkCmdPipelineBarrier(frame.commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {static_cast<uint32_t>(m_swapchainSize.width()), static_cast<uint32_t>(m_swapchainSize.height()), 1};
        m_df->vkCmdCopyImageToBuffer(frame.commandBuffer, m_images[m_imageIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            grabBuffer, 1, &copy);
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = m_finalLayout;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = 0;
        m_df->vkCmdPipelineBarrier(frame.commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }
    m_df->vkEndCommandBuffer(frame.commandBuffer);

    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.waitSemaphoreCount = m_offscreen ? 0 : 1;
    submit.pWaitSemaphores = &frame.imageAvailable;
    submit.pWaitDstStageMask = &waitStage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &frame.commandBuffer;
    submit.signalSemaphoreCount = m_offscreen ? 0 : 1;
    submit.pSignalSemaphores = &m_renderFinished[m_imageIndex];
    m_df->vkQueueSubmit(m_queue, 1, &submit, frame.fence);

    VkResult presented = VK_SUCCESS;
    if (!m_offscreen) {
    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &m_renderFinished[m_imageIndex];
    present.swapchainCount = 1;
    present.pSwapchains = &m_swapchain;
    present.pImageIndices = &m_imageIndex;
    vulkanInstance()->presentAboutToBeQueued(this);
    presented = m_queuePresent(m_queue, &present);
    vulkanInstance()->presentQueued(this);
    }
    if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) {
        m_swapchainSize = QSize();
    }

    if (grabbing) {
        m_df->vkWaitForFences(m_device, 1, &frame.fence, VK_TRUE, UINT64_MAX);
        void* data = nullptr;
        const VkDeviceSize bytes = static_cast<VkDeviceSize>(m_swapchainSize.width() > 0 ? m_swapchainSize.width() : 0)
            * m_swapchainSize.height() * 4;
        if (bytes > 0 && m_df->vkMapMemory(m_device, grabMemory, 0, bytes, 0, &data) == VK_SUCCESS) {
            const QImage::Format fmt = m_colorFormat == VK_FORMAT_B8G8R8A8_UNORM ? QImage::Format_ARGB32 : QImage::Format_RGBA8888;
            m_grabbed = QImage(static_cast<const uchar*>(data), m_swapchainSize.width(), m_swapchainSize.height(), fmt)
                            .convertToFormat(QImage::Format_RGB32);
            m_df->vkUnmapMemory(m_device, grabMemory);
        }
        m_df->vkDestroyBuffer(m_device, grabBuffer, nullptr);
        m_df->vkFreeMemory(m_device, grabMemory, nullptr);
    }
    m_grabRequested = false;
    m_frame = (m_frame + 1) % MAX_CONCURRENT_FRAME_COUNT;
    m_inFrame = false;
}

QImage VulkanWindow::grab()
{
    if (!m_canGrab || m_device == VK_NULL_HANDLE) {
        return {};
    }
    if (m_offscreen) {
        m_df->vkDeviceWaitIdle(m_device);
    }
    m_frameTimer.stop();
    m_grabbed = QImage();
    m_grabRequested = true;
    const QSize before = m_swapchainSize;
    renderFrame();
    if (m_grabbed.isNull() && before != m_swapchainSize) {
        m_grabRequested = true;
        renderFrame();
    }
    return m_grabbed;
}

} // namespace fp
