#include "VulkanContext.hpp"
#include "../../../Renderer.hpp"
#include "../../../Mods/Branding.hpp"
#include <iostream>
#include <stdexcept>

#if defined(FRUITY_HAS_VULKAN)
#include "VulkanContextInternal.hpp"
#include "VulkanGraphicsDevice.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <tuple>
#include <vector>

namespace MphRead::NativeRuntime::Rhi::Vulkan
{
    Context::Context(bool validation) : _impl(std::make_unique<Impl>())
    {
        _impl->Initialize(validation);
    }
    Context::Context(bool validation, ::MphRead::RendererPlatform::Window& window, bool allowMaintenance)
        : _impl(std::make_unique<Impl>())
    {
#if defined(__ANDROID__)
        (void)validation; (void)window; (void)allowMaintenance;
        throw std::invalid_argument("Android presents through an ANativeWindow, not a GLFW window.");
#else
        void* native = window.NativeHandle();
        if (!native || window.GraphicsMode() != ::MphRead::RendererPlatform::GraphicsWindowMode::NoApi)
            throw std::invalid_argument("A Vulkan context requires a NoApi window.");
        _impl->Initialize(validation, native, allowMaintenance);
#endif
    }
    Context::Context(bool validation, AndroidWindow window) : _impl(std::make_unique<Impl>())
    {
#if defined(__ANDROID__)
        _impl->Initialize(validation, window.Native, true);
#else
        (void)validation; (void)window;
        throw std::invalid_argument("An ANativeWindow surface exists only on Android.");
#endif
    }
    void Context::ReplaceAndroidSurface(void* nativeWindow)
    {
#if defined(__ANDROID__)
        WaitIdle();
        _impl->CreateAndroidSurface(nativeWindow);
#else
        (void)nativeWindow;
        throw std::invalid_argument("An ANativeWindow surface exists only on Android.");
#endif
    }
    void Context::ReplaceWindowSurface(void* window)
    {
#if defined(__ANDROID__)
        (void)window;
        throw std::invalid_argument("A GLFW window surface does not exist on Android.");
#else
        WaitIdle();
        if (window == nullptr) _impl->DestroySurface();
        else _impl->CreateWindowSurface(window);
#endif
    }
    Context::~Context() = default;
    std::string Context::ProbePassive()
    {
        try
        {
            Impl probe;
            probe.Initialize(false, nullptr, true, false);
            if (probe.device || probe.graphics || probe.present || !probe.physical
                || !probe.instanceProbe.Eligible || !SelectPhysicalDevice(probe.deviceProbes))
                throw std::logic_error("Vulkan passive eligibility violated its instance/physical-device-only boundary.");
            std::cout << "[vulkan probe] passive eligible; logical-device=0; queues=0; candidates="
                << probe.deviceProbes.size() << '\n';
            return {};
        }
        catch (const std::exception& ex) { return ex.what(); }
    }
    const Capabilities& Context::Caps() const noexcept { return _impl->caps; }
    const std::string& Context::DeviceName() const noexcept { return _impl->name; }
    std::string Context::Describe() const
    {
        const auto& impl = *_impl;
        std::string driver;
        if (impl.vendorId == 0x10DE)
            driver = std::to_string((impl.driverVersion >> 22) & 0x3FF) + "."
                + std::to_string((impl.driverVersion >> 14) & 0xFF);
        else
            driver = std::to_string(VK_API_VERSION_MAJOR(impl.driverVersion)) + "."
                + std::to_string(VK_API_VERSION_MINOR(impl.driverVersion)) + "."
                + std::to_string(VK_API_VERSION_PATCH(impl.driverVersion));
        return impl.name + ", Vulkan " + std::to_string(VK_API_VERSION_MAJOR(impl.apiVersion)) + "."
            + std::to_string(VK_API_VERSION_MINOR(impl.apiVersion)) + "."
            + std::to_string(VK_API_VERSION_PATCH(impl.apiVersion)) + ", driver " + driver;
    }
    unsigned Context::ValidationErrors() const noexcept { return _impl->errors.load(); }
    bool Context::ValidationEnabled() const noexcept { return _impl->validation; }
    void Context::WaitIdle() { if (_impl->device) Check(_impl->vkDeviceWaitIdle(_impl->device), "vkDeviceWaitIdle"); }
    void Context::Shutdown() { WaitIdle(); _impl->Shutdown(); }

    void Context::CheckCommandBufferDebugName()
    {
        VkCommandPoolCreateInfo info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; info.queueFamilyIndex = _impl->graphicsFamily;
        VkCommandPool pool = VK_NULL_HANDLE; Check(_impl->vkCreateCommandPool(_impl->device, &info, nullptr, &pool), "vkCreateCommandPool");
        try
        {
            VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            allocate.commandPool = pool; allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; allocate.commandBufferCount = 1;
            VkCommandBuffer buffer = VK_NULL_HANDLE;
            Check(_impl->vkAllocateCommandBuffers(_impl->device, &allocate, &buffer), "vkAllocateCommandBuffers");
            _impl->Name(VK_OBJECT_TYPE_COMMAND_BUFFER, reinterpret_cast<std::uint64_t>(buffer), "RHI foundation command buffer");
        }
        catch (...) { _impl->vkDestroyCommandPool(_impl->device, pool, nullptr); throw; }
        _impl->vkDestroyCommandPool(_impl->device, pool, nullptr);
    }

    int RunFoundationCheck()
    {
        try
        {
            RendererPlatform::WindowSettings settings{};
            settings.GraphicsMode = RendererPlatform::GraphicsWindowMode::NoApi;
            settings.Title = std::string(Mods::Branding::Name) + " Vulkan foundation check";
            settings.StartVisible = false;
            auto window = RendererPlatform::CreateWindow(settings);
            const auto passiveFailure = Context::ProbePassive();
            if (!passiveFailure.empty()) throw std::runtime_error(passiveFailure);
            Context context(true);
            context.CheckCommandBufferDebugName(); context.WaitIdle();
            // Destroy while the messenger still exists, so teardown errors count.
            context.Shutdown();
            if (context.ValidationErrors()) throw std::runtime_error("Vulkan validation errors.");
            std::cout << "[vulkan] foundation PASS; clean shutdown; validation=" << context.ValidationEnabled() << '\n';
            return 0;
        }
        catch (const std::exception& e) { std::cerr << "[vulkan] foundation FAIL: " << e.what() << '\n'; return 1; }
    }

    int RunResourceCheck()
    {
        try
        {
            RendererPlatform::WindowSettings settings{};
            settings.GraphicsMode = RendererPlatform::GraphicsWindowMode::NoApi;
            settings.Title = std::string(Mods::Branding::Name) + " Vulkan resource check";
            settings.StartVisible = false;
            auto window = RendererPlatform::CreateWindow(settings);
            (void)window;

            Context context(true);
            if (!context.ValidationEnabled())
                throw std::runtime_error("Vulkan validation layers are required for the resource check.");
            auto device = CreateGraphicsDevice(context);
            // Cycle every descriptor frame slot repeatedly: pool reset must
            // follow its completion fence, including otherwise empty frames.
            for (std::uint64_t frame = 1; frame <= 8; ++frame)
            {
                const auto acquired = device->BeginFrame();
                if (acquired.Number != frame || acquired.Slot != (frame - 1) % FramesInFlight)
                    throw std::runtime_error("Vulkan descriptor frame numbering differs.");
                device->EndFrame();
            }
            device->WaitIdle();
            CheckBindingAllocations(*device);
            CheckShaderModules(*device);
            CheckGraphicsPipelines(*device);
            CheckResourceRetirement(*device);
            CheckUploadReuse(*device);
            CheckMemoryAdmission(*device);
            std::cout << "[vulkan] retirement PASS; 64 clear/bind/resize/release cycles; failed resize retained; "
                << "churn device-wide waits=0; completed=submitted; retired=0\n";
            std::cout << "[vulkan] graphics pipelines PASS; main; composite; cel; shift; manifest layouts\n";
            std::cout << "[vulkan] shader modules PASS; eight stages; entry point validation; release\n";
            std::cout << "[vulkan] bindings allocation PASS; arrays; alignment; fresh sets; overflow pools; frame reuse; GPU bind submit\n";

            constexpr std::uint32_t width = 8;
            constexpr std::uint32_t height = 4;
            constexpr std::size_t bytes = width * height * 4;
            std::array<std::byte, bytes> expected{};
            for (std::size_t i = 0; i < expected.size(); ++i)
                expected[i] = static_cast<std::byte>((i * 37U + 11U) & 0xffU);

            {
                BufferDesc gpuDesc{};
                gpuDesc.size = bytes + 1;
                gpuDesc.usage = BufferUsage::TransferDst | BufferUsage::TransferSrc
                    | BufferUsage::Storage | BufferUsage::Vertex | BufferUsage::Index | BufferUsage::Uniform;
                gpuDesc.initialState = ResourceState::VertexBuffer | ResourceState::IndexBuffer
                    | ResourceState::ConstantBuffer;
                auto gpuBuffer = device->CreateBuffer(gpuDesc);
                device->WriteBuffer(*gpuBuffer, 1, std::span(expected).first(bytes - 1));

                BufferDesc readbackDesc{};
                readbackDesc.size = bytes;
                readbackDesc.usage = BufferUsage::TransferDst;
                readbackDesc.memoryUsage = MemoryUsage::GpuToCpu;
                auto bufferReadback = device->CreateBuffer(readbackDesc);
                auto commands = device->CreateCommandList();
                commands->Begin();
                commands->Transition(*gpuBuffer, gpuDesc.initialState, ResourceState::ShaderRead);
                commands->Transition(*gpuBuffer, ResourceState::ShaderRead, ResourceState::ShaderWrite);
                commands->Transition(*gpuBuffer, ResourceState::ShaderWrite, ResourceState::CopySrc);
                commands->Transition(*bufferReadback, ResourceState::Undefined, ResourceState::CopyDst);
                commands->CopyBuffer(*gpuBuffer, 1, *bufferReadback, 0, bytes - 1);
                commands->End();
                std::array<std::byte, bytes> actual{};
                device->ReadBuffer(*bufferReadback, 0, std::span(actual).first(bytes - 1));
                if (!std::equal(expected.begin(), expected.end() - 1, actual.begin()))
                    throw std::runtime_error("Vulkan buffer upload/copy/readback contents differ.");
            }

            {
                TextureDesc textureDesc{};
                textureDesc.width = 1;
                textureDesc.height = 1;
                textureDesc.format = TextureFormat::RGBA8Unorm;
                textureDesc.usage = TextureUsage::Sampled | TextureUsage::TransferSrc
                    | TextureUsage::TransferDst;
                auto texture = device->CreateTexture(textureDesc);
                TextureViewDesc viewDesc{};
                auto view = device->CreateTextureView(*texture, viewDesc);
                const TextureWrite write{width, height, TextureFormat::RGBA8Unorm, expected.data()};
                device->WriteTexture(*texture, write);
                if (texture->Desc().width != width || texture->Desc().height != height
                    || &view->TextureResource() != texture.get())
                    throw std::runtime_error("Vulkan texture upload did not preserve its resized texture view.");

                BufferDesc readbackDesc{};
                readbackDesc.size = bytes;
                readbackDesc.usage = BufferUsage::TransferDst;
                readbackDesc.memoryUsage = MemoryUsage::GpuToCpu;
                auto textureReadback = device->CreateBuffer(readbackDesc);
                auto commands = device->CreateCommandList();
                commands->Begin();
                commands->Transition(*texture, ResourceState::ShaderRead, ResourceState::CopySrc);
                commands->Transition(*textureReadback, ResourceState::Undefined, ResourceState::CopyDst);
                BufferTextureCopy region{};
                region.width = width;
                region.height = height;
                commands->CopyTextureToBuffer(*texture, *textureReadback, region);
                commands->End();
                std::array<std::byte, bytes> actual{};
                device->ReadBuffer(*textureReadback, 0, actual);
                if (actual != expected)
                    throw std::runtime_error("Vulkan texture upload/readback contents differ.");

                device->ResizeTexture(*texture, width + 3, height + 2);
                if (texture->Desc().width != width + 3 || texture->Desc().height != height + 2)
                    throw std::runtime_error("Vulkan texture resize did not retain its RHI object.");
            }

            {
                TextureDesc colorDesc{};
                colorDesc.format = TextureFormat::RGBA8Unorm;
                colorDesc.usage = TextureUsage::ColorAttachment | TextureUsage::TransferSrc
                    | TextureUsage::TransferDst;
                colorDesc.initialState = ResourceState::ColorAttachment;
                auto color = device->CreateTexture(colorDesc);
                auto commands = device->CreateCommandList();
                commands->Begin();
                commands->Transition(*color, ResourceState::ColorAttachment, ResourceState::CopyDst);
                commands->Transition(*color, ResourceState::CopyDst, ResourceState::ColorAttachment);
                commands->End();
            }

            {
                TextureDesc depthDesc{};
                depthDesc.format = TextureFormat::D32Float;
                depthDesc.usage = TextureUsage::DepthStencilAttachment;
                depthDesc.initialState = ResourceState::DepthStencilWrite;
                auto depth = device->CreateTexture(depthDesc);
                auto commands = device->CreateCommandList();
                commands->Begin();
                commands->Transition(*depth, ResourceState::DepthStencilWrite,
                    ResourceState::DepthStencilRead);
                commands->Transition(*depth, ResourceState::DepthStencilRead,
                    ResourceState::DepthStencilWrite);
                commands->End();
            }

            // Exercise tightly packed two-channel uploads and the separate
            // depth/stencil aspects of a packed image through the public RHI.
            for (const auto [format, aspect, pixelBytes] : std::array{
                std::tuple{TextureFormat::RG16Float, TextureAspect::Automatic, 4U},
                std::tuple{TextureFormat::D24UnormS8Uint, TextureAspect::Depth, 4U},
                std::tuple{TextureFormat::D24UnormS8Uint, TextureAspect::Stencil, 1U}})
            {
                const std::size_t size = width * height * pixelBytes;
                std::vector<std::byte> input(size, std::byte{0x11});
                BufferDesc uploadDesc{};
                uploadDesc.size = size;
                uploadDesc.usage = BufferUsage::TransferSrc;
                uploadDesc.memoryUsage = MemoryUsage::CpuToGpu;
                auto upload = device->CreateBuffer(uploadDesc);
                device->WriteBuffer(*upload, 0, input);
                TextureDesc desc{};
                desc.width = width;
                desc.height = height;
                desc.format = format;
                desc.usage = TextureUsage::TransferDst | TextureUsage::TransferSrc;
                auto image = device->CreateTexture(desc);
                BufferDesc readDesc{};
                readDesc.size = size;
                readDesc.usage = BufferUsage::TransferDst;
                readDesc.memoryUsage = MemoryUsage::GpuToCpu;
                auto readback = device->CreateBuffer(readDesc);
                auto commands = device->CreateCommandList();
                commands->Begin();
                commands->Transition(*upload, ResourceState::Undefined, ResourceState::CopySrc);
                commands->Transition(*image, ResourceState::Undefined, ResourceState::CopyDst);
                BufferTextureCopy region{};
                region.width = width;
                region.height = height;
                region.aspect = aspect;
                commands->CopyBufferToTexture(*upload, *image, region);
                commands->Transition(*image, ResourceState::CopyDst, ResourceState::CopySrc);
                commands->Transition(*readback, ResourceState::Undefined, ResourceState::CopyDst);
                commands->CopyTextureToBuffer(*image, *readback, region);
                commands->End();
                std::vector<std::byte> output(size);
                device->ReadBuffer(*readback, 0, output);
                // D24 depth copies use X8_D24_UNORM_PACK32 in buffer memory.
                // The high byte is padding with undefined contents, so compare
                // all defined depth bits while ignoring only that byte.
                if (format == TextureFormat::D24UnormS8Uint && aspect == TextureAspect::Depth)
                {
                    for (std::size_t offset = 0; offset < size; offset += 4)
                    {
                        std::uint32_t expected = 0;
                        std::uint32_t actual = 0;
                        std::memcpy(&expected, input.data() + offset, sizeof(expected));
                        std::memcpy(&actual, output.data() + offset, sizeof(actual));
                        if ((expected & 0x00FFFFFFU) != (actual & 0x00FFFFFFU))
                            throw std::runtime_error("Vulkan D24 depth copy contents differ.");
                    }
                    continue;
                }
                if (input != output)
                    throw std::runtime_error("Vulkan image aspect copy contents differ.");
            }

            device->WaitIdle();
            const GpuResourceStatistics live = device->Statistics();
            if (live.Textures != 0 || live.Buffers != 0 || live.Retired != 0 || live.Shaders != 0 || live.Programs != 0)
                throw std::runtime_error("Vulkan resource check found live resources after release.");
            device.reset();
            context.Shutdown();
            if (context.ValidationErrors() != 0)
                throw std::runtime_error("Vulkan validation reported resource or synchronization errors.");
            std::cout << "[vulkan] resources PASS; buffer upload/copy/readback; texture upload/readback/resize; "
                << "transitions; live=0; validation=1; errors=0\n";
            return 0;
        }
        catch (const std::exception& e)
        {
            std::cerr << "[vulkan] resources FAIL: " << e.what() << '\n';
            return 1;
        }
    }
}
#else
namespace MphRead::NativeRuntime::Rhi::Vulkan
{
    struct Context::Impl { Capabilities caps; std::string name; };
    std::string Context::ProbePassive() { return "Vulkan support was not built."; }
    Context::Context(bool) { throw std::runtime_error("Desktop Vulkan development support was not built."); }
    Context::Context(bool, ::MphRead::RendererPlatform::Window&, bool)
    {
        throw std::runtime_error("Desktop Vulkan development support was not built.");
    }
    Context::Context(bool, AndroidWindow) { throw std::runtime_error("Vulkan support was not built."); }
    void Context::ReplaceAndroidSurface(void*) { throw std::runtime_error("Vulkan support was not built."); }
    void Context::ReplaceWindowSurface(void*) { throw std::runtime_error("Vulkan support was not built."); }
    Context::~Context() = default;
    const Capabilities& Context::Caps() const noexcept { return _impl->caps; }
    const std::string& Context::DeviceName() const noexcept { return _impl->name; }
    std::string Context::Describe() const { return {}; }
    unsigned Context::ValidationErrors() const noexcept { return 0; }
    bool Context::ValidationEnabled() const noexcept { return false; }
    void Context::WaitIdle() { throw std::runtime_error("Vulkan unavailable."); }
    void Context::Shutdown() { throw std::runtime_error("Vulkan unavailable."); }
    void Context::CheckCommandBufferDebugName() { throw std::runtime_error("Vulkan unavailable."); }
    int RunFoundationCheck()
    {
        std::cerr << "[vulkan] foundation unavailable: desktop Vulkan SDK support was not built.\n";
        return 1;
    }
    int RunResourceCheck()
    {
        std::cerr << "[vulkan] resource check unavailable: desktop Vulkan SDK support was not built.\n";
        return 1;
    }
}
#endif
