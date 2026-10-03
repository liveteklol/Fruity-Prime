#include "VulkanGraphicsDevice.hpp"
#include "VulkanScene.hpp"
#include "../ResourceStatePolicy.hpp"
#include "../BackdropNoise.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <functional>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <map>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <utility>

#if defined(FRUITY_HAS_VULKAN)
#include "VulkanContextInternal.hpp"
#include "VulkanFrameScheduler.hpp"
#include "VulkanFrameSlots.hpp"
#include "VulkanSynchronization.hpp"
#include "VulkanPipelineCache.hpp"
#include "VulkanDescriptorAllocator.hpp"
#include "VulkanUploadArena.hpp"
#include "VulkanCommandSlots.hpp"
#include "VulkanRgbTransfer.hpp"
#include "VulkanMemory.hpp"
#include "VulkanResources.hpp"
#include "../../../Testing/MemoryAdmissionCheck.hpp"
#include "../SceneShaderAbi.hpp"
#include "../../../Mods/Platform/AppPaths.hpp"
#include "FruityVulkanSceneShaders.hpp"
#include <vk_mem_alloc.h>

namespace MphRead::NativeRuntime::Rhi::Vulkan
{
    namespace
    {
        std::uint64_t NextSceneResourceIdentity() noexcept
        {
            static std::atomic<std::uint64_t> next{1};
            return next.fetch_add(1, std::memory_order_relaxed);
        }
        [[noreturn]] void Unsupported(const char* operation)
        {
            throw std::logic_error(std::string("Vulkan RHI operation is scheduled for a later phase: ") + operation);
        }

        [[nodiscard]] bool Has(BufferUsage value, BufferUsage flag) noexcept
        {
            return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(flag)) != 0;
        }

        [[nodiscard]] bool Has(TextureUsage value, TextureUsage flag) noexcept
        {
            return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(flag)) != 0;
        }

        [[nodiscard]] std::uint32_t BytesPerPixel(TextureFormat format)
        {
            switch (format)
            {
            case TextureFormat::R8Unorm: return 1;
            case TextureFormat::RG8Unorm: return 2;
            case TextureFormat::RGB8Unorm: return 3;
            case TextureFormat::RGBA8Unorm:
            case TextureFormat::RGBA8Srgb:
            case TextureFormat::BGRA8Unorm:
            case TextureFormat::BGRA8Srgb:
            case TextureFormat::R32Float:
            case TextureFormat::D24UnormS8Uint:
            case TextureFormat::D32Float: return 4;
            case TextureFormat::R16Float: return 2;
            case TextureFormat::RG16Float: return 4;
            case TextureFormat::RG32Float:
            case TextureFormat::D32FloatS8Uint: return 8;
            case TextureFormat::RGBA16Float: return 8;
            case TextureFormat::RGB32Float: return 12;
            case TextureFormat::RGBA32Float: return 16;
            case TextureFormat::D16Unorm: return 2;
            case TextureFormat::Undefined: break;
            }
            throw std::invalid_argument("Vulkan RHI: unknown bytes-per-pixel value.");
        }

        // Bytes a texel occupies in the image and in its copies.
        [[nodiscard]] std::uint32_t StorageBytesPerPixel(TextureFormat format)
        {
            return IsRgbTextureFormat(format) ? (format == TextureFormat::RGB8Unorm ? 4U : 16U) : BytesPerPixel(format);
        }

        [[nodiscard]] VkBufferImageCopy ToVkBufferImageCopy(
            const BufferDesc& buffer, const TextureDesc& texture, const BufferTextureCopy& region)
        {
            if (texture.sampleCount != 1 || region.mipLevel >= texture.mipLevels
                || region.arrayLayer >= texture.arrayLayers)
                throw std::invalid_argument("Vulkan RHI: invalid image-copy subresource or buffer offset.");
            if (texture.depth > 1 && region.arrayLayer != 0)
                throw std::invalid_argument("Vulkan RHI: a 3D image has no array layers.");
            if ((texture.depth == 1 && (region.z != 0 || region.depth != 1))
                || (texture.depth > 1 && region.depth == 0))
                throw std::invalid_argument("Vulkan RHI: image-copy depth does not match the image type.");

            const auto mipDimension = [level = region.mipLevel](std::uint32_t size)
            {
                return level >= 32 ? 1U : std::max(1U, size >> level);
            };
            const std::uint32_t mipWidth = mipDimension(texture.width);
            const std::uint32_t mipHeight = mipDimension(texture.height);
            const std::uint32_t mipDepth = texture.depth > 1 ? mipDimension(texture.depth) : 1U;
            if (region.width == 0 || region.height == 0 || region.depth == 0
                || region.x > mipWidth || region.width > mipWidth - region.x
                || region.y > mipHeight || region.height > mipHeight - region.y
                || region.z > mipDepth || region.depth > mipDepth - region.z)
                throw std::out_of_range("Vulkan RHI: image-copy region is outside the selected mip.");

            const VkImageAspectFlags available = VulkanResources::AspectMask(texture.format);
            VkImageAspectFlags selected = available == VK_IMAGE_ASPECT_COLOR_BIT
                ? VK_IMAGE_ASPECT_COLOR_BIT : VK_IMAGE_ASPECT_DEPTH_BIT;
            switch (region.aspect)
            {
            case TextureAspect::Automatic: break;
            case TextureAspect::Color: selected = VK_IMAGE_ASPECT_COLOR_BIT; break;
            case TextureAspect::Depth: selected = VK_IMAGE_ASPECT_DEPTH_BIT; break;
            case TextureAspect::Stencil: selected = VK_IMAGE_ASPECT_STENCIL_BIT; break;
            default: throw std::invalid_argument("Vulkan RHI: invalid copy aspect.");
            }
            if ((available & selected) == 0)
                throw std::invalid_argument("Vulkan RHI: copy aspect does not exist in the image.");
            const std::uint64_t pixelSize = selected == VK_IMAGE_ASPECT_STENCIL_BIT ? 1
                : (texture.format == TextureFormat::D32FloatS8Uint ? 4 : StorageBytesPerPixel(texture.format));
            const std::uint64_t offsetAlignment = selected == VK_IMAGE_ASPECT_COLOR_BIT ? pixelSize : 4;
            if (region.bufferOffset % offsetAlignment != 0)
                throw std::invalid_argument("Vulkan RHI: image-copy buffer offset has invalid texel/aspect alignment.");
            const std::uint64_t tightRowBytes = static_cast<std::uint64_t>(region.width) * pixelSize;
            const std::uint64_t rowBytes = region.bytesPerRow == 0 ? tightRowBytes : region.bytesPerRow;
            const std::uint64_t rowsPerImage = region.rowsPerImage == 0
                ? region.height : region.rowsPerImage;
            if (rowBytes < tightRowBytes || rowBytes % pixelSize != 0
                || rowsPerImage < region.height)
                throw std::invalid_argument("Vulkan RHI: image-copy row pitch or height is too small.");

            constexpr std::uint64_t maxSize = std::numeric_limits<std::uint64_t>::max();
            if (rowBytes > maxSize / rowsPerImage)
                throw std::out_of_range("Vulkan RHI: image-copy row pitch overflows.");
            const std::uint64_t imageStride = rowBytes * rowsPerImage;
            if (region.depth - 1 > maxSize / imageStride)
                throw std::out_of_range("Vulkan RHI: image-copy depth stride overflows.");
            std::uint64_t required = static_cast<std::uint64_t>(region.depth - 1) * imageStride;
            const std::uint64_t lastRow = static_cast<std::uint64_t>(region.height - 1) * rowBytes;
            if (required > maxSize - lastRow || required + lastRow > maxSize - tightRowBytes)
                throw std::out_of_range("Vulkan RHI: image-copy byte range overflows.");
            required += lastRow + tightRowBytes;
            if (region.bufferOffset > buffer.size || required > buffer.size - region.bufferOffset)
                throw std::out_of_range("Vulkan RHI: image-copy byte range is outside the buffer.");

            VkBufferImageCopy copy{};
            copy.bufferOffset = region.bufferOffset;
            copy.bufferRowLength = region.bytesPerRow == 0 ? 0
                : static_cast<std::uint32_t>(rowBytes / pixelSize);
            copy.bufferImageHeight = region.rowsPerImage;
            copy.imageSubresource.aspectMask = selected;
            copy.imageSubresource.mipLevel = region.mipLevel;
            copy.imageSubresource.baseArrayLayer = region.arrayLayer;
            copy.imageSubresource.layerCount = 1;
            copy.imageOffset = {static_cast<std::int32_t>(region.x),
                static_cast<std::int32_t>(region.y), static_cast<std::int32_t>(region.z)};
            copy.imageExtent = {region.width, region.height, region.depth};
            return copy;
        }
    }

        class VulkanDeviceState;

        // The device owns native lifetime, even when a public wrapper survives
        // its session. Registrations borrow wrappers and never prolong them.
        class VulkanNativeOwner
        {
        public:
            virtual void CloseNative() noexcept = 0;
        protected:
            ~VulkanNativeOwner() = default;
        };
        class VulkanNativeRegistration final
        {
        public:
            VulkanNativeRegistration(VulkanDeviceState& state, VulkanNativeOwner& owner, bool commands = false);
            ~VulkanNativeRegistration();
            VulkanNativeRegistration(const VulkanNativeRegistration&) = delete;
            VulkanNativeRegistration& operator=(const VulkanNativeRegistration&) = delete;
        private:
            VulkanDeviceState& _state;
            VulkanNativeOwner& _owner;
        };

#include "VulkanResourceWrappersInternal.inc"


        class VulkanDeviceState final
        {
        public:
            explicit VulkanDeviceState(Context& context) : ContextPointer(&context)
            {
                auto& vk = *context._impl;
                Scheduler = std::make_unique<VulkanFrameScheduler>(VulkanFrameScheduler::Dispatch{
                    vk.device, vk.graphics, vk.vkCreateSemaphore, vk.vkDestroySemaphore,
                    vk.vkQueueSubmit2, vk.vkGetSemaphoreCounterValue, Check, vk.vkWaitSemaphores,
                    [&context]() -> std::optional<std::uint64_t> { auto& v = *context._impl;
                        return v.reflex ? v.reflex->SubmissionId() : v.nvLowLatency2 && v.nvLowLatency2Revision >= 3 ? std::optional<std::uint64_t>(0) : std::nullopt; },
                    [&context] { if (auto* r = context._impl->reflex) r->Mark(LowLatencyMarker::RenderSubmitStart); }});
                vk.establishReflexFrame = [this] { (void)Scheduler->MarkExternalWork(false); };
                VmaVulkanFunctions functions{};
                functions.vkGetInstanceProcAddr = Context::Impl::InstanceProc();
                functions.vkGetDeviceProcAddr = vk.vkGetDeviceProcAddr;
                VmaAllocatorCreateInfo create{};
                create.instance = vk.instance;
                create.physicalDevice = vk.physical;
                create.device = vk.device;
                create.vulkanApiVersion = VK_API_VERSION_1_3;
                create.pVulkanFunctions = &functions;
                if (vk.memoryBudget) create.flags |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
                VkPhysicalDeviceMaintenance3Properties maintenance{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES};
                VkPhysicalDeviceMaintenance4Properties maintenance4{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_PROPERTIES};
                maintenance.pNext = &maintenance4;
                VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
                properties.pNext = &maintenance;
                vk.vkGetPhysicalDeviceProperties2(vk.physical, &properties);
                Memory = std::make_unique<VulkanMemory>(create, VulkanMemory::Dispatch{
                    vk.vkGetDeviceBufferMemoryRequirements, vk.vkGetDeviceImageMemoryRequirements,
                    vk.vkGetPhysicalDeviceMemoryProperties2, Check}, VulkanMemory::Limits{
                    maintenance.maxMemoryAllocationSize, properties.properties.limits.maxMemoryAllocationCount,
                    maintenance4.maxBufferSize, vk.memoryBudget});
                Allocator = Memory->Allocator(); // Borrowed by transfer and retirement callbacks.
                Resources = std::make_unique<VulkanResources>(VulkanResources::Dispatch{
                    vk.physical, vk.device, vk.vkGetPhysicalDeviceImageFormatProperties, vk.vkGetPhysicalDeviceFormatProperties,
                    vk.vkCreateImageView, vk.vkDestroyImageView, vk.vkCreateSampler, vk.vkDestroySampler, Check,
                    [memory = Memory.get()](const VkBufferCreateInfo& info, const VmaAllocationCreateInfo& allocation) {
                        VulkanResources::BufferStorage result{}; VmaAllocationInfo mapped{};
                        memory->CreateBuffer(info, allocation, result.Buffer, result.Allocation, &mapped);
                        result.Mapped = static_cast<std::byte*>(mapped.pMappedData); return result;
                    },
                    [allocator = Allocator](VulkanResources::BufferStorage storage) {
                        vmaDestroyBuffer(allocator, storage.Buffer, storage.Allocation);
                    },
                    [memory = Memory.get()](const VkImageCreateInfo& info, const VmaAllocationCreateInfo& allocation) {
                        VulkanResources::ImageStorage result{};
                        memory->CreateImage(info, allocation, result.Image, result.Allocation); return result;
                    },
                    [allocator = Allocator](VulkanResources::ImageStorage storage) {
                        vmaDestroyImage(allocator, storage.Image, storage.Allocation);
                    },
                    [context = &vk](VkObjectType type, std::uint64_t handle, const char* label) { context->Name(type, handle, label); }
                }, context.Caps());
                Frames = std::make_unique<VulkanFrameSlots>(VulkanFrameSlots::Dispatch{
                    vk.device, vk.vkCreateFence, vk.vkDestroyFence, vk.vkGetFenceStatus, vk.vkWaitForFences,
                    vk.vkResetFences, vk.vkDestroyCommandPool, vk.vkDestroyPipelineLayout,
                    [this](const VkSubmitInfo2& work, VkFence fence) { return Scheduler->Submit(work, fence); },
                    [this] { return Scheduler->Poll(); }, [this] { return MakeDescriptors({}); },
                    [this] { ++HostWaits; }
                });
                try
                {
                    VkPhysicalDeviceProperties identity{};
                    vk.vkGetPhysicalDeviceProperties(vk.physical, &identity);
                    UniformAlignment = std::max<VkDeviceSize>(16, identity.limits.minUniformBufferOffsetAlignment);
                    std::filesystem::path file;
#if !defined(__ANDROID__)
                    const auto* overrideDirectory = std::getenv("FRUITY_VK_PIPELINE_CACHE_DIR");
                    const auto directory = overrideDirectory && *overrideDirectory ? std::filesystem::u8path(overrideDirectory)
                        : std::filesystem::u8path(Mods::Platform::AppPaths::UserDataDirectory()) / "render-cache";
                    file = directory / "vulkan-pipelines.bin";
#endif
                    PipelineCache = std::make_unique<VulkanPipelineCache>(VulkanPipelineCache::Dispatch{
                        vk.device, vk.vkCreatePipelineCache, vk.vkDestroyPipelineCache,
                        vk.vkGetPipelineCacheData, vk.vkCreateGraphicsPipelines}, identity, std::move(file));
                }
                catch (...) { std::cerr << "[vulkan cache] optional library unavailable; continuing uncached\n"; }
            }

            ~VulkanDeviceState() { CloseNative(); }

            void CloseNative() noexcept
            {
                if (Allocator != VK_NULL_HANDLE)
                {
                    try { FlushScene(); } catch (...) {}
                    try { ContextPointer->WaitIdle(); } catch (...) {}
                    FinalSubmitted = Scheduler->Submitted();
                    try { FinalCompleted = Scheduler->Poll(); } catch (...) {}
                    Closing = true;
                    Readbacks.Close();
                    // Command-owned caches, rings and pools precede images and
                    // buffers. Closing an owner can unregister other owners.
                    for (;;)
                    {
                        auto next = std::find_if(NativeOwners.begin(), NativeOwners.end(),
                            [](const auto& entry) { return entry.second; });
                        if (next == NativeOwners.end()) break;
                        auto* owner = next->first;
                        NativeOwners.erase(next);
                        owner->CloseNative();
                    }
                    while (!NativeOwners.empty())
                    {
                        auto next = NativeOwners.begin();
                        auto* owner = next->first;
                        NativeOwners.erase(next);
                        owner->CloseNative();
                    }
                    ReleaseWindowTarget();
                    Retired.CollectAll([](auto& release) { release(); });
                    auto& vk = *ContextPointer->_impl;
                    Frames->CloseAfterDrain();
                    VmaTotalStatistics statistics{};
                    vmaCalculateStatistics(Allocator, &statistics);
                    OutstandingAllocationsAtShutdown = statistics.total.statistics.allocationCount;
                    Resources->Close();
                    Memory->Close();
                    Allocator = VK_NULL_HANDLE;
                    if (PipelineCache) PipelineCache->Close();
                    vk.establishReflexFrame = {};
                    Scheduler.reset();
                    ContextPointer = nullptr;
                    SceneFlushers.clear(); SceneForgetters.clear(); SceneViewReplacers.clear();
                    RecordingList = nullptr; CurrentSceneProgram = nullptr;
                    TexturesByHandle.clear();
                }
            }

            void RequireAlive() const
            { if (!ContextPointer || Closing) throw std::logic_error("The Vulkan session has ended."); }
            bool Closing = false;
            std::uint32_t OutstandingAllocationsAtShutdown = 0;
            SubmissionSerial FinalSubmitted{}, FinalCompleted{};
            std::unordered_map<VulkanNativeOwner*, bool> NativeOwners;
            std::shared_ptr<TimestampBudget> TimestampCapacity = std::make_shared<TimestampBudget>();

            [[nodiscard]] FrameContext BeginDescriptorFrame()
            {
                const auto frame = Frames->Begin();
                CollectRetired();
                return frame;
            }

            void EndDescriptorFrame()
            {
                Frames->End();
            }

            [[nodiscard]] std::unique_ptr<VulkanDescriptorAllocator> MakeDescriptors(
                VulkanDescriptorAllocator::Capacity capacity)
            {
                auto& vk = *ContextPointer->_impl;
                return std::make_unique<VulkanDescriptorAllocator>(VulkanDescriptorAllocator::Dispatch{
                    vk.device, vk.vkCreateDescriptorPool, vk.vkDestroyDescriptorPool,
                    vk.vkResetDescriptorPool, vk.vkAllocateDescriptorSets, Check}, capacity);
            }

            [[nodiscard]] VkDescriptorSet AllocateDescriptors(VkDescriptorSetLayout layout,
                const BindingLayoutDesc& desc)
            {
                return Frames->Allocate(layout, desc);
            }

            Context* ContextPointer = nullptr;
            std::unique_ptr<VulkanFrameSlots> Frames;
            VmaAllocator Allocator = VK_NULL_HANDLE;
            std::unique_ptr<VulkanMemory> Memory;
            std::unique_ptr<VulkanResources> Resources;
            std::atomic<std::uint32_t> Buffers{0};
            std::atomic<std::uint32_t> Textures{0};
            std::atomic<std::uint32_t> Shaders{0};
            std::atomic<std::uint32_t> Programs{0};
            std::atomic<std::uint32_t> Samplers{0};
            std::atomic<std::uint32_t> UploadPages{0};
            // GPU-only transfer scratch pages, kept by command slots for reuse:
            // internal, like upload pages, so not a resource a caller made.
            std::atomic<std::uint32_t> ScratchPages{0};
            std::atomic<std::uint64_t> UploadPageCreations{0};
            std::atomic<std::uint32_t> UploadCommandLists{0};
            std::atomic<std::uint64_t> HostWaits{0};
            std::atomic<std::uint64_t> DeviceWideWaits{0};
            std::unique_ptr<VulkanFrameScheduler> Scheduler;
            ReadbackQueue Readbacks;
            std::unique_ptr<VulkanPipelineCache> PipelineCache;
            VkDeviceSize UniformAlignment = 16;
            RetirementQueue<std::function<void()>> Retired;

            VkResult ImageFormatProperties(VkFormat format, VkImageUsageFlags usage, VkImageFormatProperties& properties) const
            {
                auto& vk = *ContextPointer->_impl;
                return vk.vkGetPhysicalDeviceImageFormatProperties(vk.physical, format,
                    VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, usage, 0, &properties);
            }

            void Retire(std::function<void()> release)
            {
                // Callers flush recorded uses before logical destruction.
                // Closures hold native values only: no cycle back to this state.
                if (Closing) release();
                else Retired.Retire(std::move(release), Scheduler->Submitted());
            }

            void CollectRetired()
            {
                Retired.Collect(Scheduler->Poll(), [](auto& release) { release(); });
            }

            std::unordered_map<const void*, std::function<void(VkImageView, VkImageView)>> SceneViewReplacers;
            void ReplaceView(VkImageView before, VkImageView after)
            {
                for (const auto& [owner, replace] : SceneViewReplacers) replace(before, after);
            }
            // The one list holding unsubmitted work (BeginBuffer flushes any
            // other first), which an upload is recorded into in place.
            void* RecordingList = nullptr; // VulkanCommandList*
            std::mutex TextureMutex{};
            std::unordered_map<std::int32_t, VulkanTexture*> TexturesByHandle{};
            // Above every name the OpenGL side chooses (UiOverlay 1e6, thumbnails
            // 1.1e6, GlNames 2e6+): a scene switched to OpenGL recreates its
            // textures under these same handles, and a low one could be a
            // name Skia takes from glGenTextures there.
            std::int32_t NextTextureHandle = 3'000'000;

            // The window's own colour and depth, which every command list on
            // this device draws into as OpenGL draws into the default
            // framebuffer. Created by the first list that renders to it.
            std::unique_ptr<VulkanTexture> WindowColor;
            std::unique_ptr<VulkanTexture> WindowDepth;
            std::unique_ptr<VulkanTextureView> WindowColorView;
            std::unique_ptr<VulkanTextureView> WindowDepthView;
            // Lists alive on this device: the window target goes with the
            // last one, as OpenGL's default framebuffer goes with nothing.
            std::uint32_t CommandLists = 0;
            void ReleaseWindowTarget() noexcept
            {
                WindowColorView.reset();
                WindowDepthView.reset();
                WindowColor.reset();
                WindowDepth.reset();
            }

            // The scene renderer's context state, which OpenGL keeps per
            // context: the program constants go to, and the current vertex
            // attributes a draw inherits where its mesh has no array.
            void* CurrentSceneProgram = nullptr;
            std::array<float, 4> CurrentColor{1.0F, 1.0F, 1.0F, 1.0F};
            std::array<float, 3> CurrentNormal{0.0F, 0.0F, 1.0F};
            std::array<float, 3> CurrentTexCoord{0.0F, 0.0F, 0.0F};

            // Command lists holding recorded, unsubmitted scene work. Anything
            // that submits on its own, or destroys a resource, flushes them
            // first, so the queue sees work in the order it was recorded.
            std::unordered_map<const void*, std::function<void()>> SceneFlushers{};
            // Told when a texture, program or pipeline a list may still name goes away.
            std::unordered_map<const void*, std::function<void(const void*)>> SceneForgetters{};
            void ForgetScene(const void* object)
            {
                FlushScene();
                std::vector<std::function<void(const void*)>> listeners;
                for (const auto& [owner, forget] : SceneForgetters) listeners.push_back(forget);
                for (const auto& forget : listeners) forget(object);
            }
            bool Flushing = false;
            void FlushScene(const void* except = nullptr)
            {
                if (Closing || !ContextPointer || Flushing || SceneFlushers.empty()) return;
                Flushing = true;
                try
                {
                    // A flush unregisters its list: iterate over a copy.
                    std::vector<std::function<void()>> pending;
                    for (const auto& [owner, flush] : SceneFlushers)
                        if (owner != except) pending.push_back(flush);
                    for (const auto& flush : pending) flush();
                }
                catch (...)
                {
                    Flushing = false;
                    throw;
                }
                Flushing = false;
            }
        };

        VulkanNativeRegistration::VulkanNativeRegistration(VulkanDeviceState& state,
            VulkanNativeOwner& owner, bool commands) : _state(state), _owner(owner)
        { state.RequireAlive(); state.NativeOwners.emplace(&owner, commands); }
        VulkanNativeRegistration::~VulkanNativeRegistration() { _state.NativeOwners.erase(&_owner); }
        #include "VulkanGpuDiagnosticsInternal.inc"

        template <typename Native, typename Resource>
        decltype(auto) CheckedResource(Resource& resource, const std::shared_ptr<VulkanDeviceState>& device)
        {
            device->RequireAlive();
            using Checked = std::conditional_t<std::is_const_v<Resource>, const Native, Native>;
            auto* native = dynamic_cast<Checked*>(&resource);
            if (!native || native->DeviceState() != device)
                throw std::invalid_argument("Vulkan RHI: resource belongs to another backend or session.");
            if constexpr (std::is_same_v<Native, VulkanTextureView>)
                if (!native->TextureAlive()) throw std::invalid_argument("Vulkan RHI: texture view's texture has ended.");
            return *native;
        }

        class VulkanShader final : public Shader, public VulkanNativeOwner
        {
        public:
            // The scene program this module belongs to, when a scene shader
            // set made it (VulkanSceneProgram*).
            void* SceneProgram = nullptr;
            VulkanShader(std::shared_ptr<VulkanDeviceState> device, const ShaderDesc& desc)
                : _device(std::move(device)), _registration(*_device, *this), _desc(desc)
            {
                if (desc.format != ShaderCodeFormat::SpirV)
                    throw std::invalid_argument("Vulkan RHI: expected SPIR-V shader code.");
                if (desc.stage != ShaderStage::Vertex && desc.stage != ShaderStage::Fragment
                    && desc.stage != ShaderStage::Compute)
                    throw std::invalid_argument("Vulkan RHI: shader needs one supported stage.");
                if (desc.entryPoint.empty() || desc.entryPoint.find('\0') != std::string::npos
                    || desc.code.size() < 20 || desc.code.size() % 4 != 0)
                    throw std::invalid_argument("Vulkan RHI: invalid SPIR-V shader description.");
                std::vector<std::uint32_t> words(desc.code.size() / 4);
                std::memcpy(words.data(), desc.code.data(), desc.code.size());
                if (words[0] != 0x07230203 || words[4] != 0)
                    throw std::invalid_argument("Vulkan RHI: invalid SPIR-V header.");
                const std::uint32_t model = desc.stage == ShaderStage::Vertex ? 0
                    : desc.stage == ShaderStage::Fragment ? 4 : 5;
                bool found = false;
                for (std::size_t index = 5; index < words.size();)
                {
                    const auto count = words[index] >> 16;
                    const auto opcode = words[index] & 0xFFFF;
                    if (count == 0 || count > words.size() - index)
                        throw std::invalid_argument("Vulkan RHI: malformed SPIR-V instruction.");
                    if (opcode == 15 && count >= 4) // OpEntryPoint
                    {
                        const char* name = reinterpret_cast<const char*>(&words[index + 3]);
                        const auto available = (count - 3) * sizeof(std::uint32_t);
                        const char* end = static_cast<const char*>(std::memchr(name, 0, available));
                        if (!end) throw std::invalid_argument("Vulkan RHI: unterminated SPIR-V entry point.");
                        if (words[index + 1] == model && std::string_view(name, end - name) == desc.entryPoint)
                            found = true;
                    }
                    index += count;
                }
                if (!found) throw std::invalid_argument("Vulkan RHI: shader entry point/stage not found.");
                auto& vk = *_device->ContextPointer->_impl;
                VkShaderModuleCreateInfo create{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
                create.codeSize = desc.code.size();
                create.pCode = words.data();
                Check(vk.vkCreateShaderModule(vk.device, &create, nullptr, &_module), "vkCreateShaderModule");
                ++_device->Shaders;
            }
            ~VulkanShader() override { CloseNative(); }
            void CloseNative() noexcept override
            {
                if (_module)
                {
                    auto& vk = *_device->ContextPointer->_impl;
                    vk.vkDestroyShaderModule(vk.device, _module, nullptr);
                    _module = VK_NULL_HANDLE;
                    --_device->Shaders;
                }
            }
            [[nodiscard]] const ShaderDesc& Desc() const noexcept override { return _desc; }
            [[nodiscard]] VkShaderModule Native() const noexcept { return _module; }
            [[nodiscard]] const std::shared_ptr<VulkanDeviceState>& DeviceState() const noexcept { return _device; }
        private:
            std::shared_ptr<VulkanDeviceState> _device;
            VulkanNativeRegistration _registration;
            ShaderDesc _desc;
            VkShaderModule _module = VK_NULL_HANDLE;
        };

        [[nodiscard]] VkFormat ToVkVertexFormat(VertexFormat format)
        {
            switch (format)
            {
            case VertexFormat::Float: return VK_FORMAT_R32_SFLOAT;
            case VertexFormat::Float2: return VK_FORMAT_R32G32_SFLOAT;
            case VertexFormat::Float3: return VK_FORMAT_R32G32B32_SFLOAT;
            case VertexFormat::Float4: return VK_FORMAT_R32G32B32A32_SFLOAT;
            case VertexFormat::UByte4Norm: return VK_FORMAT_R8G8B8A8_UNORM;
            case VertexFormat::Short2Norm: return VK_FORMAT_R16G16_SNORM;
            case VertexFormat::Short4Norm: return VK_FORMAT_R16G16B16A16_SNORM;
            case VertexFormat::UInt: return VK_FORMAT_R32_UINT;
            }
            throw std::invalid_argument("Vulkan RHI: invalid vertex format.");
        }

        [[nodiscard]] VkPrimitiveTopology ToVkTopology(PrimitiveTopology topology)
        {
            switch (topology)
            {
            case PrimitiveTopology::PointList: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
            case PrimitiveTopology::LineList: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
            case PrimitiveTopology::LineStrip: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
            case PrimitiveTopology::TriangleList: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            case PrimitiveTopology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
            }
            throw std::invalid_argument("Vulkan RHI: invalid primitive topology.");
        }

        [[nodiscard]] VkBlendFactor ToVkBlendFactor(BlendFactor factor)
        {
            switch (factor)
            {
            case BlendFactor::Zero: return VK_BLEND_FACTOR_ZERO;
            case BlendFactor::One: return VK_BLEND_FACTOR_ONE;
            case BlendFactor::SrcColor: return VK_BLEND_FACTOR_SRC_COLOR;
            case BlendFactor::OneMinusSrcColor: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
            case BlendFactor::DstColor: return VK_BLEND_FACTOR_DST_COLOR;
            case BlendFactor::OneMinusDstColor: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
            case BlendFactor::SrcAlpha: return VK_BLEND_FACTOR_SRC_ALPHA;
            case BlendFactor::OneMinusSrcAlpha: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            case BlendFactor::DstAlpha: return VK_BLEND_FACTOR_DST_ALPHA;
            case BlendFactor::OneMinusDstAlpha: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
            case BlendFactor::ConstantColor: return VK_BLEND_FACTOR_CONSTANT_COLOR;
            case BlendFactor::OneMinusConstantColor: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
            case BlendFactor::ConstantAlpha: return VK_BLEND_FACTOR_CONSTANT_ALPHA;
            case BlendFactor::OneMinusConstantAlpha: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
            }
            throw std::invalid_argument("Vulkan RHI: invalid blend factor.");
        }

        [[nodiscard]] VkDescriptorType ToVkDescriptorType(BindingType type)
        {
            switch (type)
            {
            case BindingType::UniformBuffer: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            case BindingType::StorageBuffer: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            case BindingType::SampledTexture: return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            case BindingType::StorageTexture: return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            case BindingType::Sampler: return VK_DESCRIPTOR_TYPE_SAMPLER;
            }
            throw std::invalid_argument("Vulkan RHI: unknown binding type.");
        }

        [[nodiscard]] VkShaderStageFlags ToVkShaderStages(ShaderStage stages)
        {
            const auto bits = static_cast<std::uint32_t>(stages);
            if (bits == 0 || (bits & ~7U) != 0)
                throw std::invalid_argument("Vulkan RHI: invalid binding shader stages.");
            VkShaderStageFlags result = 0;
            if (bits & static_cast<std::uint32_t>(ShaderStage::Vertex)) result |= VK_SHADER_STAGE_VERTEX_BIT;
            if (bits & static_cast<std::uint32_t>(ShaderStage::Fragment)) result |= VK_SHADER_STAGE_FRAGMENT_BIT;
            if (bits & static_cast<std::uint32_t>(ShaderStage::Compute)) result |= VK_SHADER_STAGE_COMPUTE_BIT;
            return result;
        }

        class VulkanBindingLayout final : public BindingLayout, public VulkanNativeOwner
        {
        public:
            VulkanBindingLayout(std::shared_ptr<VulkanDeviceState> state, const BindingLayoutDesc& desc)
                : _device(std::move(state)), _registration(*_device, *this), _desc(desc)
            {
                std::vector<VkDescriptorSetLayoutBinding> bindings;
                bindings.reserve(desc.entries.size());
                for (const auto& entry : desc.entries)
                {
                    if (entry.count == 0)
                        throw std::invalid_argument("Vulkan RHI: binding array count must be nonzero.");
                    if (std::any_of(bindings.begin(), bindings.end(), [&entry](const auto& binding) {
                        return binding.binding == entry.binding;
                    }))
                        throw std::invalid_argument("Vulkan RHI: duplicate layout binding.");
                    bindings.push_back({entry.binding, ToVkDescriptorType(entry.type),
                        entry.count, ToVkShaderStages(entry.stages), nullptr});
                }
                VkDescriptorSetLayoutCreateInfo create{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
                create.bindingCount = static_cast<std::uint32_t>(bindings.size());
                create.pBindings = bindings.data();
                auto& vk = *_device->ContextPointer->_impl;
                VkDescriptorSetLayoutSupport support{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_SUPPORT};
                vk.vkGetDescriptorSetLayoutSupport(vk.device, &create, &support);
                if (!support.supported)
                    throw std::invalid_argument("Vulkan RHI: descriptor layout exceeds device support.");
                Check(vk.vkCreateDescriptorSetLayout(vk.device, &create, nullptr, &_layout),
                    "vkCreateDescriptorSetLayout");
            }
            ~VulkanBindingLayout() override { CloseNative(); }
            void CloseNative() noexcept override
            {
                if (_layout)
                {
                    auto& vk = *_device->ContextPointer->_impl;
                    vk.vkDestroyDescriptorSetLayout(vk.device, _layout, nullptr);
                    _layout = VK_NULL_HANDLE;
                }
            }
            [[nodiscard]] const BindingLayoutDesc& Desc() const noexcept override { return _desc; }
            [[nodiscard]] VkDescriptorSetLayout Native() const noexcept { return _layout; }
            [[nodiscard]] const std::shared_ptr<VulkanDeviceState>& DeviceState() const noexcept { return _device; }
        private:
            std::shared_ptr<VulkanDeviceState> _device;
            VulkanNativeRegistration _registration;
            BindingLayoutDesc _desc;
            VkDescriptorSetLayout _layout = VK_NULL_HANDLE;
        };

#include "VulkanPipelineInternal.inc"

        class VulkanBindingSet final : public BindingSet
        {
        public:
            VulkanBindingSet(std::shared_ptr<VulkanDeviceState> state, const BindingSetDesc& desc,
                std::shared_ptr<VulkanBindingLayout> ownedLayout = {})
                : _device(std::move(state)), _desc(desc), _ownedLayout(std::move(ownedLayout))
            {
                if (_ownedLayout) _desc.layout = _ownedLayout.get();
                _layout = dynamic_cast<const VulkanBindingLayout*>(_desc.layout);
                if (!_layout || _layout->DeviceState() != _device)
                    throw std::invalid_argument("Vulkan RHI: binding layout belongs to another device.");
                if (!_ownedLayout) _ownedLayout = std::make_shared<VulkanBindingLayout>(_device, _layout->Desc());
                _layout = _ownedLayout.get(); _desc.layout = _layout;
                auto& vk = *_device->ContextPointer->_impl;
                vk.vkGetPhysicalDeviceProperties(vk.physical, &_properties);
                Validate();
                for (const auto& entry : _desc.entries)
                    _resourceLifetimes.push_back(std::visit([](const auto& resource) -> std::weak_ptr<void> {
                        using T = std::decay_t<decltype(resource)>;
                        if constexpr (std::is_same_v<T, BufferBinding>)
                            return static_cast<const VulkanBuffer*>(resource.buffer)->Lifetime();
                        else if constexpr (std::is_same_v<T, TextureBinding>)
                            return static_cast<const VulkanTextureView*>(resource.view)->Lifetime();
                        else return static_cast<const VulkanSampler*>(resource.sampler)->Lifetime();
                    }, entry.resource));
            }
            void ValidateResources() const { Validate(); }
            void ValidateSampledAccess() const
            {
                for (const auto& entry : _desc.entries)
                    if (Declaration(entry.binding).type == BindingType::SampledTexture)
                    {
                        const auto& texture = static_cast<const VulkanTexture&>(
                            std::get<TextureBinding>(entry.resource).view->TextureResource());
                        if (texture.State() != ResourceState::Common && !HasAny(texture.State(), ResourceState::ShaderRead))
                            throw std::invalid_argument("Vulkan RHI: sampled draw requires shader-readable texture state.");
                    }
            }
            [[nodiscard]] bool TextureDescriptorsCurrent() const
            {
                if (_textureDescriptors.size() != _desc.entries.size()) return false;
                for (std::size_t i = 0; i < _desc.entries.size(); ++i)
                    if (const auto* binding = std::get_if<TextureBinding>(&_desc.entries[i].resource))
                    {
                        const auto& view = dynamic_cast<const VulkanTextureView&>(*binding->view);
                        const auto layout = Declaration(_desc.entries[i].binding).type == BindingType::StorageTexture
                            ? VK_IMAGE_LAYOUT_GENERAL : ToVkSampledDescriptorLayout(static_cast<const VulkanTexture&>(view.TextureResource()).State());
                        if (_textureDescriptors[i] != std::pair{view.Native(), layout}) return false;
                    }
                return true;
            }
            [[nodiscard]] const BindingSetDesc& Desc() const noexcept override { return _desc; }
            [[nodiscard]] VkDeviceSize UniformOffsetAlignment() const noexcept
            {
                return _properties.limits.minUniformBufferOffsetAlignment;
            }
            [[nodiscard]] bool FrameHasOverflowPools() const noexcept
            {
                return _device->Frames->CurrentDescriptorPages() > 1;
            }

            void RecordDiagnosticUse() const
            {
                const auto set = Native();
                auto& vk = *_device->ContextPointer->_impl;
                const auto setLayout = _layout->Native();
                VkPipelineLayoutCreateInfo layoutCreate{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
                layoutCreate.setLayoutCount = 1;
                layoutCreate.pSetLayouts = &setLayout;
                VkPipelineLayout layout = VK_NULL_HANDLE;
                VkCommandPool pool = VK_NULL_HANDLE;
                try
                {
                    Check(vk.vkCreatePipelineLayout(vk.device, &layoutCreate, nullptr, &layout), "vkCreatePipelineLayout(binding diagnostic)");
                    VkCommandPoolCreateInfo poolCreate{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
                    poolCreate.queueFamilyIndex = vk.graphicsFamily;
                    Check(vk.vkCreateCommandPool(vk.device, &poolCreate, nullptr, &pool), "vkCreateCommandPool(binding diagnostic)");
                    VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
                    allocate.commandPool = pool;
                    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                    allocate.commandBufferCount = 1;
                    VkCommandBuffer commands = VK_NULL_HANDLE;
                    Check(vk.vkAllocateCommandBuffers(vk.device, &allocate, &commands), "vkAllocateCommandBuffers(binding diagnostic)");
                    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
                    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                    Check(vk.vkBeginCommandBuffer(commands, &begin), "vkBeginCommandBuffer(binding diagnostic)");
                    vk.vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set, 0, nullptr);
                    Check(vk.vkEndCommandBuffer(commands), "vkEndCommandBuffer(binding diagnostic)");
                    // Reserve before submission so retaining the native objects
                    // cannot fail after their work has entered the GPU queue.
                    VkCommandBufferSubmitInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
                    commandInfo.commandBuffer = commands;
                    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
                    submit.commandBufferInfoCount = 1;
                    submit.pCommandBufferInfos = &commandInfo;
                    _device->Frames->SubmitTransient(submit, pool, layout);
                }
                catch (...)
                {
                    if (pool) vk.vkDestroyCommandPool(vk.device, pool, nullptr);
                    if (layout) vk.vkDestroyPipelineLayout(vk.device, layout, nullptr);
                    throw;
                }
            }

            [[nodiscard]] const std::shared_ptr<VulkanDeviceState>& DeviceState() const noexcept { return _device; }
            [[nodiscard]] VkDescriptorSet Native(VulkanDescriptorAllocator* allocator = nullptr) const
            {
                if (!allocator && !_device->Frames->Active())
                    throw std::logic_error("Vulkan RHI: descriptor allocation requires an active frame.");
                Validate();
                auto& vk = *_device->ContextPointer->_impl;
                const VkDescriptorSetLayout layout = _layout->Native();
                const VkDescriptorSet set = allocator ? allocator->Allocate(layout, _layout->Desc())
                    : _device->AllocateDescriptors(layout, _layout->Desc());
                // Allocate a fresh set for every materialization: no update can
                // overwrite a descriptor previously recorded for GPU use.
                std::vector<VkDescriptorBufferInfo> buffers(_desc.entries.size());
                std::vector<VkDescriptorImageInfo> images(_desc.entries.size());
                std::vector<std::pair<VkImageView, VkImageLayout>> textureDescriptors(_desc.entries.size());
                std::vector<VkWriteDescriptorSet> writes;
                writes.reserve(_desc.entries.size());
                for (std::size_t i = 0; i < _desc.entries.size(); ++i)
                {
                    const auto& entry = _desc.entries[i];
                    const auto& declaration = Declaration(entry.binding);
                    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                    write.dstSet = set;
                    write.dstBinding = entry.binding;
                    write.dstArrayElement = entry.arrayElement;
                    write.descriptorCount = 1;
                    write.descriptorType = ToVkDescriptorType(declaration.type);
                    if (const auto* buffer = std::get_if<BufferBinding>(&entry.resource))
                    {
                        buffers[i] = {dynamic_cast<const VulkanBuffer&>(*buffer->buffer).Native(),
                            buffer->offset, buffer->size ? buffer->size : buffer->buffer->Desc().size - buffer->offset};
                        write.pBufferInfo = &buffers[i];
                    }
                    else if (const auto* texture = std::get_if<TextureBinding>(&entry.resource))
                    {
                        images[i].imageView = dynamic_cast<const VulkanTextureView&>(*texture->view).Native();
                        images[i].imageLayout = declaration.type == BindingType::StorageTexture
                            ? VK_IMAGE_LAYOUT_GENERAL : ToVkSampledDescriptorLayout(static_cast<const VulkanTexture&>(texture->view->TextureResource()).State());
                        textureDescriptors[i] = {images[i].imageView, images[i].imageLayout};
                        write.pImageInfo = &images[i];
                    }
                    else
                    {
                        images[i].sampler = dynamic_cast<const VulkanSampler&>(
                            *std::get<SamplerBinding>(entry.resource).sampler).Native();
                        write.pImageInfo = &images[i];
                    }
                    writes.push_back(write);
                }
                vk.vkUpdateDescriptorSets(vk.device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
                _textureDescriptors = std::move(textureDescriptors);
                return set;
            }
        private:
            [[nodiscard]] const BindingLayoutEntry& Declaration(std::uint32_t binding) const
            {
                const auto& declarations = _layout->Desc().entries;
                const auto found = std::find_if(declarations.begin(), declarations.end(), [binding](const auto& item) {
                    return item.binding == binding;
                });
                if (found == declarations.end()) throw std::invalid_argument("Vulkan RHI: undeclared binding.");
                return *found;
            }
            void Validate() const
            {
                _device->RequireAlive();
                for (const auto& lifetime : _resourceLifetimes)
                    if (lifetime.expired()) throw std::invalid_argument("Vulkan RHI: binding resource has expired.");
                std::uint64_t required = 0;
                for (const auto& declaration : _layout->Desc().entries) required += declaration.count;
                if (required != _desc.entries.size())
                    throw std::invalid_argument("Vulkan RHI: binding set must initialize every array element.");
                for (std::size_t i = 0; i < _desc.entries.size(); ++i)
                {
                    const auto& entry = _desc.entries[i];
                    const auto& declaration = Declaration(entry.binding);
                    if (entry.arrayElement >= declaration.count)
                        throw std::invalid_argument("Vulkan RHI: binding array index exceeds layout.");
                    for (std::size_t j = 0; j < i; ++j)
                        if (_desc.entries[j].binding == entry.binding && _desc.entries[j].arrayElement == entry.arrayElement)
                            throw std::invalid_argument("Vulkan RHI: duplicate binding array element.");
                    if (declaration.type == BindingType::UniformBuffer || declaration.type == BindingType::StorageBuffer)
                    {
                        const auto* value = std::get_if<BufferBinding>(&entry.resource);
                        const auto* buffer = value ? dynamic_cast<const VulkanBuffer*>(value->buffer) : nullptr;
                        if (!buffer || buffer->DeviceState() != _device)
                            throw std::invalid_argument("Vulkan RHI: invalid buffer binding.");
                        const bool uniform = declaration.type == BindingType::UniformBuffer;
                        const auto alignment = uniform ? _properties.limits.minUniformBufferOffsetAlignment
                            : _properties.limits.minStorageBufferOffsetAlignment;
                        const auto maximum = uniform ? _properties.limits.maxUniformBufferRange
                            : _properties.limits.maxStorageBufferRange;
                        const auto total = buffer->Desc().size;
                        if (!Has(buffer->Desc().usage, uniform ? BufferUsage::Uniform : BufferUsage::Storage)
                            || value->offset >= total || value->offset % alignment != 0
                            || value->size > total - value->offset)
                            throw std::invalid_argument("Vulkan RHI: buffer binding usage, alignment or bounds invalid.");
                        const auto range = value->size ? value->size : total - value->offset;
                        if (range > maximum) throw std::invalid_argument("Vulkan RHI: descriptor buffer range exceeds device limit.");
                    }
                    else if (declaration.type == BindingType::Sampler)
                    {
                        const auto* value = std::get_if<SamplerBinding>(&entry.resource);
                        const auto* sampler = value ? dynamic_cast<const VulkanSampler*>(value->sampler) : nullptr;
                        if (!sampler || sampler->DeviceState() != _device)
                            throw std::invalid_argument("Vulkan RHI: invalid sampler binding.");
                    }
                    else
                    {
                        const auto* value = std::get_if<TextureBinding>(&entry.resource);
                        const auto* view = value ? dynamic_cast<const VulkanTextureView*>(value->view) : nullptr;
                        if (!view || view->DeviceState() != _device || !view->TextureAlive())
                            throw std::invalid_argument("Vulkan RHI: texture view belongs to another session.");
                        const auto* texture = view ? dynamic_cast<const VulkanTexture*>(&view->TextureResource()) : nullptr;
                        const auto usage = declaration.type == BindingType::StorageTexture ? TextureUsage::Storage : TextureUsage::Sampled;
                        if (!texture || texture->DeviceState() != _device || !Has(texture->Desc().usage, usage))
                            throw std::invalid_argument("Vulkan RHI: invalid texture binding.");
                    }
                }
            }
            std::shared_ptr<VulkanDeviceState> _device;
            BindingSetDesc _desc;
            std::shared_ptr<VulkanBindingLayout> _ownedLayout;
            std::vector<std::weak_ptr<void>> _resourceLifetimes;
            mutable std::vector<std::pair<VkImageView, VkImageLayout>> _textureDescriptors;
            const VulkanBindingLayout* _layout = nullptr;
            VkPhysicalDeviceProperties _properties{};
        };

        VulkanBuffer::VulkanBuffer(std::shared_ptr<VulkanDeviceState> state, const BufferDesc& desc)
            : _device(std::move(state)), _registration(*_device, *this), _desc(desc), _state(ResourceState::Undefined)
        {
            const auto storage = _device->Resources->CreateBuffer(desc);
            _buffer = storage.Buffer; _allocation = storage.Allocation; _mapped = storage.Mapped;
            ++_device->Buffers;
        }

        VulkanBuffer::~VulkanBuffer() { CloseNative(); }
        void VulkanBuffer::CloseNative() noexcept
        {
            if (_buffer != VK_NULL_HANDLE)
            {
                try { _device->FlushScene(); } catch (...) {}
                _device->Retire([allocator = _device->Allocator, buffer = _buffer, allocation = _allocation] {
                    vmaDestroyBuffer(allocator, buffer, allocation);
                });
                _buffer = VK_NULL_HANDLE;
                _allocation = VK_NULL_HANDLE;
                _mapped = nullptr;
                --_device->Buffers;
            }
        }

        VulkanSampler::VulkanSampler(std::shared_ptr<VulkanDeviceState> state, const SamplerDesc& desc)
            : _device(std::move(state)), _registration(*_device, *this), _desc(desc)
        {
            _sampler = _device->Resources->CreateSampler(desc);
            ++_device->Samplers;
        }

        VulkanSampler::~VulkanSampler() { CloseNative(); }
        void VulkanSampler::CloseNative() noexcept
        {
            if (_sampler != VK_NULL_HANDLE)
            {
                if (!_device->Closing) { try { _device->ForgetScene(this); } catch (...) {} }
                --_device->Samplers;
                auto& vk = *_device->ContextPointer->_impl;
                _device->Retire([device = vk.device, destroy = vk.vkDestroySampler, sampler = _sampler] {
                    destroy(device, sampler, nullptr);
                });
                _sampler = VK_NULL_HANDLE;
            }
        }

        VulkanTexture::VulkanTexture(std::shared_ptr<VulkanDeviceState> state,
            const TextureDesc& desc, TextureHandle handle)
            : _device(std::move(state)), _registration(*_device, *this), _desc(desc), _handle(handle), _state(ResourceState::Undefined)
        {
            CreateImage();
            ++_device->Textures;
        }

        void VulkanTexture::CreateImage()
        {
            const auto storage = _device->Resources->CreateImage(_desc);
            _image = storage.Image; _allocation = storage.Allocation;
        }

        VkImageView VulkanTexture::SampledView()
        {
            _device->RequireAlive();
            if (_sampledView != VK_NULL_HANDLE) return _sampledView;
            TextureViewDesc view{};
            view.mipLevelCount = _desc.mipLevels; view.arrayLayerCount = _desc.arrayLayers;
            _sampledView = _device->Resources->CreateView(_image, _desc, view, true);
            return _sampledView;
        }

        void VulkanTexture::DestroyImage() noexcept
        {
            if (_image != VK_NULL_HANDLE)
            {
                auto& vk = *_device->ContextPointer->_impl;
                _device->Retire([device = vk.device, destroyView = vk.vkDestroyImageView,
                    view = _sampledView, allocator = _device->Allocator, image = _image, allocation = _allocation] {
                    if (view) destroyView(device, view, nullptr);
                    vmaDestroyImage(allocator, image, allocation);
                });
                _sampledView = VK_NULL_HANDLE;
                _image = VK_NULL_HANDLE;
                _allocation = VK_NULL_HANDLE;
            }
        }

        VulkanTexture::~VulkanTexture() { CloseNative(); }
        void VulkanTexture::CloseNative() noexcept
        {
            if (!_image) return;
            if (!_device->Closing) { try { _device->ForgetScene(this); } catch (...) {} }
            for (VulkanTextureView* view : _views)
                view->DestroyNative();
            _views.clear();
            DestroyImage();
            {
                std::lock_guard lock(_device->TextureMutex);
                const auto found = _device->TexturesByHandle.find(_handle.value);
                if (found != _device->TexturesByHandle.end() && found->second == this)
                    _device->TexturesByHandle.erase(found);
            }
            --_device->Textures;
            _handle = {};
        }

        void VulkanTexture::Resize(std::uint32_t width, std::uint32_t height)
        {
            _device->RequireAlive();
            if (width == 0 || height == 0)
                throw std::invalid_argument("Vulkan RHI: a texture extent cannot be zero.");
            if (_desc.width == width && _desc.height == height) return;
            _device->FlushScene();
            TextureDesc resized = _desc;
            resized.width = width;
            resized.height = height;
            // Admit the entire replacement before changing the live image or
            // public views. Allocation failure leaves the old texture usable.
            VulkanTexture replacement(_device, resized, _handle);
            std::vector<std::unique_ptr<VulkanTextureView>> views;
            views.reserve(_views.size());
            for (const auto* view : _views)
                views.push_back(std::make_unique<VulkanTextureView>(replacement, view->Desc()));
            if (_sampledView) (void)replacement.SampledView();
            std::swap(_desc, replacement._desc);
            std::swap(_image, replacement._image);
            std::swap(_allocation, replacement._allocation);
            std::swap(_sampledView, replacement._sampledView);
            std::swap(_state, replacement._state);
            ++_generation;
            for (std::size_t i = 0; i < _views.size(); ++i)
            {
                const auto before = _views[i]->Native();
                _views[i]->SwapNative(*views[i]);
                _device->ReplaceView(before, _views[i]->Native());
            }
            // Old views retire before their old image. Public object identity
            // and TextureHandle stay stable, and later descriptors use new views.
        }

        void VulkanTexture::Register(VulkanTextureView& view)
        {
            _views.push_back(&view);
            try { view.CreateNative(); }
            catch (...)
            {
                _views.pop_back();
                throw;
            }
        }

        void VulkanTexture::Unregister(VulkanTextureView& view) noexcept
        {
            const auto found = std::find(_views.begin(), _views.end(), &view);
            if (found != _views.end()) _views.erase(found);
        }

        VulkanTextureView::VulkanTextureView(VulkanTexture& texture, const TextureViewDesc& desc)
            : _texture(texture), _device(texture.DeviceState()), _textureLifetime(texture.Lifetime()), _desc(desc)
        {
            _device->RequireAlive();
            _desc = VulkanResources::ResolveViewDesc(texture.Desc(), desc);
            texture.Register(*this);
        }

        VulkanTextureView::~VulkanTextureView()
        {
            DestroyNative();
            if (!_textureLifetime.expired()) _texture.Unregister(*this);
        }

        void VulkanTextureView::CreateNative()
        {
            _device->RequireAlive();
            if (_view != VK_NULL_HANDLE) return;
            _view = _device->Resources->CreateView(_texture.Native(), _texture.Desc(), _desc);
        }

        void VulkanTextureView::DestroyNative() noexcept
        {
            if (_view != VK_NULL_HANDLE)
            {
                auto& state = *_device;
                try { state.FlushScene(); } catch (...) {}
                state.ReplaceView(_view, VK_NULL_HANDLE);
                auto& vk = *state.ContextPointer->_impl;
                state.Retire([device = vk.device, destroy = vk.vkDestroyImageView, view = _view] {
                    destroy(device, view, nullptr);
                });
                _view = VK_NULL_HANDLE;
            }
        }

        // One of the scene shader set's four programs: its modules, its
        // descriptor layout and the CPU copy of its constant block, which the
        // constant sink writes and a draw copies out (Vulkan has no program
        // object to hold uniforms the way OpenGL does).
        struct VulkanSceneProgram final : VulkanSceneUniforms
        {
            using VulkanSceneUniforms::VulkanSceneUniforms;
            SceneProgram Id = SceneProgram::Main;
            const std::uint64_t Identity = NextSceneResourceIdentity();
            std::shared_ptr<VulkanDeviceState> Device;
            ~VulkanSceneProgram()
            {
                if (Device)
                {
                    if (Device->CurrentSceneProgram == this) Device->CurrentSceneProgram = nullptr;
                    // Match native wrapper teardown: a flush failure must not
                    // throw from destruction during construction rollback/loss.
                    try { Device->ForgetScene(this); } catch (...) {}
                }
            }
            std::unique_ptr<VulkanShader> Vertex;
            std::unique_ptr<VulkanShader> Fragment;
            std::array<std::unique_ptr<VulkanBindingLayout>, SceneShaderAbi::GroupCount> Layouts;
            std::vector<Generated::TextureBinding> Textures;
        };

#include "VulkanCommandListInternal.inc"

        class VulkanGraphicsDevice final : public GraphicsDevice
        {
        public:
            explicit VulkanGraphicsDevice(Context& context) : _state(std::make_shared<VulkanDeviceState>(context)) {}
            ~VulkanGraphicsDevice() override
            {
                _state->CloseNative();
                _retained.clear();
                _pipelines.clear();
            }

            [[nodiscard]] GraphicsBackend GetBackend() const noexcept override { return GraphicsBackend::Vulkan; }
            [[nodiscard]] LowLatencyCapabilities LowLatencyCaps() const noexcept override
            {
                const auto* vk = _state->ContextPointer ? _state->ContextPointer->_impl.get() : nullptr;
                return vk && vk->reflex ? vk->reflex->Caps() : LowLatencyCapabilities{true, false, LowLatencyProvider::Generic,
                    vk ? std::string_view(vk->reflexUnavailableReason) : "Vulkan presentation session is closed."};
            }
            [[nodiscard]] bool WaitForLatestSubmission(std::uint64_t timeoutNanoseconds) override
            { _state->RequireAlive(); return _state->Scheduler->WaitForLatest(timeoutNanoseconds); }
            [[nodiscard]] PresentationWaitStatistics PresentationWaits() const noexcept override
            { return _state->Scheduler->PresentationWaits(); }
            bool SupportsAsyncReadback() const noexcept override { return true; }
            ReadbackTicket EnqueueReadback(Buffer&, std::uint64_t, std::uint64_t) override;
            void PollReadbacks() override { _state->Readbacks.Poll(); }
            void SetReadbackLimits(ReadbackLimits limits) override { _state->Readbacks.SetLimits(limits); }
            ReadbackUsage ReadbackStatistics() const override { return _state->Readbacks.Usage(); }
            std::unique_ptr<TimestampQuerySet> CreateTimestampQuerySet(std::uint32_t count, std::string_view label) override
            {
                _state->RequireAlive(); TimestampWriteState validate(count); ValidateDebugLabel({std::string(label)});
                if (!_state->ContextPointer->Caps().supportsTimestampQueries) return {};
                auto reservation = _state->TimestampCapacity->TryReserve();
                if (!reservation) return {};
                return std::make_unique<VulkanTimestampSet>(_state, count, std::string(label), std::move(reservation));
            }
            [[nodiscard]] MemoryBudgetSnapshot MemoryBudget() const override { return _state->Memory->Snapshot(); }
            [[nodiscard]] MemoryTelemetry MemoryUsageTelemetry() const override { return _state->Memory->Telemetry(); }
            [[nodiscard]] const std::shared_ptr<VulkanDeviceState>& State() const noexcept { return _state; }
            [[nodiscard]] InteropDevice Describe() const;
            [[nodiscard]] VulkanNvidiaReflex* Reflex() const noexcept { return _state->ContextPointer->_impl->reflex; }
            [[nodiscard]] const Capabilities& GetCapabilities() const noexcept override
            {
                return _state->ContextPointer->Caps();
            }

            [[nodiscard]] std::unique_ptr<Buffer> CreateBuffer(const BufferDesc& desc) override
            {
                auto buffer = std::make_unique<VulkanBuffer>(_state, desc);
                if (desc.initialState != ResourceState::Undefined)
                {
                    auto commands = CreateCommandList();
                    commands->Begin();
                    commands->Transition(*buffer, ResourceState::Undefined, desc.initialState);
                    commands->End();
                }
                return buffer;
            }

            [[nodiscard]] std::unique_ptr<Texture> CreateTexture(const TextureDesc& desc) override
            {
                std::int32_t handle;
                {
                    std::lock_guard lock(_state->TextureMutex);
                    handle = _state->NextTextureHandle;
                    while (_state->TexturesByHandle.contains(handle))
                        handle = handle == std::numeric_limits<std::int32_t>::max() ? 1 : handle + 1;
                    _state->NextTextureHandle = handle == std::numeric_limits<std::int32_t>::max() ? 1 : handle + 1;
                }
                auto texture = std::make_unique<VulkanTexture>(_state, desc, TextureHandle{handle});
                InitializeTextureState(*texture, desc.initialState);
                bool inserted;
                {
                    std::lock_guard lock(_state->TextureMutex);
                    inserted = _state->TexturesByHandle.emplace(handle, texture.get()).second;
                }
                if (!inserted) throw std::invalid_argument("Vulkan RHI: texture handle already live.");
                return texture;
            }

            [[nodiscard]] std::unique_ptr<Texture> CreateTexture(
                const TextureDesc& desc, TextureHandle handle) override
            {
                if (!handle) throw std::invalid_argument("Vulkan RHI: a chosen texture handle must be nonzero.");
                auto texture = std::make_unique<VulkanTexture>(_state, desc, handle);
                InitializeTextureState(*texture, desc.initialState);
                bool inserted;
                {
                    std::lock_guard lock(_state->TextureMutex);
                    inserted = _state->TexturesByHandle.emplace(handle.value, texture.get()).second;
                }
                if (!inserted) throw std::invalid_argument("Vulkan RHI: texture handle already live.");
                return texture;
            }

            [[nodiscard]] Texture* FindTexture(TextureHandle handle) noexcept override
            {
                std::lock_guard lock(_state->TextureMutex);
                const auto found = _state->TexturesByHandle.find(handle.value);
                return found == _state->TexturesByHandle.end() ? nullptr : found->second;
            }

            Texture& RetainTexture(std::unique_ptr<Texture> texture) override
            {
                if (!texture)
                    throw std::invalid_argument("Vulkan RHI: retained texture belongs to another device.");
                (void)CheckedResource<VulkanTexture>(*texture, _state);
                Texture& retained = *texture;
                _retained.push_back(std::move(texture));
                return retained;
            }

            [[nodiscard]] std::unique_ptr<TextureView> CreateTextureView(
                Texture& texture, const TextureViewDesc& desc) override
            {
                auto& native = CheckedResource<VulkanTexture>(texture, _state);
                if (native.DeviceState() != _state)
                    throw std::invalid_argument("Vulkan RHI: texture belongs to another device.");
                return std::make_unique<VulkanTextureView>(native, desc);
            }

            [[nodiscard]] std::unique_ptr<Sampler> CreateSampler(const SamplerDesc& desc) override
            {
                return std::make_unique<VulkanSampler>(_state, desc);
            }

            [[nodiscard]] std::unique_ptr<Shader> CreateShader(const ShaderDesc& desc) override
            {
                return std::make_unique<VulkanShader>(_state, desc);
            }
            [[nodiscard]] std::unique_ptr<BindingLayout> CreateBindingLayout(const BindingLayoutDesc& desc) override
            {
                return std::make_unique<VulkanBindingLayout>(_state, desc);
            }
            [[nodiscard]] std::unique_ptr<BindingSet> CreateBindingSet(const BindingSetDesc& desc) override
            {
                return std::make_unique<VulkanBindingSet>(_state, desc);
            }
            [[nodiscard]] std::unique_ptr<GraphicsPipeline> CreateGraphicsPipeline(
                const GraphicsPipelineDesc& desc) override
            {
                const auto* vertex = dynamic_cast<const VulkanShader*>(desc.vertexShader);
                const auto* fragment = dynamic_cast<const VulkanShader*>(desc.fragmentShader);
                // A scene pass: the scene shader set's program (or none, for
                // the state a frame ends in) and no layout or vertex input.
                if (desc.pipelineLayout.groups.empty() && desc.vertexBuffers.empty() && desc.vertexAttributes.empty()
                    && ((!desc.vertexShader && !desc.fragmentShader)
                        || (vertex && fragment && vertex->SceneProgram && fragment->SceneProgram
                            && vertex->DeviceState() == _state && fragment->DeviceState() == _state)))
                    return std::make_unique<VulkanGraphicsPipeline>(_state, desc, VulkanGraphicsPipeline::Deferred{});
                if (!vertex || !fragment || desc.pipelineLayout.groups.empty() || vertex->DeviceState() != _state
                    || fragment->DeviceState() != _state)
                    throw std::invalid_argument("Vulkan RHI: pipeline resources belong to another device.");
                VulkanPipelineKey key(desc);
                const auto hash = key.Hash();
                std::lock_guard lock(_pipelineMutex);
                const auto range = _pipelines.equal_range(hash);
                for (auto found = range.first; found != range.second; ++found)
                    if (found->second.first == key)
                        return std::make_unique<VulkanGraphicsPipeline>(desc, found->second.second);
                auto pipeline = std::make_shared<VulkanGraphicsPipeline>(_state, desc);
                _pipelines.emplace(hash, std::make_pair(std::move(key), pipeline));
                return std::make_unique<VulkanGraphicsPipeline>(desc, std::move(pipeline));
            }
            [[nodiscard]] std::unique_ptr<CommandList> CreateCommandList() override
            {
                return std::make_unique<VulkanCommandList>(_state);
            }

            void WriteTexture(Texture& texture, const TextureWrite& write) override
            {
                auto& image = CheckedResource<VulkanTexture>(texture, _state);
                if (image.DeviceState() != _state)
                    throw std::invalid_argument("Vulkan RHI: texture belongs to another device.");
                if (write.data == nullptr || write.width == 0 || write.height == 0
                    || image.Desc().depth != 1 || image.Desc().arrayLayers != 1
                    || image.Desc().sampleCount != 1 || write.format != image.Desc().format)
                    throw std::invalid_argument("Vulkan RHI: texture upload format or extent is invalid.");
                if (write.width != image.Desc().width || write.height != image.Desc().height)
                    image.Resize(write.width, write.height);
                if (!Has(image.Desc().usage, TextureUsage::TransferDst))
                    throw std::invalid_argument("Vulkan RHI: texture upload requires TransferDst usage.");
                const std::size_t texels = static_cast<std::size_t>(write.width) * write.height;
                const std::size_t dataSize = texels * BytesPerPixel(write.format);
                const auto* source = static_cast<const std::byte*>(write.data);
                WithUploadCommands(dataSize, [&](VulkanCommandList& commands) {
                    commands.UploadTexture(image, std::span(source, dataSize));
                });
            }

            void WriteBuffer(Buffer& buffer, std::uint64_t offset,
                std::span<const std::byte> data) override
            {
                auto& destination = CheckedResource<VulkanBuffer>(buffer, _state);
                if (destination.DeviceState() != _state)
                    throw std::invalid_argument("Vulkan RHI: buffer belongs to another device.");
                if (data.empty() || offset > destination.Desc().size
                    || data.size() > destination.Desc().size - offset)
                    throw std::out_of_range("Vulkan RHI: buffer write range is empty or outside the resource.");
                if (destination.Desc().memoryUsage == MemoryUsage::GpuToCpu)
                    throw std::invalid_argument("Vulkan RHI: a readback buffer cannot be used as an upload destination.");

                if (destination.Desc().memoryUsage == MemoryUsage::CpuToGpu)
                {
                    std::memcpy(destination.Mapped() + offset, data.data(), data.size());
                    Check(vmaFlushAllocation(_state->Allocator, destination.Allocation(),
                        offset, data.size()), "vmaFlushAllocation");
                    return;
                }
                if (!Has(destination.Desc().usage, BufferUsage::TransferDst))
                    throw std::invalid_argument("Vulkan RHI: GPU-only buffer upload requires TransferDst usage.");

                WithUploadCommands(data.size(), [&](VulkanCommandList& commands) { commands.UploadBuffer(destination, offset, data); });
            }

            void ReadBuffer(Buffer& buffer, std::uint64_t offset, std::span<std::byte> data) override
            {
                auto& source = CheckedResource<VulkanBuffer>(buffer, _state);
                if (source.DeviceState() != _state)
                    throw std::invalid_argument("Vulkan RHI: buffer belongs to another device.");
                if (data.empty() || offset > source.Desc().size
                    || data.size() > source.Desc().size - offset)
                    throw std::out_of_range("Vulkan RHI: buffer read range is empty or outside the resource.");
                if (source.Desc().memoryUsage != MemoryUsage::GpuToCpu)
                    throw std::invalid_argument("Vulkan RHI: CPU readback requires MemoryUsage::GpuToCpu.");
                WaitIdle();
                void* mapped = nullptr;
                Check(vmaMapMemory(_state->Allocator, source.Allocation(), &mapped), "vmaMapMemory");
                Check(vmaInvalidateAllocation(_state->Allocator, source.Allocation(),
                    offset, data.size()), "vmaInvalidateAllocation");
                std::memcpy(data.data(), static_cast<const std::byte*>(mapped) + offset, data.size());
                vmaUnmapMemory(_state->Allocator, source.Allocation());
            }

            void ResizeTexture(Texture& texture, std::uint32_t width, std::uint32_t height) override
            {
                auto& image = CheckedResource<VulkanTexture>(texture, _state);
                if (image.DeviceState() != _state)
                    throw std::invalid_argument("Vulkan RHI: texture belongs to another device.");
                image.Resize(width, height);
            }

            [[nodiscard]] FrameContext BeginFrame() override
            {
                // A frame begun over an open one ends it first, as OpenGL's does.
                if (_state->Frames->Active()) EndFrame();
                _state->Readbacks.Poll();
                return _state->BeginDescriptorFrame();
            }

            void EndFrame() override
            {
                // The frame's scene work is submitted as the frame ends.
                _state->FlushScene();
                _state->EndDescriptorFrame();
                _state->Readbacks.Poll();
            }

            void WaitIdle() override
            {
                _state->FlushScene();
                ++_state->HostWaits;
                ++_state->DeviceWideWaits;
                _state->ContextPointer->WaitIdle();
                _state->Frames->ObserveDeviceIdle();
                _state->CollectRetired();
            }

            void ClearPipelineCacheForCheck()
            {
                WaitIdle();
                TrimCaches();
            }

            void TrimCaches() override
            {
                std::lock_guard lock(_pipelineMutex);
                _pipelines.clear();
            }

            [[nodiscard]] GpuResourceStatistics Statistics() const override
            {
                GpuResourceStatistics result{};
                _state->CollectRetired();
                result.Textures = _state->Textures.load();
                result.Buffers = _state->Buffers.load();
                result.Shaders = _state->Shaders.load();
                result.Programs = _state->Programs.load();
                result.Samplers = _state->Samplers.load();
                result.TransferScratchPages = _state->ScratchPages.load();
                result.TimestampSets = _state->TimestampCapacity->Sets();
                result.CompletedFrame = _state->Frames->Completed();
                result.HostWaits = _state->HostWaits.load();
                result.DeviceWideWaits = _state->DeviceWideWaits.load();
                result.Submitted = _state->Scheduler->Submitted();
                result.Completed = _state->Scheduler->Completed();
                result.Retired = static_cast<std::uint32_t>(_state->Retired.Size());
                return result;
            }

            [[nodiscard]] std::string AdapterDescription() override
            {
                return _state->ContextPointer->DeviceName();
            }

            [[nodiscard]] std::int32_t DrainErrors() override
            {
                const unsigned now = _state->ContextPointer->ValidationErrors();
                const unsigned old = _reportedValidationErrors.exchange(now);
                return now == old ? 0 : static_cast<std::int32_t>(now - old);
            }

            [[nodiscard]] bool CanRender(const RenderingInfo& info) override
            {
                if (info.width == 0 || info.height == 0) return false;
                for (const RenderingColorAttachment& attachment : info.colorAttachments)
                {
                    if (!attachment.view) continue;
                    const auto& view = CheckedResource<VulkanTextureView>(*attachment.view, _state);
                    const auto& image = CheckedResource<VulkanTexture>(view.TextureResource(), _state);
                    if (!Has(image.Desc().usage, TextureUsage::ColorAttachment)) return false;
                }
                if (info.depthStencilAttachment && info.depthStencilAttachment->view)
                {
                    const auto& view = CheckedResource<VulkanTextureView>(*info.depthStencilAttachment->view, _state);
                    const auto& image = CheckedResource<VulkanTexture>(view.TextureResource(), _state);
                    if (!Has(image.Desc().usage, TextureUsage::DepthStencilAttachment)) return false;
                }
                return true;
            }

            [[nodiscard]] std::uint32_t DepthBits(const RenderingInfo& info) override
            {
                if (!info.depthStencilAttachment || !info.depthStencilAttachment->view) return 0;
                const auto& view = CheckedResource<VulkanTextureView>(*info.depthStencilAttachment->view, _state);
                const auto& image = CheckedResource<VulkanTexture>(view.TextureResource(), _state);
                switch (image.Desc().format)
                {
                case TextureFormat::D16Unorm: return 16;
                case TextureFormat::D24UnormS8Uint: return 24;
                case TextureFormat::D32Float:
                case TextureFormat::D32FloatS8Uint: return 32;
                default: return 0;
                }
            }

        private:
            template<class Write> void WithUploadCommands(VkDeviceSize bytes, Write&& write)
            {
                auto* recording = static_cast<VulkanCommandList*>(_state->RecordingList);
                if (recording && recording != _uploadCommands.get())
                {
                    write(*recording);
                    return;
                }
                // Batch writes outside user recording in a bounded transfer
                // stream. A consumer/another list, frame end, readback, resource
                // release or session teardown flushes it in graphics-queue order.
                try
                {
                    if (!_uploadCommands)
                    {
                        _uploadCommands = std::make_unique<VulkanCommandList>(_state, true);
                        _uploadCommands->Begin();
                    }
                    constexpr VkDeviceSize batchBytes = 8U << 20U;
                    if (recording)
                    {
                        const auto pending = recording->PendingUploadBytes();
                        if (pending && (bytes >= batchBytes || bytes > batchBytes - pending)) recording->Flush();
                    }
                    // This stream's interval remains open across Flush().
                    // The write restarts its native buffer when necessary.
                    write(*_uploadCommands);
                    if (_uploadCommands->PendingUploadBytes() >= batchBytes) _uploadCommands->Flush();
                }
                catch (...)
                {
                    _uploadCommands.reset();
                    throw;
                }
            }
            void InitializeTextureState(VulkanTexture& texture, ResourceState initialState)
            {
                if (initialState == ResourceState::Undefined) return;
                auto commands = CreateCommandList();
                commands->Begin();
                commands->Transition(texture, ResourceState::Undefined, initialState);
                commands->End();
            }

            std::shared_ptr<VulkanDeviceState> _state;
            std::unique_ptr<VulkanCommandList> _uploadCommands;
            std::mutex _pipelineMutex;
            std::unordered_multimap<std::size_t,
                std::pair<VulkanPipelineKey, std::shared_ptr<VulkanGraphicsPipeline>>> _pipelines;
            std::vector<std::unique_ptr<Texture>> _retained{};
            std::atomic<unsigned> _reportedValidationErrors{0};
        };
#include "VulkanSceneInternal.inc"
#include "VulkanReadbackInternal.inc"

    std::unique_ptr<SceneShaderSet> CreateSceneShaderSet(GraphicsDevice& device,
        std::span<const float> toonTable, std::span<const float> shiftTable)
    {
        return std::make_unique<VulkanSceneShaderSet>(dynamic_cast<VulkanGraphicsDevice&>(device).State(),
            toonTable, shiftTable);
    }

    std::shared_ptr<MphRead::GpuMeshResource> CreateGpuMeshResource(
        GraphicsDevice& device, CommandList& commands, const MphRead::RendererGeometry& geometry)
    {
        return std::make_shared<VulkanGpuMesh>(dynamic_cast<VulkanGraphicsDevice&>(device),
            dynamic_cast<VulkanCommandList&>(commands), geometry);
    }

    std::shared_ptr<MphRead::TransientGeometryResource> CreateTransientGeometryResource(
        GraphicsDevice& device, CommandList& commands)
    {
        return std::make_shared<VulkanTransientGeometry>(dynamic_cast<VulkanGraphicsDevice&>(device).State(),
            dynamic_cast<VulkanCommandList&>(commands));
    }

    std::uint32_t EmbeddedShaderStages() noexcept
    {
        std::uint32_t stages = 0;
        for (const std::size_t words : {Generated::main_vert.size(), Generated::main_frag.size(),
                 Generated::composite_vert.size(), Generated::composite_frag.size(),
                 Generated::cel_vert.size(), Generated::cel_frag.size(),
                 Generated::shift_vert.size(), Generated::shift_frag.size(),
                 Generated::backdrop_vert.size(), Generated::backdrop_frag.size()})
            stages += words > 5 ? 1U : 0U;
        return stages;
    }

    InteropDevice DescribeDevice(GraphicsDevice& device)
    {
        return dynamic_cast<VulkanGraphicsDevice&>(device).Describe();
    }

    InteropDevice VulkanGraphicsDevice::Describe() const
    {
        auto& vk = *_state->ContextPointer->_impl;
        InteropDevice result{};
        result.Instance = reinterpret_cast<std::uint64_t>(vk.instance);
        result.PhysicalDevice = reinterpret_cast<std::uint64_t>(vk.physical);
        result.Device = reinterpret_cast<std::uint64_t>(vk.device);
        result.Queue = reinterpret_cast<std::uint64_t>(vk.graphics);
        result.QueueFamily = vk.graphicsFamily;
        result.ApiVersion = VK_API_VERSION_1_3;
        result.GetInstanceProcAddr = reinterpret_cast<void*>(Context::Impl::InstanceProc());
        result.GetDeviceProcAddr = reinterpret_cast<void*>(vk.vkGetDeviceProcAddr);
        return result;
    }

    void FlushDevice(GraphicsDevice& device)
    {
        dynamic_cast<VulkanGraphicsDevice&>(device).State()->FlushScene();
    }

    void BeginExternalSubmit(GraphicsDevice& device)
    {
        if (auto* reflex = dynamic_cast<VulkanGraphicsDevice&>(device).Reflex()) reflex->Mark(LowLatencyMarker::RenderSubmitStart);
    }

    InteropImage PrepareForExternal(GraphicsDevice& device, Texture& texture, ResourceState state)
    {
        auto& native = dynamic_cast<VulkanTexture&>(texture);
        if (native.State() != state)
        {
            auto commands = device.CreateCommandList();
            commands->Begin();
            commands->Transition(texture, native.State(), state);
            commands->End();
        }
        else
            FlushDevice(device);
        InteropImage result{};
        result.Image = reinterpret_cast<std::uint64_t>(native.Native());
        result.Format = static_cast<std::uint32_t>(VulkanResources::Format(native.Desc().format));
        result.Usage = static_cast<std::uint32_t>(VulkanResources::ImageUsage(native.Desc().usage));
        result.Layout = static_cast<std::uint32_t>(ToVkState(state, true,
            Has(native.Desc().usage, TextureUsage::Storage) && !Has(native.Desc().usage, TextureUsage::Sampled)).Layout);
        return result;
    }

    void AdoptExternalState(Texture& texture, ResourceState state)
    {
        auto& native = dynamic_cast<VulkanTexture&>(texture);
        native.State(state);
        native.DeviceState()->Scheduler->MarkExternalWork();
    }

    PresentResult PresentWindow(GraphicsDevice& device, Swapchain& swapchain)
    {
        auto& state = *dynamic_cast<VulkanGraphicsDevice&>(device).State();
        state.FlushScene();
        const auto acquired = swapchain.TryAcquireTexture();
        if (!acquired.texture) return {acquired.status, acquired.failure};
        VulkanTexture* window = state.WindowColor.get();
        if (!window || window->State() == ResourceState::Undefined)
        {
            RecordSwapchainBlit(swapchain, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED,
                VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, {});
        }
        else
        {
            const StateMapping mapping = ToVkState(window->State(), true);
            RecordSwapchainBlit(swapchain, window->Native(), mapping.Layout, mapping.Stages, mapping.Access,
                {window->Desc().width, window->Desc().height});
        }
        const auto result = swapchain.TryPresent();
        // Preserve the first native loss. Submitting a new completion marker
        // to a lost device could throw another error and replace this result.
        // The caller's explicit shutdown/idle/loss boundary closes ownership.
        if (result.failure || result.status == PresentationStatus::DeviceLost
            || result.status == PresentationStatus::SurfaceLost) return result;
        // The swapchain submits its own blit on this graphics queue. Cover
        // its source image's lifetime, independently of present queue fences.
        state.Scheduler->MarkExternalWork();
        return result;
    }

    void CheckMemoryAdmission(GraphicsDevice& device)
    {
        const auto state = dynamic_cast<VulkanGraphicsDevice&>(device).State();
        Testing::CheckMemoryAdmission(device, [state](std::uint64_t bytes) { state->Memory->SetBudgetCeilingForCheck(bytes); },
            [](Texture& texture, TextureView& view) { return std::pair{
                static_cast<VulkanTexture&>(texture).Native(), static_cast<VulkanTextureView&>(view).Native()}; });
        const auto before = device.MemoryUsageTelemetry();
        BufferDesc oversized{}; oversized.size = UINT64_MAX; oversized.usage = BufferUsage::Vertex;
        bool rejected = false;
        try { (void)device.CreateBuffer(oversized); }
        catch (const BackendError& error) { rejected = error.Kind() == BackendErrorKind::OutOfMemory && error.NativeCode() == 0; }
        const auto after = device.MemoryUsageTelemetry();
        if (!rejected || after.DeniedRequests != before.DeniedRequests + 1 || after.NativeFailures != before.NativeFailures
            || after.AcceptedRequests != before.AcceptedRequests || after.PendingRequests || after.PendingBytes)
            throw std::runtime_error("Oversized Vulkan buffer reached native requirements/allocation.");
        std::cout << "[memory admission] Vulkan oversized buffer rejected before native requirements query\n";
    }

    void CheckResourceRetirement(GraphicsDevice& device)
    {
        auto& state = *dynamic_cast<VulkanGraphicsDevice&>(device).State();
        const auto baseline = device.Statistics();
        auto commands = device.CreateCommandList();
        commands->Begin();
        BindingLayoutDesc layoutDesc{};
        layoutDesc.entries = {{0, BindingType::UniformBuffer, ShaderStage::Vertex, 1},
            {1, BindingType::SampledTexture, ShaderStage::Fragment, 1},
            {2, BindingType::Sampler, ShaderStage::Fragment, 1}};
        auto layout = device.CreateBindingLayout(layoutDesc);
        for (int cycle = 0; cycle < 64; ++cycle)
        {
            (void)device.BeginFrame();
            TextureDesc desc{};
            desc.width = 8; desc.height = 8;
            desc.format = TextureFormat::RGBA8Unorm;
            desc.usage = TextureUsage::ColorAttachment | TextureUsage::Sampled;
            auto texture = device.CreateTexture(desc);
            auto view = device.CreateTextureView(*texture, {});
            const auto handle = texture->Handle();
            auto sampler = device.CreateSampler({});
            BufferDesc bufferDesc{};
            bufferDesc.size = 4096; bufferDesc.usage = BufferUsage::Uniform;
            auto buffer = device.CreateBuffer(bufferDesc);
            BindingSetDesc setDesc{};
            setDesc.layout = layout.get();
            setDesc.entries = {{0, BufferBinding{buffer.get(), 0, 256}},
                {1, TextureBinding{view.get()}}, {2, SamplerBinding{sampler.get()}}};
            auto set = device.CreateBindingSet(setDesc);
            const RenderingColorAttachment color{view.get(), LoadOp::Clear, StoreOp::Store,
                {0.25F, 0.5F, 0.75F, 1.0F}};
            RenderingInfo rendering{};
            rendering.width = 8; rendering.height = 8;
            rendering.colorAttachments = std::span(&color, 1);
            commands->BeginRendering(rendering);
            commands->EndRendering();
            FlushDevice(device); // Deliberately do not wait for this clear.
            dynamic_cast<const VulkanBindingSet&>(*set).RecordDiagnosticUse();
            const auto before = dynamic_cast<const VulkanTextureView&>(*view).Native();
            device.ResizeTexture(*texture, 16, 12);
            if (texture->Handle() != handle || &view->TextureResource() != texture.get()
                || dynamic_cast<const VulkanTextureView&>(*view).Native() == before)
                throw std::runtime_error("Vulkan replacement did not preserve logical texture/view identity.");
            dynamic_cast<const VulkanBindingSet&>(*set).RecordDiagnosticUse();
            const auto admitted = dynamic_cast<const VulkanTexture&>(*texture).Native();
            const auto admittedView = dynamic_cast<const VulkanTextureView&>(*view).Native();
            bool rejected = false;
            try { device.ResizeTexture(*texture, UINT32_MAX, UINT32_MAX); }
            catch (const std::out_of_range&) { rejected = true; }
            if (!rejected || texture->Desc().width != 16 || texture->Desc().height != 12
                || dynamic_cast<const VulkanTexture&>(*texture).Native() != admitted
                || dynamic_cast<const VulkanTextureView&>(*view).Native() != admittedView)
                throw std::runtime_error("Vulkan failed resize changed the admitted image or view.");
            device.EndFrame();
            set.reset(); sampler.reset(); view.reset(); texture.reset(); buffer.reset();
            if (state.Retired.Size() == 0)
                throw std::runtime_error("Vulkan resource destruction bypassed retirement.");
            if (device.Statistics().DeviceWideWaits != baseline.DeviceWideWaits)
                throw std::runtime_error("Vulkan resource churn or resize waited for the whole device.");
        }
        commands->End(); commands.reset();
        layout.reset();
        device.WaitIdle(); // Explicit boundary; ordinary churn above must not use it.
        const auto after = device.Statistics();
        if (after.Textures != baseline.Textures || after.Buffers != baseline.Buffers
            || after.Programs != baseline.Programs || after.Retired != 0
            || after.Submitted != after.Completed
            || after.Submitted.Value < baseline.Submitted.Value + 64 * 4)
            throw std::runtime_error("Vulkan submission retirement failed to return to its resource baseline.");
    }

    void CheckUploadReuse(GraphicsDevice& device)
    {
        const auto state = dynamic_cast<VulkanGraphicsDevice&>(device).State();
        const auto initialWaits = device.Statistics().DeviceWideWaits;
        const auto initialSubmitted = state->Scheduler->Submitted().Value;
        std::uint64_t warmedCreations = 0;
        BufferDesc gpuDesc{};
        gpuDesc.size = 256; gpuDesc.usage = BufferUsage::TransferSrc | BufferUsage::TransferDst;
        auto gpu = device.CreateBuffer(gpuDesc);
        TextureDesc textureDesc{};
        textureDesc.width = 3; textureDesc.height = 2; textureDesc.format = TextureFormat::RGBA8Unorm;
        textureDesc.usage = TextureUsage::TransferSrc | TextureUsage::TransferDst | TextureUsage::Sampled;
        auto texture = device.CreateTexture(textureDesc);
        const auto floatChannels = 3U;
        textureDesc.format = TextureFormat::RGB32Float;
        auto floatTexture = device.CreateTexture(textureDesc);
        std::array<std::byte, 256> expected{};
        std::vector<float> floats(6 * floatChannels);
        const auto floatSize = floats.size() * sizeof(float);
        for (unsigned cycle = 0; cycle < 64; ++cycle)
        {
            for (std::size_t i = 0; i < expected.size(); ++i) expected[i] = std::byte((i + cycle * 17) & 255);
            for (std::size_t i = 0; i < floats.size(); ++i) floats[i] = float(i + cycle) * 0.25F;
            device.WriteBuffer(*gpu, 0, expected);
            TextureWrite write{};
            write.data = expected.data(); write.width = 3; write.height = 2; write.format = TextureFormat::RGBA8Unorm;
            device.WriteTexture(*texture, write);
            write.data = floats.data(); write.format = textureDesc.format;
            device.WriteTexture(*floatTexture, write);
            if (cycle == 1) warmedCreations = state->UploadPageCreations;
            else if (cycle > 1 && state->UploadPageCreations != warmedCreations)
                throw std::runtime_error("Vulkan upload fallback kept allocating after warmup.");
        }
        if (device.Statistics().DeviceWideWaits != initialWaits)
            throw std::runtime_error("Vulkan upload fallback added device-wide waits.");
        if (state->Scheduler->Submitted().Value - initialSubmitted > 2)
            throw std::runtime_error("Vulkan small uploads were not batched into a shared stream.");
        const auto readback = [&](std::size_t bytes) {
            BufferDesc desc{}; desc.size = bytes; desc.usage = BufferUsage::TransferDst;
            desc.memoryUsage = MemoryUsage::GpuToCpu; return device.CreateBuffer(desc);
        };
        auto result = readback(512);
        auto textureResult = readback(24);
        auto floatResult = readback(floatSize);
        auto commands = device.CreateCommandList();
        commands->Begin();
        commands->Transition(*gpu, ResourceState::CopyDst, ResourceState::CopySrc);
        commands->Transition(*result, ResourceState::Undefined, ResourceState::CopyDst);
        commands->CopyBuffer(*gpu, 0, *result, 0, 256);
        // Write again inside the same stream, mutating the source CPU memory:
        // the first recorded copy must retain the earlier staged bytes.
        for (auto& byte : expected) byte ^= std::byte{0xA5};
        device.WriteBuffer(*gpu, 0, expected);
        commands->CopyBuffer(*gpu, 0, *result, 256, 256);
        commands->Transition(*texture, ResourceState::ShaderRead, ResourceState::CopySrc);
        commands->Transition(*textureResult, ResourceState::Undefined, ResourceState::CopyDst);
        BufferTextureCopy region{}; region.width = 3; region.height = 2;
        commands->CopyTextureToBuffer(*texture, *textureResult, region);
        commands->Transition(*floatTexture, ResourceState::ShaderRead, ResourceState::CopySrc);
        commands->Transition(*floatResult, ResourceState::Undefined, ResourceState::CopyDst);
        commands->CopyTextureToBuffer(*floatTexture, *floatResult, region);
        commands->End();
        std::array<std::byte, 512> actual{};
        device.ReadBuffer(*result, 0, actual);
        for (std::size_t i = 0; i < expected.size(); ++i)
            if (actual[i] != (expected[i] ^ std::byte{0xA5}) || actual[256 + i] != expected[i])
                throw std::runtime_error("Vulkan upload changed recorded buffer copy order/data.");
        std::array<std::byte, 24> textureBytes{};
        device.ReadBuffer(*textureResult, 0, textureBytes);
        for (std::size_t i = 0; i < textureBytes.size(); ++i)
            if (textureBytes[i] != (expected[i] ^ std::byte{0xA5}))
                throw std::runtime_error("Vulkan fallback texture upload bytes differ.");
        std::vector<std::byte> floatBytes(floatSize);
        device.ReadBuffer(*floatResult, 0, floatBytes);
        if (std::memcmp(floatBytes.data(), floats.data(), floatSize))
            throw std::runtime_error("Vulkan float texture upload alignment/data differs.");
        commands.reset();

        // Native overflow pages as well as the default pages must be reused.
        std::vector<std::byte> large((8U << 20U) + 64, std::byte{0x37});
        gpuDesc.size = large.size();
        auto largeGpu = device.CreateBuffer(gpuDesc);
        for (unsigned cycle = 0; cycle < 4; ++cycle)
        {
            large.front() = std::byte(cycle);
            device.WriteBuffer(*largeGpu, 0, large);
            if (cycle == 1) warmedCreations = state->UploadPageCreations;
            else if (cycle > 1 && state->UploadPageCreations != warmedCreations)
                throw std::runtime_error("Vulkan oversized upload page was not reused.");
        }
        auto largeResult = readback(large.size());
        commands = device.CreateCommandList(); commands->Begin();
        commands->Transition(*largeGpu, ResourceState::CopyDst, ResourceState::CopySrc);
        commands->Transition(*largeResult, ResourceState::Undefined, ResourceState::CopyDst);
        commands->CopyBuffer(*largeGpu, 0, *largeResult, 0, large.size()); commands->End();
        std::vector<std::byte> largeActual(large.size()); device.ReadBuffer(*largeResult, 0, largeActual);
        if (largeActual != large) throw std::runtime_error("Vulkan oversized upload contents differ.");
        std::cout << "[vulkan] upload arena PASS; 64 batched buffer/texture cycles; persistent page reuse; "
            << "recorded copy order; RGB32Float (GPU packing into RGBA32Float)"
            << "; oversized page reuse/readback\n";
    }

    void CheckBindingAllocations(GraphicsDevice& device)
    {
        BufferDesc bufferDesc{};
        bufferDesc.size = 4096;
        bufferDesc.usage = BufferUsage::Uniform | BufferUsage::Storage;
        auto buffer = device.CreateBuffer(bufferDesc);
        TextureDesc textureDesc{};
        textureDesc.width = 4;
        textureDesc.height = 4;
        textureDesc.format = TextureFormat::RGBA8Unorm;
        textureDesc.usage = TextureUsage::Sampled | TextureUsage::Storage;
        auto texture = device.CreateTexture(textureDesc);
        auto view = device.CreateTextureView(*texture, {});
        auto sampler = device.CreateSampler({});
        BindingLayoutDesc layoutDesc{};
        layoutDesc.entries = {
            {0, BindingType::UniformBuffer, ShaderStage::AllGraphics, 2},
            {1, BindingType::StorageBuffer, ShaderStage::Vertex, 1},
            {2, BindingType::SampledTexture, ShaderStage::Fragment, 1},
            {3, BindingType::StorageTexture, ShaderStage::Fragment, 1},
            {4, BindingType::Sampler, ShaderStage::Fragment, 1}};
        auto layout = device.CreateBindingLayout(layoutDesc);
        BindingSetDesc setDesc{};
        setDesc.layout = layout.get();
        setDesc.entries = {
            {0, BufferBinding{buffer.get(), 0, 256}, 0},
            {0, BufferBinding{buffer.get(), 0, 512}, 1},
            {1, BufferBinding{buffer.get(), 0, 4096}},
            {2, TextureBinding{view.get()}},
            {3, TextureBinding{view.get()}},
            {4, SamplerBinding{sampler.get()}}};
        auto set = device.CreateBindingSet(setDesc);
        auto invalid = setDesc;
        invalid.entries[1].arrayElement = 0;
        bool rejected = false;
        try { auto unexpected = device.CreateBindingSet(invalid); }
        catch (const std::invalid_argument&) { rejected = true; }
        if (!rejected) throw std::runtime_error("Vulkan duplicate descriptor array element was accepted.");
        invalid = setDesc;
        invalid.entries[0].resource = BufferBinding{buffer.get(), 4096, 1};
        rejected = false;
        try { auto unexpected = device.CreateBindingSet(invalid); }
        catch (const std::invalid_argument&) { rejected = true; }
        if (!rejected) throw std::runtime_error("Vulkan out-of-range buffer descriptor was accepted.");
        const auto alignment = dynamic_cast<const VulkanBindingSet&>(*set).UniformOffsetAlignment();
        if (alignment > 1)
        {
            invalid = setDesc;
            invalid.entries[0].resource = BufferBinding{buffer.get(), 1, 256};
            rejected = false;
            try { auto unexpected = device.CreateBindingSet(invalid); }
            catch (const std::invalid_argument&) { rejected = true; }
            if (!rejected) throw std::runtime_error("Vulkan misaligned uniform descriptor was accepted.");
        }
        invalid = setDesc;
        invalid.entries[0].resource = SamplerBinding{sampler.get()};
        rejected = false;
        try { auto unexpected = device.CreateBindingSet(invalid); }
        catch (const std::invalid_argument&) { rejected = true; }
        if (!rejected) throw std::runtime_error("Vulkan mismatched descriptor type was accepted.");
        for (int frame = 0; frame < 8; ++frame)
        {
            (void)device.BeginFrame();
            dynamic_cast<const VulkanBindingSet&>(*set).RecordDiagnosticUse();
            const auto first = dynamic_cast<const VulkanBindingSet&>(*set).Native();
            const auto second = dynamic_cast<const VulkanBindingSet&>(*set).Native();
            if (!first || !second || first == second)
                throw std::runtime_error("Vulkan descriptor update reused a live set.");
            // Exceed a pool's set capacity, then cycle both frame slots so
            // overflow pages are also reset and reused after their fences.
            for (int allocation = 0; allocation < 1050; ++allocation)
                (void)dynamic_cast<const VulkanBindingSet&>(*set).Native();
            if (!dynamic_cast<const VulkanBindingSet&>(*set).FrameHasOverflowPools())
                throw std::runtime_error("Vulkan descriptor diagnostic did not exercise pool growth.");
            device.EndFrame();
        }
        device.WaitIdle();
    }

    void CheckShaderModules(GraphicsDevice& device)
    {
        const auto baseline = device.Statistics().Shaders;
        auto check = [&](const auto& words, ShaderStage stage)
        {
            ShaderDesc desc{};
            desc.stage = stage;
            desc.code.resize(words.size() * sizeof(std::uint32_t));
            std::memcpy(desc.code.data(), words.data(), desc.code.size());
            {
                auto shader = device.CreateShader(desc);
                if (device.Statistics().Shaders != baseline + 1)
                    throw std::runtime_error("Vulkan shader module accounting mismatch.");
            }
            if (device.Statistics().Shaders != baseline)
                throw std::runtime_error("Vulkan shader module leaked.");
            auto reject = [&](const ShaderDesc& invalid)
            {
                bool rejected = false;
                try { auto shader = device.CreateShader(invalid); }
                catch (const std::invalid_argument&) { rejected = true; }
                if (!rejected) throw std::runtime_error("Vulkan invalid shader description accepted.");
            };
            auto invalid = desc;
            invalid.entryPoint = "missing_entry_point";
            reject(invalid);
            invalid = desc;
            invalid.stage = stage == ShaderStage::Vertex ? ShaderStage::Fragment : ShaderStage::Vertex;
            reject(invalid);
            invalid = desc;
            invalid.code.pop_back();
            reject(invalid);
            invalid = desc;
            invalid.format = ShaderCodeFormat::GlslSource;
            reject(invalid);
        };
        check(Generated::main_vert, ShaderStage::Vertex);
        check(Generated::main_frag, ShaderStage::Fragment);
        check(Generated::composite_vert, ShaderStage::Vertex);
        check(Generated::composite_frag, ShaderStage::Fragment);
        check(Generated::cel_vert, ShaderStage::Vertex);
        check(Generated::cel_frag, ShaderStage::Fragment);
        check(Generated::shift_vert, ShaderStage::Vertex);
        check(Generated::shift_frag, ShaderStage::Fragment);
    }

    void CheckGraphicsPipelines(GraphicsDevice& device)
    {
        auto check = [&](const auto& vertexWords, const auto& fragmentWords,
            const auto& textures, const auto& blocks, bool main)
        {
            auto shader = [&](const auto& words, ShaderStage stage) {
                ShaderDesc desc{};
                desc.stage = stage;
                desc.code.resize(words.size() * sizeof(std::uint32_t));
                std::memcpy(desc.code.data(), words.data(), desc.code.size());
                return device.CreateShader(desc);
            };
            auto vertex = shader(vertexWords, ShaderStage::Vertex);
            auto fragment = shader(fragmentWords, ShaderStage::Fragment);
            std::array<BindingLayoutDesc, SceneShaderAbi::GroupCount> layoutDescs{};
            for (const auto& block : blocks)
                if (!block.small) layoutDescs.at(block.group).entries.push_back({block.binding, BindingType::UniformBuffer, ShaderStage::AllGraphics, 1});
            for (const auto& texture : textures)
            {
                layoutDescs.at(texture.group).entries.push_back({texture.image, BindingType::SampledTexture, ShaderStage::AllGraphics, 1});
                layoutDescs.at(texture.group).entries.push_back({texture.sampler, BindingType::Sampler, ShaderStage::AllGraphics, 1});
            }
            std::array<std::unique_ptr<BindingLayout>, SceneShaderAbi::GroupCount> layouts;
            GraphicsPipelineDesc desc{};
            desc.vertexShader = vertex.get(); desc.fragmentShader = fragment.get();
            desc.smallConstantBytes = main ? sizeof(CommandList::SmallDrawConstants) : 0;
            for (std::uint32_t group = 0; group < SceneShaderAbi::GroupCount; ++group)
            {
                layouts[group] = device.CreateBindingLayout(layoutDescs[group]);
                desc.pipelineLayout.groups.push_back(layouts[group]->Desc());
            }
            desc.colorFormats = {TextureFormat::RGBA8Unorm};
            desc.blendAttachments.resize(1);
            desc.vertexBuffers = {{0, 56, VertexInputRate::Vertex}};
            desc.vertexAttributes = {{0, 0, VertexFormat::Float4, 0}, {3, 0, VertexFormat::Float3, 44}};
            if (main)
            {
                desc.vertexAttributes.push_back({1, 0, VertexFormat::Float3, 16});
                desc.vertexAttributes.push_back({2, 0, VertexFormat::Float4, 28});
                desc.depthStencilFormat = TextureFormat::D24UnormS8Uint;
                desc.depthStencil.depthTestEnable = true; desc.depthStencil.depthWriteEnable = true;
            }
            auto pipeline = device.CreateGraphicsPipeline(desc);
            auto executableState = desc;
            executableState.vertexShader = executableState.fragmentShader = nullptr;
            if (pipeline->Desc() != executableState) throw std::runtime_error("Vulkan pipeline state changed or retained borrowed shaders.");
            auto commands = device.CreateCommandList();
            std::vector<std::unique_ptr<Buffer>> constants;
            std::array<BindingSetDesc, SceneShaderAbi::GroupCount> setDescs;
            for (std::uint32_t group = 0; group < SceneShaderAbi::GroupCount; ++group)
                setDescs[group].layout = layouts[group].get();
            for (const auto& block : blocks)
            {
                if (block.small) continue;
                BufferDesc bufferDesc{};
                bufferDesc.size = block.size; bufferDesc.usage = BufferUsage::Uniform;
                auto buffer = device.CreateBuffer(bufferDesc);
                setDescs[block.group].entries.push_back({block.binding, BufferBinding{buffer.get(), 0, block.size}});
                constants.push_back(std::move(buffer));
            }
            TextureDesc textureDesc{};
            textureDesc.width = 4; textureDesc.height = 4; textureDesc.format = TextureFormat::RGBA8Unorm;
            textureDesc.usage = TextureUsage::Sampled;
            auto texture = device.CreateTexture(textureDesc);
            auto view = device.CreateTextureView(*texture, {});
            auto sampler = device.CreateSampler({});
            for (const auto& binding : textures)
            {
                setDescs[binding.group].entries.push_back({binding.image, TextureBinding{view.get()}});
                setDescs[binding.group].entries.push_back({binding.sampler, SamplerBinding{sampler.get()}});
            }
            std::array<std::unique_ptr<BindingSet>, SceneShaderAbi::GroupCount> sets;
            for (std::uint32_t group = 0; group < SceneShaderAbi::GroupCount; ++group)
                sets[group] = device.CreateBindingSet(setDescs[group]);
            const auto incompatibleGroup = blocks.front().group;
            auto incompatibleDesc = layoutDescs[incompatibleGroup];
            incompatibleDesc.entries[0].stages = ShaderStage::Vertex;
            auto incompatibleLayout = device.CreateBindingLayout(incompatibleDesc);
            auto incompatibleSetDesc = setDescs[incompatibleGroup];
            incompatibleSetDesc.layout = incompatibleLayout.get();
            auto incompatibleSet = device.CreateBindingSet(incompatibleSetDesc);
            (void)device.BeginFrame();
            if (commands->QueryReadiness() != CommandListReadiness::Ready || !commands->TryBegin())
                throw std::runtime_error("Idle command list was not admitted nonblocking.");
            bool nestedRejected = false;
            try { (void)commands->TryBegin(); } catch (const std::logic_error&) { nestedRejected = true; }
            if (!nestedRejected) throw std::runtime_error("Nested nonblocking Begin was accepted.");
            commands->SetPipeline(*pipeline);
            if (main) commands->SetSmallConstants({0.5F, 1, {}});
            bool rejected = false;
            try { commands->SetBindingSet(incompatibleGroup, *incompatibleSet); }
            catch (const std::invalid_argument&) { rejected = true; }
            if (!rejected) throw std::runtime_error("Vulkan incompatible binding layout accepted.");
            for (std::uint32_t group = 0; group < SceneShaderAbi::GroupCount; ++group)
                commands->SetBindingSet(group, *sets[group]);
            commands->End(); device.EndFrame(); device.WaitIdle();
            // Native shaders now use the four actual logical groups. Change
            // an unused entry to prove the cache includes every group layout.
            auto grouped = desc;
            grouped.pipelineLayout.groups[0].entries.push_back({99, BindingType::UniformBuffer, ShaderStage::AllGraphics, 1});
            auto groupedPipeline = device.CreateGraphicsPipeline(grouped);
            if (dynamic_cast<const VulkanGraphicsPipeline&>(*pipeline).Native()
                == dynamic_cast<const VulkanGraphicsPipeline&>(*groupedPipeline).Native())
                throw std::runtime_error("Vulkan pipeline cache ignored binding groups.");
            auto groupedLayout = device.CreateBindingLayout(grouped.pipelineLayout.groups[0]);
            auto groupedSetDesc = setDescs[0]; groupedSetDesc.layout = groupedLayout.get();
            groupedSetDesc.entries.push_back({99, BufferBinding{constants.front().get(), 0, blocks.front().size}});
            auto groupedSet = device.CreateBindingSet(groupedSetDesc);
            (void)device.BeginFrame();
            commands->Begin(); commands->SetPipeline(*groupedPipeline);
            commands->SetBindingSet(0, *groupedSet);
            for (std::uint32_t group = 1; group < SceneShaderAbi::GroupCount; ++group)
                commands->SetBindingSet(group, *sets[group]);
            rejected = false;
            try { commands->SetBindingSet(SceneShaderAbi::GroupCount, *sets[0]); }
            catch (const std::invalid_argument&) { rejected = true; }
            if (!rejected) throw std::runtime_error("Vulkan out-of-range binding group accepted.");
            commands->End(); device.EndFrame(); device.WaitIdle();
            auto reused = device.CreateGraphicsPipeline(desc);
            if (dynamic_cast<const VulkanGraphicsPipeline&>(*pipeline).Native()
                != dynamic_cast<const VulkanGraphicsPipeline&>(*reused).Native())
                throw std::runtime_error("Vulkan identical pipeline was not cached.");
            auto replacementVertex = shader(vertexWords, ShaderStage::Vertex);
            auto replacementFragment = shader(fragmentWords, ShaderStage::Fragment);
            auto replacementLayout = device.CreateBindingLayout(layoutDescs[0]);
            auto replacement = desc;
            replacement.vertexShader = replacementVertex.get(); replacement.fragmentShader = replacementFragment.get();
            replacement.pipelineLayout.groups[0] = replacementLayout->Desc();
            auto equivalent = device.CreateGraphicsPipeline(replacement);
            if (equivalent->Desc() != executableState || dynamic_cast<const VulkanGraphicsPipeline&>(*pipeline).Native()
                != dynamic_cast<const VulkanGraphicsPipeline&>(*equivalent).Native())
                throw std::runtime_error("Vulkan content-equivalent pipeline was not cached.");
            auto different = desc;
            different.rasterizer.cullMode = CullMode::None;
            auto distinct = device.CreateGraphicsPipeline(different);
            if (dynamic_cast<const VulkanGraphicsPipeline&>(*pipeline).Native()
                == dynamic_cast<const VulkanGraphicsPipeline&>(*distinct).Native())
                throw std::runtime_error("Vulkan different pipeline state aliased in cache.");
            auto reject = [&](const GraphicsPipelineDesc& invalid) {
                bool rejected = false;
                try { auto unexpected = device.CreateGraphicsPipeline(invalid); }
                catch (const std::invalid_argument&) { rejected = true; }
                if (!rejected) throw std::runtime_error("Vulkan invalid pipeline description accepted.");
            };
            auto invalid = desc; invalid.sampleCount = 3; reject(invalid);
            invalid = desc; invalid.vertexBuffers.clear(); reject(invalid);
            invalid = desc; invalid.blendAttachments.clear(); reject(invalid);
            invalid = desc; invalid.pipelineLayout.groups.clear(); reject(invalid);
            invalid = desc; invalid.rasterizer.depthBiasSlope = std::numeric_limits<float>::quiet_NaN(); reject(invalid);
            if (main) { invalid = desc; invalid.depthStencilFormat = TextureFormat::RGBA8Unorm; reject(invalid); }
        };
        check(Generated::main_vert, Generated::main_frag, Generated::main_textures, Generated::main_blocks, true);
        check(Generated::composite_vert, Generated::composite_frag, Generated::composite_textures, Generated::composite_blocks, false);
        check(Generated::cel_vert, Generated::cel_frag, Generated::cel_textures, Generated::cel_blocks, false);
        check(Generated::shift_vert, Generated::shift_frag, Generated::shift_textures, Generated::shift_blocks, false);
        dynamic_cast<VulkanGraphicsDevice&>(device).ClearPipelineCacheForCheck();
        if (device.Statistics().Programs != 0) throw std::runtime_error("Vulkan pipeline cache failed to release programs.");
    }

    std::unique_ptr<GraphicsDevice> CreateGraphicsDevice(Context& context)
    {
        return std::make_unique<VulkanGraphicsDevice>(context);
    }

    std::function<void()> SessionReleaseCheck(GraphicsDevice& device)
    {
        const auto state = dynamic_cast<VulkanGraphicsDevice&>(device).State();
        state->RequireAlive();
        if (!state->Buffers || !state->Textures || !state->Shaders || !state->Programs || !state->Samplers || !state->CommandLists)
            throw std::logic_error("Vulkan release check needs a live fixture of each resource kind.");
        const auto submitted = state->Scheduler->Submitted();
        return [state, submitted] {
            if (state->ContextPointer || state->Allocator || state->Scheduler || !state->NativeOwners.empty()
                || state->Buffers || state->Textures || state->Shaders || state->Programs || state->Samplers
                || state->CommandLists || state->UploadCommandLists || state->UploadPages || state->ScratchPages
                || state->Retired.Size() || state->OutstandingAllocationsAtShutdown
                || (state->PipelineCache && state->PipelineCache->Stats().Native)
                || state->FinalCompleted != state->FinalSubmitted || state->FinalSubmitted <= submitted)
                throw std::logic_error("Vulkan session retained native resources or failed to complete pending work.");
            if (state->Memory->Allocator() || state->Memory->Telemetry().PendingRequests || state->Memory->Telemetry().PendingBytes)
                throw std::logic_error("Vulkan session retained native memory or pending admission.");
            if (state->Frames->NativeObjects())
                throw std::logic_error("Vulkan session retained frame fences, descriptor pages or transients.");
        };
    }
}
#else
namespace MphRead::NativeRuntime::Rhi::Vulkan
{
    std::unique_ptr<GraphicsDevice> CreateGraphicsDevice(Context&)
    {
        throw std::runtime_error("Desktop Vulkan development support was not built.");
    }
    PresentResult PresentWindow(GraphicsDevice&, Swapchain&)
    {
        throw std::runtime_error("Desktop Vulkan development support was not built.");
    }
    InteropDevice DescribeDevice(GraphicsDevice&)
    {
        throw std::runtime_error("Desktop Vulkan development support was not built.");
    }
    void FlushDevice(GraphicsDevice&) {}
    void CheckMemoryAdmission(GraphicsDevice&) { throw std::runtime_error("Vulkan development support was not built."); }
    std::uint32_t EmbeddedShaderStages() noexcept { return 0; }
    InteropImage PrepareForExternal(GraphicsDevice&, Texture&, ResourceState)
    {
        throw std::runtime_error("Desktop Vulkan development support was not built.");
    }
    void BeginExternalSubmit(GraphicsDevice&) {}

    void AdoptExternalState(Texture&, ResourceState) {}
    std::function<void()> SessionReleaseCheck(GraphicsDevice&)
    { throw std::runtime_error("Vulkan development support was not built."); }
}
#endif
