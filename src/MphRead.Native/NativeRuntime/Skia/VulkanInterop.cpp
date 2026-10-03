#include "VulkanInterop.hpp"

#include "../Rhi/SceneBackend.hpp"

#if defined(FRUITY_SKIA_VULKAN) && defined(FRUITY_HAS_VULKAN) && !defined(__ANDROID__)
#include "../Rhi/Vulkan/VulkanGraphicsDevice.hpp"

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <include/core/SkColorSpace.h>
#include <include/core/SkSurface.h>
#include <include/gpu/GpuTypes.h>
#include <include/gpu/MutableTextureState.h>
#include <include/gpu/ganesh/GrBackendSurface.h>
#include <include/gpu/ganesh/GrDirectContext.h>
#include <include/gpu/ganesh/SkSurfaceGanesh.h>
#include <include/gpu/ganesh/vk/GrVkBackendSurface.h>
#include <include/gpu/ganesh/vk/GrVkDirectContext.h>
#include <include/gpu/ganesh/vk/GrVkTypes.h>
#include <include/gpu/vk/VulkanBackendContext.h>
#include <include/gpu/vk/VulkanExtensions.h>
#include <include/gpu/vk/VulkanMemoryAllocator.h>
#include <include/gpu/vk/VulkanMutableTextureState.h>
#include <include/gpu/vk/VulkanTypes.h>

#include <vk_mem_alloc.h>
#endif

#include <stdexcept>

namespace MphRead::NativeRuntime::Skia::VulkanInterop
{
    Target::Target() = default;
    Target::~Target() = default;
    Target::Target(Target&&) noexcept = default;
    Target& Target::operator=(Target&&) noexcept = default;

#if defined(FRUITY_SKIA_VULKAN) && defined(FRUITY_HAS_VULKAN) && !defined(__ANDROID__)
    namespace Rhi = ::MphRead::NativeRuntime::Rhi;

    bool Available() noexcept { return true; }

    namespace
    {
        // Skia requires its client to allocate its memory (the VMA-based
        // allocator it ships is not exported from the DLL). This is that
        // allocator, on VMA over the RHI's own device: the same library and
        // the same memory-type choices Skia's makes.
        class VmaSkiaAllocator final : public skgpu::VulkanMemoryAllocator
        {
        public:
            explicit VmaSkiaAllocator(const Rhi::Vulkan::InteropDevice& device)
            {
                VmaVulkanFunctions functions{};
                functions.vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(device.GetInstanceProcAddr);
                functions.vkGetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(device.GetDeviceProcAddr);
                VmaAllocatorCreateInfo create{};
                create.instance = reinterpret_cast<VkInstance>(device.Instance);
                create.physicalDevice = reinterpret_cast<VkPhysicalDevice>(device.PhysicalDevice);
                create.device = reinterpret_cast<VkDevice>(device.Device);
                create.vulkanApiVersion = device.ApiVersion;
                create.pVulkanFunctions = &functions;
                if (vmaCreateAllocator(&create, &_allocator) != VK_SUCCESS)
                    throw std::runtime_error("VMA could not create Skia's Vulkan memory allocator.");
            }
            ~VmaSkiaAllocator() override { vmaDestroyAllocator(_allocator); }

            VkResult allocateImageMemory(VkImage image, uint32_t flags, skgpu::VulkanBackendMemory* memory) override
            {
                VmaAllocationCreateInfo info{};
                info.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
                if (flags & kDedicatedAllocation_AllocationPropertyFlag)
                    info.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
                if (flags & kLazyAllocation_AllocationPropertyFlag)
                    info.preferredFlags |= VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;
                VmaAllocation allocation = VK_NULL_HANDLE;
                const VkResult result = vmaAllocateMemoryForImage(_allocator, image, &info, &allocation, nullptr);
                *memory = reinterpret_cast<skgpu::VulkanBackendMemory>(allocation);
                return result;
            }

            VkResult allocateBufferMemory(VkBuffer buffer, BufferUsage usage, uint32_t flags,
                skgpu::VulkanBackendMemory* memory) override
            {
                VmaAllocationCreateInfo info{};
                switch (usage)
                {
                case BufferUsage::kGpuOnly:
                    info.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
                    break;
                case BufferUsage::kCpuWritesGpuReads:
                    info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
                    info.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
                    break;
                case BufferUsage::kTransfersFromCpuToGpu:
                    info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
                    break;
                case BufferUsage::kTransfersFromGpuToCpu:
                    info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
                    info.preferredFlags = VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
                    break;
                }
                if (flags & kDedicatedAllocation_AllocationPropertyFlag)
                    info.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
                if ((flags & kPersistentlyMapped_AllocationPropertyFlag) && usage != BufferUsage::kGpuOnly)
                    info.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
                VmaAllocation allocation = VK_NULL_HANDLE;
                const VkResult result = vmaAllocateMemoryForBuffer(_allocator, buffer, &info, &allocation, nullptr);
                *memory = reinterpret_cast<skgpu::VulkanBackendMemory>(allocation);
                return result;
            }

            void getAllocInfo(const skgpu::VulkanBackendMemory& memory, skgpu::VulkanAlloc* alloc) const override
            {
                const auto allocation = reinterpret_cast<VmaAllocation>(memory);
                VmaAllocationInfo info{};
                vmaGetAllocationInfo(_allocator, allocation, &info);
                VkMemoryPropertyFlags properties = 0;
                vmaGetMemoryTypeProperties(_allocator, info.memoryType, &properties);
                uint32_t flags = 0;
                if (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
                {
                    flags |= skgpu::VulkanAlloc::kMappable_Flag;
                    if (!(properties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
                        flags |= skgpu::VulkanAlloc::kNoncoherent_Flag;
                }
                if (properties & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT)
                    flags |= skgpu::VulkanAlloc::kLazilyAllocated_Flag;
                alloc->fMemory = info.deviceMemory;
                alloc->fOffset = info.offset;
                alloc->fSize = info.size;
                alloc->fFlags = flags;
                alloc->fBackendMemory = memory;
            }

            VkResult mapMemory(const skgpu::VulkanBackendMemory& memory, void** data) override
            {
                return vmaMapMemory(_allocator, reinterpret_cast<VmaAllocation>(memory), data);
            }
            void unmapMemory(const skgpu::VulkanBackendMemory& memory) override
            {
                vmaUnmapMemory(_allocator, reinterpret_cast<VmaAllocation>(memory));
            }
            VkResult flushMemory(const skgpu::VulkanBackendMemory& memory, VkDeviceSize offset,
                VkDeviceSize size) override
            {
                return vmaFlushAllocation(_allocator, reinterpret_cast<VmaAllocation>(memory), offset, size);
            }
            VkResult invalidateMemory(const skgpu::VulkanBackendMemory& memory, VkDeviceSize offset,
                VkDeviceSize size) override
            {
                return vmaInvalidateAllocation(_allocator, reinterpret_cast<VmaAllocation>(memory), offset, size);
            }
            void freeMemory(const skgpu::VulkanBackendMemory& memory) override
            {
                vmaFreeMemory(_allocator, reinterpret_cast<VmaAllocation>(memory));
            }
            std::pair<uint64_t, uint64_t> totalAllocatedAndUsedMemory() const override
            {
                VmaTotalStatistics statistics{};
                vmaCalculateStatistics(_allocator, &statistics);
                return {statistics.total.statistics.blockBytes, statistics.total.statistics.allocationBytes};
            }

        private:
            VmaAllocator _allocator = VK_NULL_HANDLE;
        };
    }

    bool Active() noexcept
    {
        return Rhi::ScenePresentsWindow();
    }

    sk_sp<GrDirectContext> MakeContext()
    {
        const Rhi::Vulkan::InteropDevice device = Rhi::Vulkan::DescribeDevice(Rhi::SceneDevice());
        const auto getInstance = reinterpret_cast<PFN_vkGetInstanceProcAddr>(device.GetInstanceProcAddr);
        const auto getDevice = reinterpret_cast<PFN_vkGetDeviceProcAddr>(device.GetDeviceProcAddr);
        skgpu::VulkanBackendContext backend{};
        backend.fInstance = reinterpret_cast<VkInstance>(device.Instance);
        backend.fPhysicalDevice = reinterpret_cast<VkPhysicalDevice>(device.PhysicalDevice);
        backend.fDevice = reinterpret_cast<VkDevice>(device.Device);
        backend.fQueue = reinterpret_cast<VkQueue>(device.Queue);
        backend.fGraphicsQueueIndex = device.QueueFamily;
        backend.fMaxAPIVersion = device.ApiVersion;
        // The RHI enables its own feature set; Skia is told of none and asks
        // for none, so it needs nothing the device does not have.
        static const skgpu::VulkanExtensions extensions;
        backend.fVkExtensions = &extensions;
        backend.fMemoryAllocator = sk_make_sp<VmaSkiaAllocator>(device);
        backend.fGetProc = [getInstance, getDevice](const char* name, VkInstance instance, VkDevice vkDevice)
        {
            if (vkDevice != VK_NULL_HANDLE)
            {
                if (PFN_vkVoidFunction function = getDevice(vkDevice, name)) return function;
            }
            return getInstance(instance, name);
        };
        sk_sp<GrDirectContext> context = GrDirectContexts::MakeVulkan(backend);
        if (!context) throw std::runtime_error("Skia could not create a Ganesh Vulkan context on the RHI device.");
        return context;
    }

    sk_sp<SkSurface> MakeSurface(GrDirectContext& context, Target& target, std::int32_t width, std::int32_t height)
    {
        auto& gpu = Rhi::SceneDevice();
        target.Texture = gpu.CreateTexture(Rhi::TextureDesc{static_cast<std::uint32_t>(width),
            static_cast<std::uint32_t>(height), 1, 1, 1, 1, Rhi::TextureFormat::RGBA8Unorm,
            Rhi::TextureUsage::Sampled | Rhi::TextureUsage::ColorAttachment
                | Rhi::TextureUsage::TransferSrc | Rhi::TextureUsage::TransferDst});
        target.Width = width;
        target.Height = height;
        const Rhi::Vulkan::InteropImage image
            = Rhi::Vulkan::PrepareForExternal(gpu, *target.Texture, Rhi::ResourceState::ColorAttachment);
        GrVkImageInfo info{};
        info.fImage = reinterpret_cast<VkImage>(image.Image);
        info.fImageTiling = VK_IMAGE_TILING_OPTIMAL;
        info.fImageLayout = static_cast<VkImageLayout>(image.Layout);
        info.fFormat = static_cast<VkFormat>(image.Format);
        info.fImageUsageFlags = image.Usage;
        info.fSampleCount = 1;
        info.fLevelCount = 1;
        info.fCurrentQueueFamily = VK_QUEUE_FAMILY_IGNORED;
        info.fSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        const GrBackendTexture backend = GrBackendTextures::MakeVk(width, height, info);
        // Top row first, as every texture this renderer uploads is.
        sk_sp<SkSurface> surface = SkSurfaces::WrapBackendTexture(&context, backend, kTopLeft_GrSurfaceOrigin,
            1, kRGBA_8888_SkColorType, nullptr, nullptr);
        if (!surface) throw std::runtime_error("Skia could not wrap the RHI Vulkan image as a surface.");
        return surface;
    }

    void BeginFrame(Target& target)
    {
        if (!target.Texture)
        {
            Rhi::Vulkan::FlushDevice(Rhi::SceneDevice());
            return;
        }
        // Back to a colour attachment (the composite left it ShaderRead), with every
        // earlier RHI submission ahead of Skia's.
        (void)Rhi::Vulkan::PrepareForExternal(Rhi::SceneDevice(), *target.Texture, Rhi::ResourceState::ColorAttachment);
    }

    void EndFrame(GrDirectContext& context, SkSurface& surface, Target& target)
    {
        const skgpu::MutableTextureState attachment
            = skgpu::MutableTextureStates::MakeVulkan(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_QUEUE_FAMILY_IGNORED);
        Rhi::Vulkan::BeginExternalSubmit(Rhi::SceneDevice());
        context.flush(&surface, GrFlushInfo{}, &attachment);
        context.submit(GrSyncCpu::kYes);
        if (target.Texture) Rhi::Vulkan::AdoptExternalState(*target.Texture, Rhi::ResourceState::ColorAttachment);
    }
#else
    bool Available() noexcept { return false; }
    bool Active() noexcept { return false; }
#endif
}
