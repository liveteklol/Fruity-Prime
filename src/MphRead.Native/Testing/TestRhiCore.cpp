#include "../NativeRuntime/Rhi/Backend.hpp"
#include "../NativeRuntime/Rhi/Bindings.hpp"
#include "../NativeRuntime/Rhi/Capabilities.hpp"
#include "../NativeRuntime/Rhi/CommandList.hpp"
#include "../NativeRuntime/Rhi/GraphicsDevice.hpp"
#include "../NativeRuntime/Rhi/Pipeline.hpp"
#include "../NativeRuntime/Rhi/Resources.hpp"
#include "../NativeRuntime/Rhi/ResourceState.hpp"
#include "../NativeRuntime/Rhi/ResourceStatePolicy.hpp"
#include "../NativeRuntime/Rhi/Swapchain.hpp"

#include <memory>
#include <type_traits>
#include <utility>

namespace
{
    using namespace MphRead::NativeRuntime::Rhi;

    constexpr BufferDesc BufferA{
        4096,
        BufferUsage::Vertex | BufferUsage::TransferDst,
        MemoryUsage::GpuOnly,
        ResourceState::CopyDst
    };
    constexpr BufferDesc BufferB = BufferA;
    static_assert(BufferA == BufferB);

    constexpr TextureViewDesc ViewA{
        TextureFormat::RGBA8Unorm,
        1,
        2,
        3,
        4
    };
    constexpr TextureViewDesc ViewB = ViewA;
    static_assert(ViewA == ViewB);

    constexpr SwapchainDesc SwapchainA{
        1280,
        720,
        3,
        TextureFormat::BGRA8Srgb,
        PresentMode::Fifo
    };
    constexpr SwapchainDesc SwapchainB = SwapchainA;
    static_assert(SwapchainA == SwapchainB);

    static_assert(IsValidResourceState(
        ResourceState::VertexBuffer | ResourceState::ShaderRead | ResourceState::CopySrc));
    static_assert(!IsValidResourceState(
        ResourceState::ColorAttachment | ResourceState::CopyDst));
    static_assert(!IsValidResourceState(ResourceState::CopyDst | ResourceState::CopySrc));
    static_assert(!IsValidResourceState(ResourceState::ShaderWrite | ResourceState::ShaderRead));
    static_assert(!IsValidResourceState(static_cast<ResourceState>(1U << 31)));
    static_assert(!IsValidResourceState(ResourceState::ShaderRead | static_cast<ResourceState>(1U << 31)));
    static_assert(IsValidResourceState(ResourceState::ShaderWrite));
    static_assert(IsValidBufferState(ResourceState::ConstantBuffer | ResourceState::VertexBuffer));
    static_assert(!IsValidBufferState(ResourceState::ColorAttachment));
    static_assert(!IsValidBufferState(ResourceState::DepthStencilRead));
    static_assert(!IsValidBufferState(ResourceState::Present));
    static_assert(IsValidTextureState(ResourceState::ShaderRead | ResourceState::CopySrc));
    static_assert(!IsValidTextureState(ResourceState::ConstantBuffer));
    static_assert(IsValidTransition(ResourceState::Undefined, ResourceState::CopyDst));
    static_assert(!IsValidTransition(ResourceState::CopyDst, ResourceState::Undefined));

    static_assert(IsValidBufferState(BufferA, ResourceState::VertexBuffer));
    static_assert(!IsValidBufferState(BufferA, ResourceState::IndexBuffer));
    static_assert(!IsValidBufferState(BufferA, ResourceState::ConstantBuffer));
    static_assert(!IsValidBufferState(BufferA, ResourceState::CopySrc));
    static_assert(!IsValidBufferState(BufferA, ResourceState::ShaderRead));
    static_assert(!IsValidBufferState(BufferA, ResourceState::ShaderWrite));
    static_assert(!IsValidBufferState({1, static_cast<BufferUsage>(1U << 31)}, ResourceState::Common));
    constexpr TextureDesc Sampled{1, 1, 1, 1, 1, 1, TextureFormat::RGBA8Unorm, TextureUsage::Sampled};
    static_assert(IsValidTextureState(Sampled, ResourceState::ShaderRead));
    static_assert(!IsValidTextureState(Sampled, ResourceState::CopySrc));
    static_assert(!IsValidTextureState(Sampled, ResourceState::CopyDst));
    static_assert(!IsValidTextureState(Sampled, ResourceState::ColorAttachment));
    static_assert(!IsValidTextureState(Sampled, ResourceState::ShaderWrite));
    static_assert(!IsValidTextureState(Sampled, ResourceState::DepthStencilRead));
    static_assert(!IsValidTextureState(Sampled, ResourceState::Present));
    constexpr TextureDesc Storage{1, 1, 1, 1, 1, 1, TextureFormat::RGBA8Unorm, TextureUsage::Storage};
    static_assert(IsValidTextureState(Storage, ResourceState::ShaderRead));
    static_assert(IsValidTextureState(Storage, ResourceState::ShaderWrite));
    static_assert([] {
        unsigned supported = 0;
        for (unsigned value = 0; value <= static_cast<unsigned>(TextureFormat::RGB8Unorm); ++value)
        {
            const auto format = static_cast<TextureFormat>(value);
            const TextureDesc desc{1, 1, 1, 1, 1, 1, format, TextureUsage::Storage};
            for (const auto state : {ResourceState::Undefined, ResourceState::Common,
                ResourceState::ShaderRead, ResourceState::ShaderWrite})
                if (IsValidTextureState(desc, state) != IsStorageTextureFormat(format)) return false;
            supported += IsStorageTextureFormat(format);
        }
        return supported == 9;
    }());
    constexpr TextureDesc Depth{1, 1, 1, 1, 1, 1, TextureFormat::D24UnormS8Uint,
        TextureUsage::DepthStencilAttachment | TextureUsage::Sampled};
    static_assert(IsValidTextureState(Depth, ResourceState::DepthStencilRead | ResourceState::ShaderRead));
    static_assert(IsValidTextureState(Depth, ResourceState::DepthStencilWrite));
    static_assert(!IsValidTextureState(Depth, ResourceState::ColorAttachment));
    static_assert(!IsValidTextureState({1, 1, 1, 1, 1, 1, TextureFormat::RGBA8Unorm,
        TextureUsage::DepthStencilAttachment}, ResourceState::Undefined));
    static_assert(!IsValidTextureState({1, 1, 1, 1, 1, 1, TextureFormat::D24UnormS8Uint,
        TextureUsage::ColorAttachment}, ResourceState::Undefined));

    static_assert(std::is_abstract_v<GraphicsDevice>);
    static_assert(std::is_abstract_v<CommandList>);
    static_assert(std::is_abstract_v<Swapchain>);
    static_assert(!std::is_copy_constructible_v<Buffer>);
    static_assert(!std::is_copy_constructible_v<Texture>);
    static_assert(!std::is_copy_constructible_v<GraphicsPipeline>);

    using BufferOwner = decltype(std::declval<GraphicsDevice&>().CreateBuffer(BufferA));
    static_assert(std::is_same_v<BufferOwner, std::unique_ptr<Buffer>>);
}
