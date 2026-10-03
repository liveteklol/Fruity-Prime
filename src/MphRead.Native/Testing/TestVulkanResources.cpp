#include "../NativeRuntime/Rhi/Vulkan/VulkanResources.hpp"
#include <array>
#include <iostream>
#include <set>
#include <stdexcept>
#include <type_traits>

namespace
{
    using namespace MphRead::NativeRuntime::Rhi;
    using namespace MphRead::NativeRuntime::Rhi::Vulkan;
    void Expect(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
    template<class T> T Handle(std::uint64_t value)
    { if constexpr (std::is_pointer_v<T>) return reinterpret_cast<T>(value); else return static_cast<T>(value); }
    template<class F> void Reject(F action)
    { bool rejected = false; try { action(); } catch (const std::exception&) { rejected = true; } Expect(rejected, "Invalid creation succeeded."); }
    struct Fake;
    Fake* active = nullptr;
    struct Fake final
    {
        std::set<VkBuffer> Buffers;
        std::set<VkImage> Images;
        std::set<VkImageView> Views;
        std::set<VkSampler> Samplers;
        std::array<std::byte, 16> Mapped{};
        VkBufferCreateInfo BufferInfo{};
        VkImageCreateInfo ImageInfo{};
        VkImageViewCreateInfo ViewInfo{};
        VkSamplerCreateInfo SamplerInfo{};
        VmaAllocationCreateInfo AllocationInfo{};
        VkFormatFeatureFlags Features = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT
            | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
        std::uint64_t Next = 100;
        unsigned Allocations = 0, NativeCreates = 0, Queries = 0, Destroys = 0;
        bool FailAllocation = false, FailNative = false, FailName = false, Unmapped = false, UnsupportedFormat = false;
        Fake() { active = this; }
        ~Fake() { active = nullptr; }
        unsigned Live() const { return Buffers.size() + Images.size() + Views.size() + Samplers.size(); }
        static void Check(VkResult result, const char*)
        { if (result != VK_SUCCESS) throw std::runtime_error("Injected native error."); }
        static VKAPI_ATTR VkResult VKAPI_CALL ImageProperties(VkPhysicalDevice, VkFormat, VkImageType,
            VkImageTiling, VkImageUsageFlags, VkImageCreateFlags, VkImageFormatProperties* out)
        {
            ++active->Queries; if (active->UnsupportedFormat) return VK_ERROR_FORMAT_NOT_SUPPORTED;
            *out = {{64, 64, 32}, 7, 4, VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_4_BIT, 1U << 24}; return VK_SUCCESS;
        }
        static VKAPI_ATTR void VKAPI_CALL FormatProperties(VkPhysicalDevice, VkFormat, VkFormatProperties* out)
        { ++active->Queries; *out = {}; out->optimalTilingFeatures = active->Features; }
        static VKAPI_ATTR VkResult VKAPI_CALL CreateView(VkDevice, const VkImageViewCreateInfo* info,
            const VkAllocationCallbacks*, VkImageView* name)
        {
            ++active->NativeCreates; if (active->FailNative) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
            active->ViewInfo = *info; *name = Handle<VkImageView>(++active->Next); active->Views.insert(*name); return VK_SUCCESS;
        }
        static VKAPI_ATTR void VKAPI_CALL DestroyView(VkDevice, VkImageView name, const VkAllocationCallbacks*)
        { Expect(active->Views.erase(name) == 1, "View destroyed twice."); ++active->Destroys; }
        static VKAPI_ATTR VkResult VKAPI_CALL CreateSampler(VkDevice, const VkSamplerCreateInfo* info,
            const VkAllocationCallbacks*, VkSampler* name)
        {
            ++active->NativeCreates; if (active->FailNative) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
            active->SamplerInfo = *info; *name = Handle<VkSampler>(++active->Next); active->Samplers.insert(*name); return VK_SUCCESS;
        }
        static VKAPI_ATTR void VKAPI_CALL DestroySampler(VkDevice, VkSampler name, const VkAllocationCallbacks*)
        { Expect(active->Samplers.erase(name) == 1, "Sampler destroyed twice."); ++active->Destroys; }
        VulkanResources::Dispatch Dispatch()
        {
            return {Handle<VkPhysicalDevice>(1), Handle<VkDevice>(2), ImageProperties, FormatProperties,
                CreateView, DestroyView, CreateSampler, DestroySampler, Check,
                [this](const VkBufferCreateInfo& info, const VmaAllocationCreateInfo& allocation) {
                    ++Allocations; if (FailAllocation) throw std::runtime_error("Buffer admission failure.");
                    BufferInfo = info; AllocationInfo = allocation;
                    auto name = Handle<VkBuffer>(++Next); Buffers.insert(name);
                    return VulkanResources::BufferStorage{name, Handle<VmaAllocation>(Next), Unmapped ? nullptr : Mapped.data()};
                },
                [this](VulkanResources::BufferStorage storage) { Expect(Buffers.erase(storage.Buffer) == 1, "Buffer destroyed twice."); ++Destroys; },
                [this](const VkImageCreateInfo& info, const VmaAllocationCreateInfo& allocation) {
                    ++Allocations; if (FailAllocation) throw std::runtime_error("Image admission failure.");
                    ImageInfo = info; AllocationInfo = allocation;
                    auto name = Handle<VkImage>(++Next); Images.insert(name);
                    return VulkanResources::ImageStorage{name, Handle<VmaAllocation>(Next)};
                },
                [this](VulkanResources::ImageStorage storage) { Expect(Images.erase(storage.Image) == 1, "Image destroyed twice."); ++Destroys; },
                [this](VkObjectType, std::uint64_t, const char*) { if (FailName) throw std::runtime_error("Debug naming failure."); }};
        }
    };

    void Run()
    {
        Fake f; Capabilities caps{}; caps.maxTexture2DDimension = 64; caps.maxTextureArrayLayers = 4; caps.maxTextureMipLevels = 7; caps.maxTexture3DDimension = 64;
        caps.supportsAnisotropy = true; caps.maxSamplerAnisotropy = 8;
        auto dispatch = f.Dispatch(); VulkanResources resources(dispatch, caps);
        BufferDesc buffer{16, BufferUsage::Vertex | BufferUsage::TransferSrc, MemoryUsage::CpuToGpu};
        auto nativeBuffer = resources.CreateBuffer(buffer);
        Expect(nativeBuffer.Mapped == f.Mapped.data() && f.BufferInfo.size == 16
            && f.BufferInfo.usage == (VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT)
            && (f.AllocationInfo.flags & VMA_ALLOCATION_CREATE_MAPPED_BIT)
            && (f.AllocationInfo.flags & VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT), "Upload buffer policy differs.");
        dispatch.DestroyBuffer(nativeBuffer);
        f.Unmapped = true; Reject([&] { (void)resources.CreateBuffer(buffer); }); f.Unmapped = false;
        Expect(!f.Live(), "Missing persistent mapping leaked native storage.");
        TextureDesc image{}; image.width = image.height = 8; image.mipLevels = 4;
        image.format = TextureFormat::RGB8Unorm; image.usage = TextureUsage::Sampled | TextureUsage::ColorAttachment;
        const auto before = f.Allocations;
        for (unsigned kind = 0; kind < 10; ++kind)
        {
            auto invalid = image;
            switch (kind)
            {
            case 0: invalid.width = 0; break;
            case 1: invalid.mipLevels = 5; break;
            case 2: invalid.arrayLayers = 5; break;
            case 3: invalid.width = 65; break;
            case 4: invalid.depth = 2; invalid.arrayLayers = 2; break;
            case 5: invalid.memoryUsage = MemoryUsage::CpuToGpu; break;
            case 6: invalid.initialState = ResourceState::CopySrc; break;
            case 7: invalid.format = TextureFormat::D32Float; break;
            case 8: invalid.sampleCount = 4; invalid.usage = TextureUsage::TransferSrc; break;
            case 9: invalid.sampleCount = 2; break;
            }
            Reject([&] { (void)resources.CreateImage(invalid); });
        }
        f.UnsupportedFormat = true; Reject([&] { (void)resources.CreateImage(image); }); f.UnsupportedFormat = false;
        f.Features = 0; Reject([&] { (void)resources.CreateImage(image); }); f.Features = ~VkFormatFeatureFlags{0};
        for (const auto format : {TextureFormat::RGB8Unorm, TextureFormat::RGB32Float,
            TextureFormat::RGBA8Srgb, TextureFormat::BGRA8Unorm, TextureFormat::BGRA8Srgb,
            TextureFormat::D16Unorm, TextureFormat::D24UnormS8Uint, TextureFormat::D32Float, TextureFormat::D32FloatS8Uint})
        {
            auto invalid = image; invalid.format = format; invalid.usage = TextureUsage::Storage;
            const auto queries = f.Queries;
            Reject([&] { (void)resources.CreateImage(invalid); });
            Expect(f.Queries == queries, "Invalid logical storage format reached a native query.");
        }
        Expect(f.Allocations == before && !f.Live(), "Invalid image reached allocation.");
        auto nativeImage = resources.CreateImage(image);
        Expect(f.ImageInfo.format == VK_FORMAT_R8G8B8A8_UNORM && f.ImageInfo.mipLevels == 4
            && f.ImageInfo.initialLayout == VK_IMAGE_LAYOUT_UNDEFINED && f.AllocationInfo.flags == 0, "Image allocation policy differs.");
        TextureViewDesc view{}; view.baseMipLevel = 1; view.mipLevelCount = 2;
        auto nativeView = resources.CreateView(nativeImage.Image, image, view, true);
        Expect(f.ViewInfo.components.a == VK_COMPONENT_SWIZZLE_ONE && f.ViewInfo.subresourceRange.baseMipLevel == 1
            && f.ViewInfo.subresourceRange.levelCount == 2, "Sampled RGB view policy differs.");
        dispatch.DestroyView(dispatch.Device, nativeView, nullptr);
        view.baseMipLevel = 3; Reject([&] { (void)resources.CreateView(nativeImage.Image, image, view); });
        view = {}; view.format = TextureFormat::R8Unorm; Reject([&] { (void)resources.CreateView(nativeImage.Image, image, view); });
        Reject([&] { (void)resources.CreateView(VK_NULL_HANDLE, image, {}); });
        auto depth = image; depth.format = TextureFormat::D24UnormS8Uint; depth.usage = TextureUsage::Sampled | TextureUsage::DepthStencilAttachment;
        auto depthImage = resources.CreateImage(depth);
        auto depthView = resources.CreateView(depthImage.Image, depth, {}, true);
        Expect(f.ViewInfo.subresourceRange.aspectMask == VK_IMAGE_ASPECT_DEPTH_BIT, "Sampled depth view includes stencil.");
        dispatch.DestroyView(dispatch.Device, depthView, nullptr);
        depthView = resources.CreateView(depthImage.Image, depth, {});
        Expect(f.ViewInfo.subresourceRange.aspectMask == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT), "Attachment lost stencil.");
        dispatch.DestroyView(dispatch.Device, depthView, nullptr); dispatch.DestroyImage(depthImage);
        auto volume = image; volume.depth = 4; auto volumeImage = resources.CreateImage(volume);
        auto volumeView = resources.CreateView(volumeImage.Image, volume, {}, true);
        Expect(f.ViewInfo.viewType == VK_IMAGE_VIEW_TYPE_3D, "Sampled 3D view has wrong type.");
        dispatch.DestroyView(dispatch.Device, volumeView, nullptr); dispatch.DestroyImage(volumeImage);
        auto array = image; array.arrayLayers = 3; auto arrayImage = resources.CreateImage(array);
        view = {}; view.arrayLayerCount = 3;
        auto arrayView = resources.CreateView(arrayImage.Image, array, view, true);
        Expect(f.ViewInfo.viewType == VK_IMAGE_VIEW_TYPE_2D_ARRAY && f.ViewInfo.subresourceRange.layerCount == 3, "Sampled array view lost layers.");
        dispatch.DestroyView(dispatch.Device, arrayView, nullptr); dispatch.DestroyImage(arrayImage);
        SamplerDesc sampler{}; sampler.maxAnisotropy = 16; sampler.addressU = SamplerAddressMode::ClampToBorder;
        auto nativeSampler = resources.CreateSampler(sampler);
        Expect(f.SamplerInfo.maxAnisotropy == 8 && f.SamplerInfo.anisotropyEnable
            && f.SamplerInfo.addressModeU == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, "Sampler capability policy differs.");
        dispatch.DestroySampler(dispatch.Device, nativeSampler, nullptr);
        auto unsupportedCaps = caps; unsupportedCaps.supportsAnisotropy = false;
        VulkanResources unsupported(dispatch, unsupportedCaps); Reject([&] { (void)unsupported.CreateSampler(sampler); });
        const auto live = f.Live();
        f.FailAllocation = true;
        Reject([&] { (void)resources.CreateBuffer(buffer); }); Reject([&] { (void)resources.CreateImage(image); });
        f.FailAllocation = false; f.FailNative = true;
        Reject([&] { (void)resources.CreateView(nativeImage.Image, image, {}); }); Reject([&] { (void)resources.CreateSampler({}); });
        f.FailNative = false; f.FailName = true; const auto destroyed = f.Destroys;
        Reject([&] { (void)resources.CreateImage(image); }); Reject([&] { (void)resources.CreateView(nativeImage.Image, image, {}); });
        Reject([&] { (void)resources.CreateSampler({}); }); f.FailName = false;
        Expect(f.Live() == live && f.Destroys == destroyed + 3, "Post-create error leaked or destroyed retained resources.");
        dispatch.DestroyImage(nativeImage); Expect(!f.Live(), "Native resource ownership did not return to zero.");
        resources.Close(); resources.Close(); const auto calls = f.Allocations + f.NativeCreates + f.Queries;
        Reject([&] { (void)resources.CreateBuffer(buffer); }); Reject([&] { (void)resources.CreateImage(image); });
        Reject([&] { (void)resources.CreateView(Handle<VkImage>(1), image, {}); }); Reject([&] { (void)resources.CreateSampler({}); });
        Expect(f.Allocations + f.NativeCreates + f.Queries == calls && !f.Live(), "Closed factory reached native dispatch.");
        std::cout << "Vulkan resources PASS; format/extent/usage/views/sampler; mapped allocation; error rollback; closed dispatch\n";
    }
}
int main()
{
    try { Run(); return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
