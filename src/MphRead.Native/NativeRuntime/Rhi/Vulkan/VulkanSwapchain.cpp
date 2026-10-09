#include "VulkanSwapchain.hpp"
#include "../../FrameTelemetry.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(FRUITY_HAS_VULKAN)
#include "VulkanContextInternal.hpp"
#include "VulkanPresentResult.hpp"
#include "VulkanReacquireProof.hpp"
#include "VulkanWindowSystem.hpp"
#include "../FullscreenExclusive.hpp"
namespace MphRead::NativeRuntime::Rhi::Vulkan
{
    namespace
    {
        // VK_EXT_full_screen_exclusive's structs and entry points, as
        // vulkan_win32.h declares them, without pulling <windows.h> into this
        // file: HMONITOR is a pointer, VkFullScreenExclusiveEXT an int.
        struct SurfaceFullScreenExclusiveInfo
        {
            VkStructureType sType = VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_INFO_EXT;
            void* pNext = nullptr;
            std::int32_t fullScreenExclusive = 0;
        };
        struct SurfaceFullScreenExclusiveWin32Info
        {
            VkStructureType sType = VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_WIN32_INFO_EXT;
            const void* pNext = nullptr;
            void* hmonitor = nullptr;
        };
        constexpr std::int32_t FullScreenExclusiveDisallowed = 2;
        constexpr std::int32_t FullScreenExclusiveApplicationControlled = 3;
        using FullScreenExclusiveModeFn = VkResult (VKAPI_PTR*)(VkDevice, VkSwapchainKHR);

        VkFormat ToVkFormat(TextureFormat format)
        {
            switch (format)
            {
            case TextureFormat::BGRA8Unorm: return VK_FORMAT_B8G8R8A8_UNORM;
            case TextureFormat::BGRA8Srgb: return VK_FORMAT_B8G8R8A8_SRGB;
            case TextureFormat::RGBA8Srgb: return VK_FORMAT_R8G8B8A8_SRGB;
            case TextureFormat::RGBA8Unorm:
            default: return VK_FORMAT_R8G8B8A8_UNORM;
            }
        }

        TextureFormat ToRhiFormat(VkFormat format)
        {
            switch (format)
            {
            case VK_FORMAT_B8G8R8A8_UNORM: return TextureFormat::BGRA8Unorm;
            case VK_FORMAT_B8G8R8A8_SRGB: return TextureFormat::BGRA8Srgb;
            case VK_FORMAT_R8G8B8A8_SRGB: return TextureFormat::RGBA8Srgb;
            case VK_FORMAT_R8G8B8A8_UNORM: return TextureFormat::RGBA8Unorm;
            default: throw std::runtime_error("Unsupported Vulkan swapchain pixel format.");
            }
        }

        class VulkanSwapchainTexture final : public Texture
        {
        public:
            VulkanSwapchainTexture(VkImage image, VkImageView view, VkExtent2D extent, TextureFormat format)
                : _image(image), _view(view)
            {
                _desc.width = extent.width;
                _desc.height = extent.height;
                _desc.depth = 1;
                _desc.mipLevels = 1;
                _desc.arrayLayers = 1;
                _desc.sampleCount = 1;
                _desc.format = format;
                _desc.usage = TextureUsage::ColorAttachment;
                _desc.memoryUsage = MemoryUsage::GpuOnly;
                _desc.initialState = ResourceState::Present;
            }

            [[nodiscard]] const TextureDesc& Desc() const noexcept override { return _desc; }
            [[nodiscard]] TextureHandle Handle() const noexcept override { return {}; }
            [[nodiscard]] VkImage Image() const noexcept { return _image; }
            [[nodiscard]] VkImageView View() const noexcept { return _view; }

        private:
            VkImage _image = VK_NULL_HANDLE; // Owned by VkSwapchainKHR.
            VkImageView _view = VK_NULL_HANDLE;
            TextureDesc _desc{};
        };
    }

    class VulkanSwapchain final : public Swapchain
    {
    public:
        VulkanSwapchain(::MphRead::RendererPlatform::Window& window, const SwapchainDesc& desc, bool allowMaintenance = true)
            : VulkanSwapchain(window, desc, std::make_unique<Context>(true, window, allowMaintenance), nullptr) {}
        // Presents through a context the caller owns and shares with its
        // graphics device (the game window's scene and UI device).
        VulkanSwapchain(Context& shared, ::MphRead::RendererPlatform::Window& window, const SwapchainDesc& desc)
            : VulkanSwapchain(window, desc, nullptr, &shared) {}

        VulkanSwapchain(::MphRead::RendererPlatform::Window& window, const SwapchainDesc& desc,
            std::unique_ptr<Context> owned, Context* shared)
            : _window(&window), _ownedContext(std::move(owned)), _context(shared ? *shared : *_ownedContext),
#if !defined(__ANDROID__)
              _nativeWindow(window.NativeHandle()),
#endif
              _desc(desc), _requestedMode(desc.presentMode)
        {
            Start();
        }

        // Android: the context's own surface (made on an ANativeWindow), sized
        // by the surface itself; there is no GLFW window.
        VulkanSwapchain(Context& shared, const SwapchainDesc& desc)
            : _context(shared), _desc(desc), _requestedMode(desc.presentMode)
        {
            Start();
        }

        void Start()
        {
            try
            {
                if (!_context._impl->surface)
                    throw std::runtime_error("The Vulkan context did not create a presentation surface.");
                InitializeFrames();
                auto& vk = *_context._impl;
                _reflex = std::make_unique<VulkanNvidiaReflex>(VulkanNvidiaReflex::Dispatch{vk.device, vk.nvLowLatency2,
                    vk.nvLowLatency2Revision, vk.reflexUnavailableReason, vk.vkCreateSemaphore, vk.vkDestroySemaphore, vk.vkWaitSemaphores,
                    vk.vkSetLatencySleepModeNV, vk.vkLatencySleepNV, vk.vkSetLatencyMarkerNV, vk.vkGetLatencyTimingsNV}, vk.reflexFrameSequence);
                vk.reflex = _reflex.get();
                int width = 0, height = 0;
                DrawableSize(width, height);
                if (width <= 0 || height <= 0)
                    throw std::runtime_error("The Vulkan window has no drawable framebuffer.");
                Recreate(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));
            }
            catch (...)
            {
                CleanupNoThrow();
                if (_ownedContext) _context.Shutdown();
                _closed = true;
                throw;
            }
        }

        ~VulkanSwapchain() override
        {
            if (!_closed)
            {
                try { Close(); }
                catch (const std::exception& e)
                {
                    std::cerr << "[vulkan] swapchain cleanup: " << e.what() << '\n';
                    CleanupNoThrow();
                }
            }
        }

        [[nodiscard]] const SwapchainDesc& Desc() const noexcept override { return _desc; }
        void ConfigureLowLatency(LowLatencyMode mode, std::uint32_t interval) override { _reflex->SetMode(mode, interval); }
        [[nodiscard]] LowLatencyCapabilities LowLatencyCaps() const noexcept override { return _reflex->Caps(); }
        [[nodiscard]] LowLatencyDiagnostics LowLatencyStats() const noexcept override { return _reflex->Stats(); }
        [[nodiscard]] std::uint64_t LowLatencyFrameId() const noexcept { return _reflex ? _reflex->FrameId() : 0; }
        void MarkLowLatency(LowLatencyMarker marker) override { _reflex->Mark(marker); }
        void AbandonLowLatencyFrame() noexcept override { if (_reflex) _reflex->AbandonFrame(); }
        [[nodiscard]] bool BeginLowLatencyFrame() override
        {
            SyncExclusive();
            if (_context._impl->fullScreenExclusive && FullscreenExclusive::Monitor()
                && !FullscreenExclusive::Active())
            {
                AbandonLowLatencyFrame();
                return true;
            }
            const auto before = _reflex->FrameId();
            const bool ready = _reflex->BeginFrame();
            if (ready && !before && _reflex->FrameId() && _context._impl->nvLowLatency2Revision >= 3
                && _context._impl->establishReflexFrame) _context._impl->establishReflexFrame();
            return ready;
        }

        void Resize(std::uint32_t width, std::uint32_t height) override
        {
            if (_acquired) throw std::logic_error("Cannot resize a Vulkan swapchain with an acquired image.");
            if (width == 0 || height == 0)
            {
                _suspended = true;
                _desc.width = 0;
                _desc.height = 0;
                _drawable = {};
                return;
            }
            int framebufferWidth = 0, framebufferHeight = 0;
            DrawableSize(framebufferWidth, framebufferHeight);
            if (framebufferWidth > 0 && framebufferHeight > 0)
            {
                width = static_cast<std::uint32_t>(framebufferWidth);
                height = static_cast<std::uint32_t>(framebufferHeight);
            }
            _suspended = false;
            if (_needsRecreate || width != _drawable.width || height != _drawable.height)
                Recreate(width, height);
        }

        [[nodiscard]] Texture& AcquireNextTexture() override
        {
            const FrameTelemetry::Scope measured(FrameTelemetry::Phase::Acquire);
            if (_closed) throw std::logic_error("The Vulkan swapchain is closed.");
            if (_acquired) throw std::logic_error("A Vulkan swapchain image is already acquired.");
            for (;;)
            {
                if (Closing())
                    throw std::runtime_error("The Vulkan presentation window is closing.");
                int width = 0, height = 0;
                DrawableSize(width, height);
                if (width <= 0 || height <= 0)
                {
                    _suspended = true;
                    _desc.width = 0;
                    _desc.height = 0;
                    _drawable = {};
                    WaitForDrawable();
                    continue;
                }
                const auto w = static_cast<std::uint32_t>(width);
                const auto h = static_cast<std::uint32_t>(height);
                SyncExclusive();
                if (_suspended || _needsRecreate || w != _drawable.width || h != _drawable.height)
                    Recreate(w, h);
                else if (_exclusiveWanted && !_exclusiveHeld && --_exclusiveRetryIn == 0)
                    AcquireExclusive();
                // The surface can still report a zero extent while the window
                // says otherwise -- a CAMetalLayer not yet laid out on macOS --
                // and Recreate then made no swapchain. Never acquire from none.
                if (_suspended || _swapchain == VK_NULL_HANDLE)
                {
                    WaitForDrawable();
                    continue;
                }

                Frame& frame = _frames[_frameIndex];
                CompleteFrame(frame, false);
                Check(_context._impl->vkResetCommandPool(_context._impl->device,
                    frame.pool, 0), "vkResetCommandPool");

                std::uint32_t imageIndex = 0;
                VkResult acquire = VK_TIMEOUT;
                for (int seconds = 2;; seconds += 2)
                {
                    acquire = _context._impl->vkAcquireNextImageKHR(
                        _context._impl->device, _swapchain, 2'000'000'000ULL,
                        frame.imageAvailable, VK_NULL_HANDLE, &imageIndex);
                    if (acquire != VK_TIMEOUT && acquire != VK_NOT_READY) break;
                    std::cerr << "[vulkan] still waiting to acquire a swapchain image after " << seconds << " s"
                        << std::endl;
                }
                if (acquire == VK_ERROR_OUT_OF_DATE_KHR || acquire == VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT)
                {
                    Recreate(w, h);
                    continue;
                }
                if (_transformSuboptimal && acquire == VK_SUBOPTIMAL_KHR) acquire = VK_SUCCESS;
                if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR)
                    Check(acquire, "vkAcquireNextImageKHR");
                if (imageIndex >= _images.size())
                    throw std::runtime_error("vkAcquireNextImageKHR returned an invalid image index.");

                // Do not wait for acquisition on the CPU. The queue waits on
                // imageAvailable before touching/signaling this image. Observe
                // its submission fence at slot reuse/teardown to retire older
                // chains. Keep current image history conservative until then.
                frame.Proof.Acquired(!_context._impl->swapchainMaintenance1
                    && _images[imageIndex].presentPending, _retirementSerial);

                ImageState& image = _images[imageIndex];
                // imageAvailable supplies the GPU dependency. The frame slot
                // above owns the CPU command pool; this image owns no mutable
                // CPU storage requiring an additional image/present host wait.
                _currentImage = imageIndex;
                _acquired = true;
                _commandsReady = false;
                _recreateAfterPresent = acquire == VK_SUBOPTIMAL_KHR;
                return *image.texture;
            }
        }

        // The frame loop's acquire: false, having acquired nothing, while the
        // window has no drawable area (minimized), where AcquireNextTexture
        // would wait for one and the loop that restores the window would
        // never run again.
        [[nodiscard]] bool TryAcquire()
        {
            if (_closed || Closing()) return false;
            SyncExclusive();
            if (_context._impl->fullScreenExclusive && FullscreenExclusive::Monitor()
                && !FullscreenExclusive::Active()) return false;
#if !defined(__ANDROID__)
            if (WindowSystem::Iconified(_window->NativeHandle()))
                return false;
#endif
            int width = 0, height = 0;
            DrawableSize(width, height);
            if (width <= 0 || height <= 0)
            {
                _suspended = true;
                _desc.width = 0;
                _desc.height = 0;
                _drawable = {};
                return false;
            }
            (void)AcquireNextTexture();
            return true;
        }

        AcquireResult TryAcquireTexture() override
        {
            if (_closed) { AbandonLowLatencyFrame(); return {PresentationStatus::SurfaceLost, nullptr}; }
            if (Closing())
            {
                AbandonLowLatencyFrame();
#if defined(__ANDROID__)
                return {PresentationStatus::SurfaceLost, nullptr};
#else
                return {PresentationStatus::TemporarilyUnavailable, nullptr};
#endif
            }
            try
            {
                if (!TryAcquire())
                {
                    AbandonLowLatencyFrame();
                    return {PresentationStatus::TemporarilyUnavailable, nullptr};
                }
                return {_recreateAfterPresent ? PresentationStatus::ResizeRequired : PresentationStatus::Ready,
                    _images[_currentImage].texture.get()};
            }
            catch (const BackendError& error) { AbandonLowLatencyFrame(); return FailedAcquire(error); }
        }
        PresentationCapabilities PresentationCaps() const noexcept override { return _presentationCaps; }
        PresentMode RequestedPresentMode() const noexcept override { return _requestedMode; }
        PresentResult TryPresent() override
        {
            if (!_acquired) AbandonLowLatencyFrame();
            if (_closed) return {PresentationStatus::SurfaceLost};
            if (!_acquired && Closing())
#if defined(__ANDROID__)
                return {PresentationStatus::SurfaceLost};
#else
                return {PresentationStatus::TemporarilyUnavailable};
#endif
            if (_suspended && !_acquired) return {PresentationStatus::TemporarilyUnavailable};
            if (!_acquired && _context._impl->fullScreenExclusive && FullscreenExclusive::Monitor()
                && !FullscreenExclusive::Active()) return {PresentationStatus::TemporarilyUnavailable};
#if !defined(__ANDROID__)
            // TryAcquire refuses an iconified (unexposed) window without
            // suspending; the present that follows must say the same thing.
            if (!_acquired && WindowSystem::Iconified(_window->NativeHandle()))
                return {PresentationStatus::TemporarilyUnavailable};
#endif
            try
            {
                return PresentCurrent();
            }
            catch (const BackendError& error) { return FailedPresent(error); }
        }

        void SetPresentMode(PresentMode mode) override
        {
            if (_acquired) throw std::logic_error("Cannot change Vulkan present mode with an acquired image.");
            _requestedMode = mode;
            _needsRecreate = true;
            if (!_suspended && _desc.width && _desc.height)
                Recreate(_desc.width, _desc.height);
        }

        void Present() override { (void)PresentCurrent(); }
        PresentResult PresentCurrent()
        {
            if (_suspended && !_acquired) return {PresentationStatus::TemporarilyUnavailable};
            if (!_acquired) throw std::logic_error("No Vulkan swapchain image is acquired.");
            if (!_commandsReady)
                throw std::logic_error("A Vulkan command list must render the acquired swapchain image before Present.");
            return SubmitAndPresent();
        }

        void ClearCurrent(float red, float green, float blue, float alpha)
        {
            if (!_acquired) throw std::logic_error("No Vulkan swapchain image is acquired.");
            if (_commandsReady) throw std::logic_error("The acquired Vulkan image already has recorded commands.");
            Frame& frame = _frames[_frameIndex];
            VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            Check(_context._impl->vkBeginCommandBuffer(frame.command, &begin), "vkBeginCommandBuffer(clear)");

            VulkanSwapchainTexture& texture = *_images[_currentImage].texture;
            VkImageMemoryBarrier2 toColor{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
            toColor.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
            toColor.srcAccessMask = VK_ACCESS_2_NONE;
            toColor.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            toColor.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            toColor.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            toColor.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            toColor.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toColor.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toColor.image = texture.Image();
            toColor.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            dependency.imageMemoryBarrierCount = 1;
            dependency.pImageMemoryBarriers = &toColor;
            _context._impl->vkCmdPipelineBarrier2(frame.command, &dependency);

            VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
            color.imageView = texture.View();
            color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            color.clearValue.color.float32[0] = red;
            color.clearValue.color.float32[1] = green;
            color.clearValue.color.float32[2] = blue;
            color.clearValue.color.float32[3] = alpha;
            VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
            rendering.renderArea.extent = {texture.Desc().width, texture.Desc().height};
            rendering.layerCount = 1;
            rendering.colorAttachmentCount = 1;
            rendering.pColorAttachments = &color;
            _context._impl->vkCmdBeginRendering(frame.command, &rendering);
            _context._impl->vkCmdEndRendering(frame.command);

            VkImageMemoryBarrier2 toPresent{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
            toPresent.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            toPresent.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            toPresent.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
            toPresent.dstAccessMask = VK_ACCESS_2_NONE;
            toPresent.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            toPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toPresent.image = texture.Image();
            toPresent.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            dependency.pImageMemoryBarriers = &toPresent;
            _context._impl->vkCmdPipelineBarrier2(frame.command, &dependency);
            Check(_context._impl->vkEndCommandBuffer(frame.command), "vkEndCommandBuffer(clear)");
            _commandsReady = true;
        }

        // The frame's picture: the window target, whose rows run bottom-up
        // as OpenGL's do, blitted upright into the acquired image. This is
        // the one place the backend's row order meets the screen's.
        void BlitCurrent(VkImage source, VkImageLayout layout, VkPipelineStageFlags2 stages,
            VkAccessFlags2 access, VkExtent2D extent)
        {
            if (!_acquired) throw std::logic_error("No Vulkan swapchain image is acquired.");
            if (_commandsReady) throw std::logic_error("The acquired Vulkan image already has recorded commands.");
            auto& vk = *_context._impl;
            Frame& frame = _frames[_frameIndex];
            VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            Check(vk.vkBeginCommandBuffer(frame.command, &begin), "vkBeginCommandBuffer(blit)");
            VulkanSwapchainTexture& texture = *_images[_currentImage].texture;
            const auto barrier = [&](VkImage image, VkImageLayout from, VkImageLayout to,
                VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess)
            {
                VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
                b.srcStageMask = srcStage; b.srcAccessMask = srcAccess;
                b.dstStageMask = dstStage; b.dstAccessMask = dstAccess;
                b.oldLayout = from; b.newLayout = to;
                b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                b.image = image; b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
                dependency.imageMemoryBarrierCount = 1; dependency.pImageMemoryBarriers = &b;
                vk.vkCmdPipelineBarrier2(frame.command, &dependency);
            };
            barrier(texture.Image(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
            if (source != VK_NULL_HANDLE)
            {
                barrier(source, layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, stages, access,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
                VkImageBlit region{};
                region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                region.srcOffsets[0] = {0, static_cast<std::int32_t>(extent.height), 0};
                region.srcOffsets[1] = {static_cast<std::int32_t>(extent.width), 0, 1};
                region.dstOffsets[0] = {0, 0, 0};
                region.dstOffsets[1] = {static_cast<std::int32_t>(texture.Desc().width),
                    static_cast<std::int32_t>(texture.Desc().height), 1};
                vk.vkCmdBlitImage(frame.command, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    texture.Image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, VK_FILTER_NEAREST);
                barrier(source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, layout,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, stages, access);
            }
            else
            {
                const VkClearColorValue black{{0.0F, 0.0F, 0.0F, 1.0F}};
                const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                vk.vkCmdClearColorImage(frame.command, texture.Image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    &black, 1, &range);
            }
            barrier(texture.Image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE);
            Check(vk.vkEndCommandBuffer(frame.command), "vkEndCommandBuffer(blit)");
            _commandsReady = true;
        }

        [[nodiscard]] bool ValidationEnabled() const noexcept { return _context.ValidationEnabled(); }
        [[nodiscard]] bool ExclusiveSupported() const noexcept { return _context._impl->fullScreenExclusive; }
        [[nodiscard]] unsigned ValidationErrors() const noexcept { return _context.ValidationErrors(); }
        [[nodiscard]] bool PresentFencesEnabled() const noexcept { return _context._impl->swapchainMaintenance1; }
        [[nodiscard]] std::uint64_t PresentFenceWaits() const noexcept { return _presentFenceWaits; }
        [[nodiscard]] std::uint64_t FallbackRetiredReleases() const noexcept { return _fallbackRetiredReleases; }
        [[nodiscard]] std::uint64_t FallbackCompletedProofs() const noexcept { return _fallbackCompletedProofs; }
        [[nodiscard]] std::uint64_t FrameFenceWaits() const noexcept { return _frameFenceWaits; }
        [[nodiscard]] std::uint64_t CleanupFrameFenceWaits() const noexcept { return _cleanupFrameFenceWaits; }

        void Close()
        {
            if (_closed) return;
            WaitOutstanding();
            _reflex->Shutdown(); _reflex.reset(); _context._impl->reflex = nullptr;
            DestroyRetired();
            DestroyImageStates();
            DestroyFrames();
            if (_swapchain)
            {
                _context._impl->vkDestroySwapchainKHR(_context._impl->device, _swapchain, nullptr);
                _swapchain = VK_NULL_HANDLE;
            }
            if (_ownedContext) _context.Shutdown();
            _closed = true;
        }

    private:
        struct Frame final
        {
            VkCommandPool pool = VK_NULL_HANDLE;
            VkCommandBuffer command = VK_NULL_HANDLE;
            VkSemaphore imageAvailable = VK_NULL_HANDLE;
            VkFence fence = VK_NULL_HANDLE;
            VulkanReacquireProof Proof;
        };

        struct ImageState final
        {
            struct PresentCompletion final
            {
                VkFence Fence = VK_NULL_HANDLE;
                bool Pending = false;
            };
            std::unique_ptr<VulkanSwapchainTexture> texture;
            VkSemaphore renderFinished = VK_NULL_HANDLE;
            // Completion proofs are independent of image admission. A bounded
            // ring allows recording without waiting on the previous present.
            std::array<PresentCompletion, 4> Completions{};
            std::size_t NextCompletion = 0;
            bool presentPending = false;
        };

        struct RetiredSwapchain final
        {
            std::uint64_t Serial = 0;
            VkSwapchainKHR chain = VK_NULL_HANDLE;
            std::vector<ImageState> images;
        };

        static constexpr std::size_t FrameCount = 2;
        ::MphRead::RendererPlatform::Window* _window = nullptr;
        std::unique_ptr<Context> _ownedContext;
        Context& _context;
#if !defined(__ANDROID__)
        void* _nativeWindow = nullptr;
#endif

        // The drawable extent: the GLFW framebuffer on the desktop; on
        // Android, the surface's current extent (zero while it has none).
        void DrawableSize(int& width, int& height)
        {
#if defined(__ANDROID__)
            VkSurfaceCapabilitiesKHR capabilities{};
            width = 0;
            height = 0;
            if (!_context._impl->surface) return;
            if (_context._impl->vkGetPhysicalDeviceSurfaceCapabilitiesKHR(_context._impl->physical,
                    _context._impl->surface, &capabilities) != VK_SUCCESS) return;
            if (capabilities.currentExtent.width == 0xFFFFFFFFU)
            {
                width = static_cast<int>(_desc.width);
                height = static_cast<int>(_desc.height);
                return;
            }
            width = static_cast<int>(capabilities.currentExtent.width);
            height = static_cast<int>(capabilities.currentExtent.height);
#else
            WindowSystem::FramebufferSize(_nativeWindow, width, height);
#endif
        }
        [[nodiscard]] bool Closing() const
        {
#if defined(__ANDROID__)
            return !_context._impl->surface;
#else
            return WindowSystem::ShouldClose(_nativeWindow);
#endif
        }
        static void WaitForDrawable()
        {
#if defined(__ANDROID__)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
#else
            WindowSystem::WaitEvents(0.05);
#endif
        }
        SwapchainDesc _desc{};
        PresentMode _requestedMode = PresentMode::Fifo;
        PresentationCapabilities _presentationCaps{};
        VkSwapchainKHR _swapchain = VK_NULL_HANDLE;
        std::unique_ptr<VulkanNvidiaReflex> _reflex;
        VkFormat _vkFormat = VK_FORMAT_UNDEFINED;
        VkExtent2D _extent{};
        // The drawable size the swapchain was made for, which is what a resize
        // is detected against. Not _desc: the surface's own extent wins over
        // the request, and the toolkit's size is its logical size times the
        // scale -- 1707 x 1.5 = 2561 for a window 2560 pixels wide at 150 % --
        // so comparing the two recreated the swapchain on every frame of
        // borderless fullscreen (7 fps on an Iris Xe laptop).
        VkExtent2D _drawable{};
        std::array<Frame, FrameCount> _frames{};
        std::vector<ImageState> _images;
        std::vector<RetiredSwapchain> _retired;
        std::uint32_t _frameIndex = 0;
        std::uint32_t _currentImage = 0;
        bool _acquired = false;
        bool _commandsReady = false;
        bool _suspended = false;
        bool _needsRecreate = true;
        bool _recreateAfterPresent = false;
        // The swapchain's transform is not the surface's, by choice (Recreate).
        bool _transformSuboptimal = false;
        bool _closed = false;
        // Exclusive fullscreen: the request generation the swapchain was made
        // for, whether it holds the display, and when to try again after the
        // driver took it away (alt-tab, another fullscreen program).
        std::uint64_t _exclusiveGeneration = 0;
        void* _exclusiveMonitor = nullptr;
        bool _exclusiveWanted = false;
        bool _exclusiveHeld = false;
        std::uint32_t _exclusiveRetryIn = 0;
        std::uint64_t _presentFenceWaits = 0;
        std::uint64_t _fallbackRetiredReleases = 0;
        std::uint64_t _fallbackCompletedProofs = 0;
        std::uint64_t _retirementSerial = 0;
        std::uint64_t _frameFenceWaits = 0, _cleanupFrameFenceWaits = 0;

        void InitializeFrames()
        {
            auto& vk = *_context._impl;
            for (Frame& frame : _frames)
            {
                VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
                pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
                pool.queueFamilyIndex = vk.graphicsFamily;
                Check(vk.vkCreateCommandPool(vk.device, &pool, nullptr, &frame.pool), "vkCreateCommandPool(frame)");
                VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
                allocate.commandPool = frame.pool;
                allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                allocate.commandBufferCount = 1;
                Check(vk.vkAllocateCommandBuffers(vk.device, &allocate, &frame.command), "vkAllocateCommandBuffers(frame)");
                VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
                Check(vk.vkCreateSemaphore(vk.device, &semaphore, nullptr, &frame.imageAvailable), "vkCreateSemaphore(image available)");
                VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
                fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
                Check(vk.vkCreateFence(vk.device, &fence, nullptr, &frame.fence), "vkCreateFence(frame)");
            }
        }

        void SyncExclusive()
        {
            const auto generation = FullscreenExclusive::Generation();
            if (_exclusiveGeneration == generation) return;
            auto& vk = *_context._impl;
            void* monitor = vk.fullScreenExclusive ? FullscreenExclusive::Monitor() : nullptr;
            if (monitor != _exclusiveMonitor)
            {
                _needsRecreate = true;
                return;
            }
            _exclusiveGeneration = generation;
            _exclusiveWanted = monitor != nullptr && FullscreenExclusive::Active();
            // Reuse application-controlled chains across focus changes. The
            // release API forbids presentation until reacquired; TryAcquire
            // reports temporary unavailability while simulation keeps running.
            if (_exclusiveHeld && !_exclusiveWanted)
            {
                (void)reinterpret_cast<FullScreenExclusiveModeFn>(vk.vkReleaseFullScreenExclusiveModeEXT)(
                    vk.device, _swapchain);
                _exclusiveHeld = false;
                std::cout << "[vulkan] full-screen exclusive released (focus)\n";
            }
            else if (_exclusiveWanted && !_exclusiveHeld && _swapchain)
                AcquireExclusive();
        }

        void AcquireExclusive()
        {
            auto& vk = *_context._impl;
            const auto acquire = reinterpret_cast<FullScreenExclusiveModeFn>(vk.vkAcquireFullScreenExclusiveModeEXT);
            const VkResult result = acquire(vk.device, _swapchain);
            const bool held = result == VK_SUCCESS;
            if (held != _exclusiveHeld || !held)
                std::cout << "[vulkan] full-screen exclusive " << (held ? "acquired" : "not acquired")
                          << " (" << static_cast<int>(result) << ")" << std::endl;
            _exclusiveHeld = held;
            // Refused while the window is not in front: ask again in about a
            // second rather than every frame.
            _exclusiveRetryIn = held ? 0 : 60;
        }

        void Recreate(std::uint32_t requestedWidth, std::uint32_t requestedHeight)
        {
            if (_acquired) throw std::logic_error("Cannot recreate a Vulkan swapchain with an acquired image.");
            WaitOutstanding();
            auto& vk = *_context._impl;
            VkSurfaceCapabilitiesKHR capabilities{};
            Check(vk.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk.physical, vk.surface, &capabilities),
                "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
            if ((capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0)
                throw std::runtime_error("The Vulkan surface does not support color-attachment swapchain images.");

            std::uint32_t formatCount = 0;
            Check(vk.vkGetPhysicalDeviceSurfaceFormatsKHR(vk.physical, vk.surface, &formatCount, nullptr),
                "vkGetPhysicalDeviceSurfaceFormatsKHR(count)");
            if (!formatCount) throw std::runtime_error("The Vulkan surface exposes no swapchain formats.");
            std::vector<VkSurfaceFormatKHR> formats(formatCount);
            Check(vk.vkGetPhysicalDeviceSurfaceFormatsKHR(vk.physical, vk.surface, &formatCount, formats.data()),
                "vkGetPhysicalDeviceSurfaceFormatsKHR");
            VkSurfaceFormatKHR selectedFormat = formats.front();
            const VkFormat requestedFormat = ToVkFormat(_desc.format);
            auto preferred = std::find_if(formats.begin(), formats.end(), [requestedFormat](const auto& format) {
                return format.format == requestedFormat && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
            });
            if (preferred != formats.end()) selectedFormat = *preferred;
            else if (formats.size() == 1 && formats.front().format == VK_FORMAT_UNDEFINED)
                selectedFormat = {requestedFormat, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
            else
            {
                auto unorm = std::find_if(formats.begin(), formats.end(), [](const auto& format) {
                    return (format.format == VK_FORMAT_B8G8R8A8_UNORM
                        || format.format == VK_FORMAT_R8G8B8A8_UNORM)
                        && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
                });
                if (unorm != formats.end()) selectedFormat = *unorm;
            }
            _vkFormat = selectedFormat.format;
            const TextureFormat rhiFormat = ToRhiFormat(_vkFormat);

            std::uint32_t modeCount = 0;
            Check(vk.vkGetPhysicalDeviceSurfacePresentModesKHR(vk.physical, vk.surface, &modeCount, nullptr),
                "vkGetPhysicalDeviceSurfacePresentModesKHR(count)");
            std::vector<VkPresentModeKHR> modes(modeCount);
            Check(vk.vkGetPhysicalDeviceSurfacePresentModesKHR(vk.physical, vk.surface, &modeCount, modes.data()),
                "vkGetPhysicalDeviceSurfacePresentModesKHR");
            VkPresentModeKHR requestedPresent = VK_PRESENT_MODE_FIFO_KHR;
            if (_requestedMode == PresentMode::Immediate) requestedPresent = VK_PRESENT_MODE_IMMEDIATE_KHR;
            else if (_requestedMode == PresentMode::Mailbox) requestedPresent = VK_PRESENT_MODE_MAILBOX_KHR;
            const bool exactMode = std::find(modes.begin(), modes.end(), requestedPresent) != modes.end();
            _presentationCaps = {
                std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_IMMEDIATE_KHR) != modes.end(),
                true,
                std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_MAILBOX_KHR) != modes.end(),
                capabilities.minImageCount, capabilities.maxImageCount};
            const VkPresentModeKHR selectedMode = exactMode ? requestedPresent : VK_PRESENT_MODE_FIFO_KHR;
            if (!exactMode && _requestedMode != PresentMode::Fifo)
                std::cout << "[vulkan] requested present mode unavailable; using FIFO\n";

            VkExtent2D extent{};
            if (capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max())
                extent = capabilities.currentExtent;
            else
            {
                extent.width = std::clamp(requestedWidth,
                    capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
                extent.height = std::clamp(requestedHeight,
                    capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
            }
            if (!extent.width || !extent.height)
            {
                _suspended = true;
                _desc.width = _desc.height = 0;
                _drawable = {};
                return;
            }
            std::uint32_t imageCount = std::max(_desc.imageCount, capabilities.minImageCount + 1);
            if (capabilities.maxImageCount && imageCount > capabilities.maxImageCount)
                imageCount = capabilities.maxImageCount;
            VkCompositeAlphaFlagBitsKHR composite = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
            if ((capabilities.supportedCompositeAlpha & composite) == 0)
            {
                constexpr VkCompositeAlphaFlagBitsKHR options[]{
                    VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                    VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
                    VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR};
                const auto found = std::find_if(std::begin(options), std::end(options),
                    [&capabilities](auto value) { return (capabilities.supportedCompositeAlpha & value) != 0; });
                if (found == std::end(options)) throw std::runtime_error("No supported Vulkan composite-alpha mode.");
                composite = *found;
            }

            const VkSwapchainKHR oldSwapchain = _swapchain;
            VkSwapchainCreateInfoKHR create{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
            create.surface = vk.surface;
            create.minImageCount = imageCount;
            create.imageFormat = selectedFormat.format;
            create.imageColorSpace = selectedFormat.colorSpace;
            create.imageExtent = extent;
            create.imageArrayLayers = 1;
            create.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                | (capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT);
            const std::uint32_t queueFamilies[]{vk.graphicsFamily, vk.presentFamily};
            if (vk.graphicsFamily != vk.presentFamily)
            {
                create.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
                create.queueFamilyIndexCount = 2;
                create.pQueueFamilyIndices = queueFamilies;
            }
            else create.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            create.preTransform = capabilities.currentTransform;
#if defined(__ANDROID__)
            // A landscape game on a panel that is portrait at rest reports a
            // 90-degree current transform: chosen, it says the picture comes
            // already turned, and nothing here turns it. Identity has the
            // compositor do it.
            if (capabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
                create.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
#endif
            // Android then calls every acquire and present suboptimal: that is
            // the choice, not a resize (which comes as out-of-date).
            _transformSuboptimal = create.preTransform != capabilities.currentTransform;
            create.compositeAlpha = composite;
            create.presentMode = selectedMode;
            create.clipped = VK_TRUE;
            create.oldSwapchain = oldSwapchain;
            _exclusiveGeneration = FullscreenExclusive::Generation();
            void* const exclusiveMonitor = vk.fullScreenExclusive ? FullscreenExclusive::Monitor() : nullptr;
            SurfaceFullScreenExclusiveWin32Info exclusiveMonitorInfo{};
            SurfaceFullScreenExclusiveInfo exclusiveInfo{};
            if (exclusiveMonitor != nullptr)
            {
                exclusiveMonitorInfo.hmonitor = exclusiveMonitor;
                exclusiveMonitorInfo.pNext = create.pNext;
                exclusiveInfo.fullScreenExclusive = FullScreenExclusiveApplicationControlled;
                exclusiveInfo.pNext = &exclusiveMonitorInfo;
                create.pNext = &exclusiveInfo;
            }
            else if (vk.fullScreenExclusive)
            {
                // Otherwise say no outright. Left at the default the driver
                // takes the display on its own whenever the window covers the
                // monitor, and then reports it lost on every present once the
                // window is in the background -- a recreate a frame.
                exclusiveInfo.fullScreenExclusive = FullScreenExclusiveDisallowed;
                exclusiveInfo.pNext = const_cast<void*>(create.pNext);
                create.pNext = &exclusiveInfo;
            }
            if (_exclusiveHeld && oldSwapchain)
            {
                (void)reinterpret_cast<FullScreenExclusiveModeFn>(vk.vkReleaseFullScreenExclusiveModeEXT)(
                    vk.device, oldSwapchain);
            }
            _exclusiveHeld = false;
            const bool exclusiveWanted = exclusiveMonitor != nullptr && FullscreenExclusive::Active();
            if (_exclusiveWanted != exclusiveWanted)
                std::cout << "[vulkan] full-screen exclusive " << (exclusiveWanted ? "requested" : "released")
                          << std::endl;
            _exclusiveMonitor = exclusiveMonitor;
            _exclusiveWanted = exclusiveWanted;
            VkSwapchainLatencyCreateInfoNV latency{VK_STRUCTURE_TYPE_SWAPCHAIN_LATENCY_CREATE_INFO_NV};
            // FRUITY_REFLEX_DISABLE_SWAPCHAIN_LATENCY_MODE: developer A/B on
            // whether the opt-in itself changes cadence. Default: chained.
            static const bool latencyOptOut = [] {
                const char* v = std::getenv("FRUITY_REFLEX_DISABLE_SWAPCHAIN_LATENCY_MODE");
                const bool off = v && *v && std::string_view(v) != "0";
                if (off) std::cout << "[reflex] diagnostic: swapchain latency opt-in disabled" << std::endl;
                return off;
            }();
            if (vk.nvLowLatency2 && !latencyOptOut)
            { latency.latencyModeEnable = VK_TRUE; latency.pNext = create.pNext; create.pNext = &latency; }
            _reflex->SetSwapchain(VK_NULL_HANDLE);
            VkSwapchainKHR replacement = VK_NULL_HANDLE;
            const VkResult createResult = vk.vkCreateSwapchainKHR(vk.device, &create, nullptr, &replacement);
            if (createResult != VK_SUCCESS)
            {
                if (oldSwapchain)
                {
                    RetireImages(oldSwapchain);
                    _swapchain = VK_NULL_HANDLE;
                }
                Check(createResult, "vkCreateSwapchainKHR");
            }
            _swapchain = replacement;
            _reflex->SetSwapchain(replacement);
            if (_exclusiveWanted) AcquireExclusive();
            if (oldSwapchain)
            {
                RetireImages(oldSwapchain);
            }
            _extent = extent;
            _drawable = {requestedWidth, requestedHeight};
            _desc.width = extent.width;
            _desc.height = extent.height;
            _desc.format = rhiFormat;
            _desc.presentMode = selectedMode == VK_PRESENT_MODE_IMMEDIATE_KHR ? PresentMode::Immediate
                : selectedMode == VK_PRESENT_MODE_MAILBOX_KHR ? PresentMode::Mailbox : PresentMode::Fifo;

            std::uint32_t actualCount = 0;
            Check(vk.vkGetSwapchainImagesKHR(vk.device, _swapchain, &actualCount, nullptr),
                "vkGetSwapchainImagesKHR(count)");
            std::vector<VkImage> images(actualCount);
            Check(vk.vkGetSwapchainImagesKHR(vk.device, _swapchain, &actualCount, images.data()),
                "vkGetSwapchainImagesKHR");
            _desc.imageCount = actualCount;
            _images.reserve(actualCount);
            for (VkImage image : images)
            {
                VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
                view.image = image;
                view.viewType = VK_IMAGE_VIEW_TYPE_2D;
                view.format = _vkFormat;
                view.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                    VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
                view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                VkImageView imageView = VK_NULL_HANDLE;
                ImageState state{};
                try
                {
                Check(vk.vkCreateImageView(vk.device, &view, nullptr, &imageView), "vkCreateImageView(swapchain)");
                state.texture = std::make_unique<VulkanSwapchainTexture>(image, imageView, extent, rhiFormat);
                VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
                Check(vk.vkCreateSemaphore(vk.device, &semaphore, nullptr, &state.renderFinished),
                    "vkCreateSemaphore(render finished)");
                if (vk.swapchainMaintenance1)
                {
                    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
                    fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
                    for (auto& completion : state.Completions)
                        Check(vk.vkCreateFence(vk.device, &fence, nullptr, &completion.Fence),
                            "vkCreateFence(present completion)");
                }
                _images.push_back(std::move(state));
                }
                catch (...)
                {
                    for (const auto& completion : state.Completions)
                        if (completion.Fence) vk.vkDestroyFence(vk.device, completion.Fence, nullptr);
                    if (state.renderFinished) vk.vkDestroySemaphore(vk.device, state.renderFinished, nullptr);
                    if (imageView) vk.vkDestroyImageView(vk.device, imageView, nullptr);
                    throw;
                }
            }
            _suspended = false;
            _needsRecreate = false;
            std::cout << "[vulkan] swapchain " << _desc.width << 'x' << _desc.height
                << " images=" << _desc.imageCount << " present="
                << (_desc.presentMode == PresentMode::Fifo ? "FIFO"
                    : _desc.presentMode == PresentMode::Mailbox ? "MAILBOX" : "IMMEDIATE") << std::endl;
        }

        void WaitForPresent(ImageState& image)
        {
            for (auto& completion : image.Completions)
            {
                if (!completion.Pending) continue;
                Check(FrameTelemetry::HostWait([&] { return WaitFenceReporting(_context._impl->vkWaitForFences, _context._impl->device,
                    &completion.Fence, "vkWaitForFences(present cleanup)"); }), "vkWaitForFences(present cleanup)");
                ++_presentFenceWaits;
                completion.Pending = false;
            }
            if (image.Completions[0].Fence) image.presentPending = false;
        }

        ImageState::PresentCompletion* AdmitPresentCompletion(ImageState& image)
        {
            if (!image.Completions[0].Fence) return nullptr;
            auto& vk = *_context._impl;
            for (std::size_t offset = 0; offset < image.Completions.size(); ++offset)
            {
                const auto index = (image.NextCompletion + offset) % image.Completions.size();
                auto& completion = image.Completions[index];
                if (completion.Pending)
                {
                    const auto result = vk.vkGetFenceStatus(vk.device, completion.Fence);
                    if (result == VK_NOT_READY) continue;
                    Check(result, "vkGetFenceStatus(present completion)");
                    completion.Pending = false;
                }
                image.NextCompletion = (index + 1) % image.Completions.size();
                return &completion;
            }
            // Exhaustion is the only admission wait. Never reset a fence still
            // referenced by presentation, or let completion storage grow forever.
            auto& completion = image.Completions[image.NextCompletion];
            Check(FrameTelemetry::HostWait([&] { return WaitFenceReporting(vk.vkWaitForFences, vk.device, &completion.Fence,
                "vkWaitForFences(present ring exhaustion)"); }), "vkWaitForFences(present ring exhaustion)");
            ++_presentFenceWaits; completion.Pending = false;
            image.NextCompletion = (image.NextCompletion + 1) % image.Completions.size();
            return &completion;
        }

        void CompleteFrame(Frame& frame, bool cleanup)
        {
            if (!frame.Proof.Pending()) return;
            auto& vk = *_context._impl;
            auto result = vk.vkGetFenceStatus(vk.device, frame.fence);
            if (result == VK_NOT_READY)
            {
                if (cleanup) ++_cleanupFrameFenceWaits;
                else ++_frameFenceWaits;
                result = FrameTelemetry::HostWait([&] { return WaitFenceReporting(vk.vkWaitForFences, vk.device, &frame.fence,
                    cleanup ? "frame cleanup" : "frame slot exhaustion"); });
            }
            Check(result, "vkGetFenceStatus/wait(frame completion)");
            const auto through = frame.Proof.CompleteAfterFenceSignal();
            if (through)
            {
                ++_fallbackCompletedProofs;
                DestroyRetiredThrough(through);
            }
        }

        void WaitOutstanding()
        {
            auto& vk = *_context._impl;
            if (!vk.device) return;
            for (Frame& frame : _frames) CompleteFrame(frame, true);
            for (ImageState& image : _images) WaitForPresent(image);
            FrameTelemetry::Count(FrameTelemetry::Counter::DeviceIdle);
            Check(vk.vkDeviceWaitIdle(vk.device), "vkDeviceWaitIdle(swapchain)");
        }

        void DestroyImageStates(std::vector<ImageState>& images) noexcept
        {
            if (!_context._impl || !_context._impl->device) { images.clear(); return; }
            auto& vk = *_context._impl;
            for (ImageState& image : images)
            {
                if (image.renderFinished) vk.vkDestroySemaphore(vk.device, image.renderFinished, nullptr);
                for (const auto& completion : image.Completions)
                    if (completion.Fence) vk.vkDestroyFence(vk.device, completion.Fence, nullptr);
                if (image.texture && image.texture->View()) vk.vkDestroyImageView(vk.device, image.texture->View(), nullptr);
            }
            images.clear();
        }

        void DestroyImageStates() noexcept { DestroyImageStates(_images); }

        void RetireImages(VkSwapchainKHR chain)
        {
            auto& vk = *_context._impl;
            if (!vk.swapchainMaintenance1 && std::any_of(_images.begin(), _images.end(),
                [](const ImageState& image) { return image.presentPending; }))
            {
                _retired.emplace_back();
                _retired.back().Serial = ++_retirementSerial;
                _retired.back().chain = chain;
                _retired.back().images = std::move(_images);
            }
            else
            {
                DestroyImageStates();
                vk.vkDestroySwapchainKHR(vk.device, chain, nullptr);
            }
        }

        void DestroyRetired() noexcept
        {
            auto& vk = *_context._impl;
            for (RetiredSwapchain& retired : _retired)
            {
                DestroyImageStates(retired.images);
                vk.vkDestroySwapchainKHR(vk.device, retired.chain, nullptr);
            }
            _retired.clear();
        }

        void DestroyRetiredThrough(std::uint64_t through) noexcept
        {
            // A proof's epoch never covers retirements made after acquisition.
            // The list is ordered; out-of-order frame completions may find an
            // already-released prefix, which is harmless.
            auto& vk = *_context._impl;
            std::size_t released = 0;
            for (auto& retired : _retired)
            {
                if (retired.Serial > through) break;
                DestroyImageStates(retired.images);
                vk.vkDestroySwapchainKHR(vk.device, retired.chain, nullptr);
                ++released;
            }
            _fallbackRetiredReleases += released;
            _retired.erase(_retired.begin(), _retired.begin() + released);
        }

        void DestroyFrames() noexcept
        {
            if (!_context._impl || !_context._impl->device) return;
            auto& vk = *_context._impl;
            for (Frame& frame : _frames)
            {
                if (frame.imageAvailable) vk.vkDestroySemaphore(vk.device, frame.imageAvailable, nullptr);
                if (frame.fence) vk.vkDestroyFence(vk.device, frame.fence, nullptr);
                if (frame.pool) vk.vkDestroyCommandPool(vk.device, frame.pool, nullptr);
                frame = {};
            }
        }

        void CleanupNoThrow() noexcept
        {
            if (!_context._impl || !_context._impl->device) return;
            auto& vk = *_context._impl;
            if (vk.vkDeviceWaitIdle) vk.vkDeviceWaitIdle(vk.device);
            if (_reflex) { _reflex->Shutdown(); _reflex.reset(); }
            vk.reflex = nullptr;
            DestroyRetired();
            DestroyImageStates();
            DestroyFrames();
            if (_swapchain && vk.vkDestroySwapchainKHR)
            {
                vk.vkDestroySwapchainKHR(vk.device, _swapchain, nullptr);
                _swapchain = VK_NULL_HANDLE;
            }
        }

        PresentResult SubmitAndPresent()
        {
            auto& vk = *_context._impl;
            Frame& frame = _frames[_frameIndex];
            ImageState& image = _images[_currentImage];
            auto* const completion = AdmitPresentCompletion(image);
            if (completion)
            {
                Check(vk.vkResetFences(vk.device, 1, &completion->Fence), "vkResetFences(present)");
            }
            VkSemaphoreSubmitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
            wait.semaphore = frame.imageAvailable;
            wait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            VkCommandBufferSubmitInfo command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
            command.commandBuffer = frame.command;
            VkSemaphoreSubmitInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
            signal.semaphore = image.renderFinished;
            signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
            submit.waitSemaphoreInfoCount = 1;
            submit.pWaitSemaphoreInfos = &wait;
            submit.commandBufferInfoCount = 1;
            submit.pCommandBufferInfos = &command;
            submit.signalSemaphoreInfoCount = 1;
            submit.pSignalSemaphoreInfos = &signal;
            Check(vk.vkResetFences(vk.device, 1, &frame.fence), "vkResetFences(frame)");
            VkLatencySubmissionPresentIdNV attribution{VK_STRUCTURE_TYPE_LATENCY_SUBMISSION_PRESENT_ID_NV};
            if (const auto id = _reflex->SubmissionId()) { attribution.presentID = *id; attribution.pNext = submit.pNext; submit.pNext = &attribution; }
            _reflex->Mark(LowLatencyMarker::RenderSubmitStart);
            FrameTelemetry::Count(FrameTelemetry::Counter::QueueSubmits);
            Check(vk.vkQueueSubmit2(vk.graphics, 1, &submit, frame.fence), "vkQueueSubmit2(present)");
            _reflex->Mark(LowLatencyMarker::RenderSubmitEnd);
            frame.Proof.Submitted();

            VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
            present.waitSemaphoreCount = 1;
            present.pWaitSemaphores = &image.renderFinished;
            present.swapchainCount = 1;
            present.pSwapchains = &_swapchain;
            present.pImageIndices = &_currentImage;
            VkSwapchainPresentFenceInfoEXT presentFenceInfo{
                VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT};
            if (completion)
            {
                presentFenceInfo.swapchainCount = 1;
                presentFenceInfo.pFences = &completion->Fence;
                present.pNext = &presentFenceInfo;
            }
            const auto frameId = _reflex->FrameId();
            VkPresentIdKHR presentId{VK_STRUCTURE_TYPE_PRESENT_ID_KHR};
            if (frameId)
            { presentId.swapchainCount = 1; presentId.pPresentIds = &frameId; presentId.pNext = present.pNext; present.pNext = &presentId; }
            _reflex->Mark(LowLatencyMarker::PresentStart);
            VkResult result = vk.vkQueuePresentKHR(vk.present, &present);
            if (_transformSuboptimal && result == VK_SUBOPTIMAL_KHR) result = VK_SUCCESS;
            _reflex->Mark(LowLatencyMarker::PresentEnd);
            _reflex->FinishFrame();
            // Rejected surface/out-of-date presents still enqueue their wait
            // operations. Their completion fence must be waited before cleanup.
            if (result == VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT)
            {
                // The display was taken back (alt-tab): the present still
                // enqueued its waits; remake the chain and ask again.
                _exclusiveHeld = false;
                image.presentPending = true;
                if (completion) completion->Pending = true;
                _needsRecreate = true;
                result = VK_SUCCESS;
            }
            if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR
                || result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_ERROR_SURFACE_LOST_KHR)
            {
                image.presentPending = true;
                if (completion) completion->Pending = true;
            }
            if (result == VK_ERROR_OUT_OF_DATE_KHR)
                _needsRecreate = true;
            else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
                Check(result, "vkQueuePresentKHR");

            _acquired = false;
            _commandsReady = false;
            if (_recreateAfterPresent || result == VK_SUBOPTIMAL_KHR)
                _needsRecreate = true;
            const auto outcome = NativePresentResult(result, _recreateAfterPresent);
            _recreateAfterPresent = false;
            _frameIndex = (_frameIndex + 1) % static_cast<std::uint32_t>(FrameCount);
            // The platform window loop owns event delivery. Presenting must
            // not recursively pump Qt input/window events inside that frame.
            return outcome;
        }
    };
    std::unique_ptr<Swapchain> CreateSurfaceSwapchain(Context& context, const SwapchainDesc& desc)
    {
        return std::make_unique<VulkanSwapchain>(context, desc);
    }

    std::unique_ptr<Swapchain> CreateSwapchain(
        ::MphRead::RendererPlatform::Window& window, const SwapchainDesc& desc)
    {
        return std::make_unique<VulkanSwapchain>(window, desc);
    }

    std::unique_ptr<Swapchain> CreateSwapchain(Context& context,
        ::MphRead::RendererPlatform::Window& window, const SwapchainDesc& desc)
    {
        return std::make_unique<VulkanSwapchain>(context, window, desc);
    }

    void RecordSwapchainBlit(Swapchain& swapchain, VkImage source, VkImageLayout layout,
        VkPipelineStageFlags2 stages, VkAccessFlags2 access, VkExtent2D extent)
    {
        dynamic_cast<VulkanSwapchain&>(swapchain).BlitCurrent(source, layout, stages, access, extent);
    }

#if !defined(__ANDROID__)
    int RunPresentationCheck(bool forceFallback, bool reflexCheck)
    {
        try
        {
            using namespace ::MphRead::RendererPlatform;
            WindowSettings settings{};
            settings.GraphicsMode = GraphicsWindowMode::NoApi;
            settings.ClientSize = ::OpenTK::Mathematics::Vector2i(1280, 720);
            settings.Title = std::string(Mods::Branding::Name) + " Vulkan presentation check";
            settings.StartVisible = true;
            auto window = CreateWindow(settings);
#if !defined(MPHREAD_QT)
            auto* const native = static_cast<GLFWwindow*>(window->NativeHandle());
#endif

            SwapchainDesc desc{};
            desc.width = 1280;
            desc.height = 720;
            desc.presentMode = PresentMode::Fifo;
            std::unique_ptr<Swapchain> swapchain = std::make_unique<VulkanSwapchain>(*window, desc, !forceFallback);
            auto& vkSwapchain = dynamic_cast<VulkanSwapchain&>(*swapchain);

            std::uint64_t nativeFrames = 0;
            int drawStep = 0;
            const auto drawColor = [&vkSwapchain, &swapchain, &window, &drawStep, reflexCheck, &nativeFrames](float r, float g, float b)
            {
                ++drawStep;
                // Present more frames than there are swapchain images so a
                // replacement chain must reacquire an already presented image.
                // Four frames did not exercise retirement on four-image Mesa
                // chains, where each acquisition selected a fresh image.
                const std::uint32_t frames = std::max(4U, swapchain->Desc().imageCount + 1U);
                for (std::uint32_t i = 0; i < frames; ++i)
                {
                    const auto sleepsBefore = swapchain->LowLatencyStats().sleepCalls;
                    if (reflexCheck)
                    {
                        while (!swapchain->BeginLowLatencyFrame()) {}
                        ProcessEvents(); // First input collection follows native sleep.
                        swapchain->MarkLowLatency(LowLatencyMarker::InputSample);
                        swapchain->MarkLowLatency(LowLatencyMarker::SimulationStart);
                        swapchain->MarkLowLatency(LowLatencyMarker::SimulationEnd);
                    }
                    AcquireResult acquired{};
                    const auto acquireDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
                    for (;;)
                    {
                        acquired = swapchain->TryAcquireTexture();
                        if (acquired.texture) break;
                        if (acquired.status != PresentationStatus::TemporarilyUnavailable)
                            throw std::runtime_error("Typed Vulkan acquisition reported a loss at step "
                                + std::to_string(drawStep) + " (status " + std::to_string(static_cast<int>(acquired.status)) + ").");
                        if (std::chrono::steady_clock::now() >= acquireDeadline)
                            throw std::runtime_error("Timed out waiting for the Vulkan drawable at step " + std::to_string(drawStep)
                                + " (window state " + std::to_string(static_cast<int>(window->WindowState())) + ").");
                        ProcessEvents();
                        std::this_thread::sleep_for(std::chrono::milliseconds(8));
                    }
                    if (reflexCheck && swapchain->LowLatencyStats().sleepCalls > sleepsBefore) ++nativeFrames;
                    vkSwapchain.ClearCurrent(r, g, b, 1.0F);
                    const auto presented = swapchain->TryPresent();
                    if (presented.status != PresentationStatus::Ready
                        && presented.status != PresentationStatus::ResizeRequired)
                        throw std::runtime_error("Typed Vulkan presentation failed.");
                    ProcessEvents();
                    std::this_thread::sleep_for(std::chrono::milliseconds(8));
                }
            };
            const auto pumpFramebuffer = [](Window& target, int timeoutMs,
                const std::function<bool(int, int)>& ready)
            {
                const auto deadline = std::chrono::steady_clock::now()
                    + std::chrono::milliseconds(timeoutMs);
                int width = 0, height = 0;
                do
                {
                    ProcessEvents();
                    const auto size = target.Size();
                    width = size.X;
                    height = size.Y;
                    if (ready(width, height)) return std::pair<int, int>{width, height};
                    std::this_thread::sleep_for(std::chrono::milliseconds(8));
                } while (std::chrono::steady_clock::now() < deadline);
                throw std::runtime_error("Timed out waiting for a Vulkan window framebuffer transition.");
            };

            if (reflexCheck)
            {
                for (auto present : {PresentMode::Fifo, PresentMode::Immediate, PresentMode::Mailbox})
                {
                    swapchain->SetPresentMode(present);
                    for (int cap : {0, 60, 144, 240, -1})
                    {
                        const auto interval = cap > 0 ? static_cast<std::uint32_t>((1'000'000 + cap - 1) / cap) : 0;
                        for (auto mode : {LowLatencyMode::Off, LowLatencyMode::On, LowLatencyMode::OnBoost, LowLatencyMode::Off})
                        {
                            swapchain->ConfigureLowLatency(mode, interval);
                            const auto before = swapchain->LowLatencyStats();
                            const auto generation = before.swapchainGeneration;
                            drawColor(0.1F, 0.3F, 0.6F);
                            const auto after = swapchain->LowLatencyStats();
                            if (!std::getenv("FRUITY_REFLEX_TEST_FAILURE"))
                            {
                                const auto frames = after.completedMeasurementFrames - before.completedMeasurementFrames;
                                if (!frames || after.markerCalls - before.markerCalls != frames * 7
                                    || (mode == LowLatencyMode::Off && after.sleepCalls != before.sleepCalls)
                                    || (mode != LowLatencyMode::Off && after.sleepCalls - before.sleepCalls != frames))
                                    throw std::runtime_error("Reflex Off/On/Boost measurement or sleep contract failed.");
                            }
                            const auto state = ResolveLowLatency(mode, swapchain->LowLatencyCaps());
                            if (mode != LowLatencyMode::Off && !std::getenv("FRUITY_REFLEX_TEST_FAILURE")
                                && (state.provider != LowLatencyProvider::Nvidia || state.authority != PacingAuthority::Native || state.effective != mode || !state.fallbackReason.empty()))
                                throw std::runtime_error("Reflex check requires usable NVIDIA native pacing.");
                            if (mode == LowLatencyMode::OnBoost && std::getenv("FRUITY_REFLEX_TEST_FAILURE")
                                && (state.provider != LowLatencyProvider::Generic || state.effective != LowLatencyMode::On || state.fallbackReason.empty()))
                                throw std::runtime_error("Injected optional failure did not preserve requested Boost in Generic fallback.");
                            if (generation != swapchain->LowLatencyStats().swapchainGeneration)
                                throw std::runtime_error("Low-latency toggle recreated the swapchain.");
                            std::cout << "[reflexcheck] PASS present=" << static_cast<int>(present) << " cap=" << cap
                                << " requested=" << static_cast<int>(mode) << " effective=" << static_cast<int>(state.effective)
                                << " provider=" << static_cast<int>(state.provider) << " boost=" << state.boostSupported
                                << " authority=" << static_cast<int>(state.authority) << " reason=" << state.fallbackReason << '\n';
                        }
                    }
                }
                swapchain->ConfigureLowLatency(LowLatencyMode::OnBoost, 0);
            }
            drawColor(0.10F, 0.35F, 0.80F);
            const auto initialExtent = swapchain->Desc();
            window->ClientSize(::OpenTK::Mathematics::Vector2i(960, 600));
            const auto resized = pumpFramebuffer(*window, 3000,
                [initialExtent](int width, int height)
                {
                    return width > 0 && height > 0
                        && (static_cast<std::uint32_t>(width) != initialExtent.width
                            || static_cast<std::uint32_t>(height) != initialExtent.height);
                });
            swapchain->Resize(static_cast<std::uint32_t>(resized.first), static_cast<std::uint32_t>(resized.second));
            drawColor(0.15F, 0.65F, 0.25F);

#if defined(MPHREAD_QT)
            // Qt's NativeHandle is the platform window id, not a GLFWwindow.
            // Exercise the same borderless-fullscreen geometry path the Qt game
            // uses instead of feeding that native id to GLFW diagnostics.
            const auto oldBorder = window->WindowBorder();
            const auto oldLocation = window->Location();
            const auto oldSize = window->ClientSize();
            const auto oldFramebuffer = window->Size();
            const auto monitor = window->CurrentMonitorClientArea();
            if (monitor.Size.X <= 0 || monitor.Size.Y <= 0)
                throw std::runtime_error("The Vulkan presentation check requires a desktop monitor.");
            window->WindowStateNormal();
            window->WindowBorder(static_cast<std::int32_t>(WindowBorderValue::Hidden));
            ProcessEvents();
            window->Location(monitor.Min);
            const auto currentClient = window->ClientSize();
            const auto currentFramebuffer = window->Size();
            const auto logicalWidth = currentFramebuffer.X > 0
                ? std::max(1, monitor.Size.X * currentClient.X / currentFramebuffer.X)
                : monitor.Size.X;
            const auto logicalHeight = currentFramebuffer.Y > 0
                ? std::max(1, monitor.Size.Y * currentClient.Y / currentFramebuffer.Y)
                : monitor.Size.Y;
            window->ClientSize({logicalWidth, logicalHeight});
            const auto fullscreen = pumpFramebuffer(*window, 3000,
                [monitor](int width, int height)
                {
                    return width == monitor.Size.X && height == monitor.Size.Y;
                });
            swapchain->Resize(static_cast<std::uint32_t>(fullscreen.first), static_cast<std::uint32_t>(fullscreen.second));
            drawColor(0.75F, 0.22F, 0.08F);
#if defined(_WIN32)
            if (std::getenv("FRUITY_FOCUSCHECK") && vkSwapchain.ExclusiveSupported())
            {
                if (!window->WindowStateFullscreen())
                    throw std::runtime_error("Focus check could not enter exclusive fullscreen.");
                window->Focus();
                ProcessEvents();
                FullscreenExclusive::Active(true);
                drawColor(0.75F, 0.22F, 0.08F);
                const auto generation = swapchain->LowLatencyStats().swapchainGeneration;
                for (int cycle = 0; cycle < 3; ++cycle)
                {
                    FullscreenExclusive::Active(false);
                    const auto start = std::chrono::steady_clock::now();
                    if (!swapchain->BeginLowLatencyFrame()
                        || swapchain->TryAcquireTexture().status != PresentationStatus::TemporarilyUnavailable
                        || swapchain->TryPresent().status != PresentationStatus::TemporarilyUnavailable
                        || std::chrono::steady_clock::now() - start > std::chrono::milliseconds(250))
                        throw std::runtime_error("Unfocused exclusive presentation blocked or remained available.");
                    FullscreenExclusive::Active(true);
                    drawColor(0.75F, 0.22F, 0.08F);
                    if (generation != swapchain->LowLatencyStats().swapchainGeneration)
                        throw std::runtime_error("Focus transition recreated the exclusive swapchain.");
                }
                window->WindowStateNormal();
                std::cout << "[vulkan] exclusive focus PASS; unavailable without blocking; restored; same swapchain\n";
            }
#endif
            window->WindowBorder(oldBorder);
            ProcessEvents();
            window->ClientSize(oldSize);
            window->Location(oldLocation);
            const auto windowed = pumpFramebuffer(*window, 3000,
                [oldFramebuffer](int width, int height)
                {
                    return width == oldFramebuffer.X && height == oldFramebuffer.Y;
                });
            swapchain->Resize(static_cast<std::uint32_t>(windowed.first), static_cast<std::uint32_t>(windowed.second));
            drawColor(0.65F, 0.55F, 0.12F);
#else
            GLFWmonitor* const monitor = ::glfwGetPrimaryMonitor();
            const GLFWvidmode* const mode = monitor ? ::glfwGetVideoMode(monitor) : nullptr;
            if (!monitor || !mode)
                throw std::runtime_error("The Vulkan presentation check requires a desktop monitor.");
            int oldX = 0, oldY = 0;
            ::glfwGetWindowPos(native, &oldX, &oldY);
            const auto oldSize = window->ClientSize();
            int oldFramebufferWidth = 0, oldFramebufferHeight = 0;
            ::glfwGetFramebufferSize(native, &oldFramebufferWidth, &oldFramebufferHeight);
            ::glfwSetWindowMonitor(native, monitor, 0, 0,
                mode->width, mode->height, mode->refreshRate);
            const auto fullscreen = pumpFramebuffer(*window, 3000,
                [mode](int width, int height)
                {
                    return width == mode->width && height == mode->height;
                });
            if (::glfwGetWindowMonitor(native) != monitor)
                throw std::runtime_error("The GLFW window did not enter fullscreen mode.");
            swapchain->Resize(static_cast<std::uint32_t>(fullscreen.first), static_cast<std::uint32_t>(fullscreen.second));
            drawColor(0.75F, 0.22F, 0.08F);
            ::glfwSetWindowMonitor(native, nullptr, oldX, oldY,
                oldSize.X, oldSize.Y, GLFW_DONT_CARE);
            const auto windowed = pumpFramebuffer(*window, 3000,
                [oldFramebufferWidth, oldFramebufferHeight](int width, int height)
                {
                    return width == oldFramebufferWidth && height == oldFramebufferHeight;
                });
            if (::glfwGetWindowMonitor(native) != nullptr)
                throw std::runtime_error("The GLFW window did not return to windowed mode.");
            swapchain->Resize(static_cast<std::uint32_t>(windowed.first), static_cast<std::uint32_t>(windowed.second));
            drawColor(0.65F, 0.55F, 0.12F);
#endif

            window->WindowStateMinimized();
            const auto minimizeDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (window->WindowState() != WindowStateValue::Minimized
                && std::chrono::steady_clock::now() < minimizeDeadline)
            {
                ProcessEvents();
                std::this_thread::sleep_for(std::chrono::milliseconds(8));
            }
            if (window->WindowState() != WindowStateValue::Minimized)
                throw std::runtime_error("The Vulkan window did not enter minimized state.");
            swapchain->Resize(0, 0);
            if (swapchain->Desc().width != 0 || swapchain->Desc().height != 0)
                throw std::runtime_error("A minimized Vulkan swapchain did not suspend its zero-sized extent.");
            // Unavailability follows exposure, and on macOS a minimising
            // window stays exposed while the Dock animates it away: present
            // what is still offered until the surface actually goes.
            const auto unexposedDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            std::uint64_t abandonedReflexId = 0;
            for (;;)
            {
                if (reflexCheck)
                {
                    while (!swapchain->BeginLowLatencyFrame()) {}
                    swapchain->MarkLowLatency(LowLatencyMarker::InputSample);
                    swapchain->MarkLowLatency(LowLatencyMarker::SimulationStart);
                    swapchain->MarkLowLatency(LowLatencyMarker::SimulationEnd);
                }
                const auto admittedId = vkSwapchain.LowLatencyFrameId();
                const auto beforeAcquire = swapchain->LowLatencyStats();
                const auto minimized = swapchain->TryAcquireTexture();
                if (!minimized.texture)
                {
                    if (reflexCheck && admittedId)
                    {
                        const auto afterAcquire = swapchain->LowLatencyStats();
                        if (vkSwapchain.LowLatencyFrameId()
                            || afterAcquire.markerCalls != beforeAcquire.markerCalls
                            || afterAcquire.completedMeasurementFrames != beforeAcquire.completedMeasurementFrames
                            || afterAcquire.abandonedMeasurementFrames != beforeAcquire.abandonedMeasurementFrames + 1)
                            throw std::runtime_error("Unavailable acquire retained an admitted Reflex frame or fabricated present markers.");
                        abandonedReflexId = admittedId;
                    }
                    if (minimized.status != PresentationStatus::TemporarilyUnavailable
                        || swapchain->TryPresent().status != PresentationStatus::TemporarilyUnavailable)
                        throw std::runtime_error("A minimized Vulkan swapchain did not report temporary unavailability.");
                    break;
                }
                vkSwapchain.ClearCurrent(0.0F, 0.0F, 0.0F, 1.0F);
                (void)swapchain->TryPresent();
                if (std::chrono::steady_clock::now() >= unexposedDeadline)
                    throw std::runtime_error("A minimized Vulkan window was still presentable after 3 s.");
                ProcessEvents();
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
            }
            window->WindowStateNormal();
            const auto restored = pumpFramebuffer(*window, 3000,
                [](int width, int height) { return width > 0 && height > 0; });
            swapchain->Resize(static_cast<std::uint32_t>(restored.first), static_cast<std::uint32_t>(restored.second));
            // Qt reports a window state on request, before the platform has
            // acted. On macOS a restore asked for while the Dock is still
            // animating the minimise is dropped, and the minimise then
            // completes: the window ends minimised for real. So keep asking
            // while it falls back, and call it restored only once frames
            // have presented for a sustained run -- TemporarilyUnavailable
            // is the one answer allowed until then.
            const auto presentableDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
            for (int presentedRun = 0; presentedRun < 20;)
            {
                ProcessEvents();
                if (window->WindowState() == WindowStateValue::Minimized)
                {
                    presentedRun = 0;
                    window->WindowStateNormal();
                }
                const auto attempt = swapchain->TryAcquireTexture();
                if (attempt.texture)
                {
                    vkSwapchain.ClearCurrent(0.20F, 0.55F, 0.75F, 1.0F);
                    const auto presented = swapchain->TryPresent();
                    if (presented.status != PresentationStatus::Ready
                        && presented.status != PresentationStatus::ResizeRequired)
                        throw std::runtime_error("A restored Vulkan swapchain did not present.");
                    ++presentedRun;
                }
                else if (attempt.status != PresentationStatus::TemporarilyUnavailable)
                    throw std::runtime_error("A restored Vulkan swapchain failed to acquire.");
                else presentedRun = 0;
                if (std::chrono::steady_clock::now() >= presentableDeadline)
                    throw std::runtime_error("A restored Vulkan swapchain never became presentable.");
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
            }
            drawColor(0.20F, 0.55F, 0.75F);
            if (abandonedReflexId && swapchain->LowLatencyStats().frameId <= abandonedReflexId)
                throw std::runtime_error("Restoring reused the abandoned Reflex frame identity.");

            swapchain->SetPresentMode(PresentMode::Mailbox);
            if (swapchain->RequestedPresentMode() != PresentMode::Mailbox
                || swapchain->Desc().presentMode != (swapchain->PresentationCaps().mailbox
                    ? PresentMode::Mailbox : PresentMode::Fifo))
                throw std::runtime_error("Vulkan requested/resolved presentation policy mismatch.");
            drawColor(0.45F, 0.20F, 0.70F);
            swapchain->SetPresentMode(PresentMode::Fifo);
            drawColor(0.12F, 0.32F, 0.72F);

            const bool validation = vkSwapchain.ValidationEnabled();
            if (reflexCheck)
            {
                const auto stats = swapchain->LowLatencyStats();
                if (!std::getenv("FRUITY_REFLEX_TEST_FAILURE") && (!stats.sleepCalls || !stats.markerCalls || !validation))
                    throw std::runtime_error("Native sleep/markers/validation were not exercised.");
                std::cout << "[reflexcheck] native frames=" << nativeFrames << " sleeps=" << stats.sleepCalls
                    << " waits=" << stats.waitCalls << " modes=" << stats.modeCalls << " markers=" << stats.markerCalls
                    << " timing-reports=" << stats.timingReports << " generations=" << stats.swapchainGeneration << '\n';
                std::cout << "[reflexcheck] completed=" << stats.completedMeasurementFrames
                    << " abandoned=" << stats.abandonedMeasurementFrames << " timing-queries=" << stats.timingQueries << '\n';
            }
            if (forceFallback && (vkSwapchain.PresentFencesEnabled()
                || vkSwapchain.FallbackRetiredReleases() == 0 || vkSwapchain.FallbackCompletedProofs() == 0))
                throw std::runtime_error("The fallback diagnostic did not prove deferred swapchain retirement.");
            vkSwapchain.Close();
            const unsigned errors = vkSwapchain.ValidationErrors();
            if (vkSwapchain.PresentFencesEnabled() && vkSwapchain.PresentFenceWaits() == 0)
                throw std::runtime_error("Enabled present fences were never waited by the presentation check.");
            if (errors != 0)
                throw std::runtime_error("Vulkan validation reported " + std::to_string(errors) + " error(s).");
            std::cout << "[vulkan] presentation PASS; clear present; resize; fullscreen; minimize/restore; "
                << "clean shutdown; validation=" << validation << "; errors=0; present-fence-waits="
                << vkSwapchain.PresentFenceWaits() << '\n';
            std::cout << "[vulkan] fallback-retired-releases=" << vkSwapchain.FallbackRetiredReleases() << '\n';
            std::cout << "[vulkan] fallback-completed-reacquire-proofs=" << vkSwapchain.FallbackCompletedProofs()
                << "; frame-slot-waits=" << vkSwapchain.FrameFenceWaits()
                << "; cleanup-frame-waits=" << vkSwapchain.CleanupFrameFenceWaits() << '\n';
            return 0;
        }
        catch (const std::exception& e)
        {
            std::cerr << "[vulkan] presentation FAIL: " << e.what() << '\n';
            return 1;
        }
    }


}
#else
    int RunPresentationCheck(bool, bool)
    {
        std::cerr << "[vulkan] the presentation check opens a desktop window.\n";
        return 1;
    }
}
#endif
#else
namespace MphRead::NativeRuntime::Rhi::Vulkan
{
    std::unique_ptr<Swapchain> CreateSwapchain(
        ::MphRead::RendererPlatform::Window&, const SwapchainDesc&)
    {
        throw std::runtime_error("Vulkan presentation is unavailable on this platform or build.");
    }
    std::unique_ptr<Swapchain> CreateSwapchain(Context&,
        ::MphRead::RendererPlatform::Window&, const SwapchainDesc&)
    {
        throw std::runtime_error("Vulkan presentation is unavailable on this platform or build.");
    }
    std::unique_ptr<Swapchain> CreateSurfaceSwapchain(Context&, const SwapchainDesc&)
    {
        throw std::runtime_error("Vulkan presentation is unavailable on this platform or build.");
    }
    int RunPresentationCheck(bool, bool)
    {
        std::cerr << "[vulkan] presentation unavailable: desktop Vulkan support was not built.\n";
        return 1;
    }
}
#endif
