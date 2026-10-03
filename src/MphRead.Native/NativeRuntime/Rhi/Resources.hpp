#pragma once

#include "ResourceState.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

namespace MphRead::NativeRuntime::Rhi
{
    enum class BufferUsage : std::uint32_t
    {
        None = 0,
        Vertex = 1U << 0,
        Index = 1U << 1,
        Uniform = 1U << 2,
        Storage = 1U << 3,
        TransferSrc = 1U << 4,
        TransferDst = 1U << 5
    };

    enum class TextureUsage : std::uint32_t
    {
        None = 0,
        Sampled = 1U << 0,
        Storage = 1U << 1,
        ColorAttachment = 1U << 2,
        DepthStencilAttachment = 1U << 3,
        TransferSrc = 1U << 4,
        TransferDst = 1U << 5
    };

    enum class TextureFormat : std::uint16_t
    {
        Undefined,
        R8Unorm,
        RG8Unorm,
        RGBA8Unorm,
        RGBA8Srgb,
        BGRA8Unorm,
        BGRA8Srgb,
        R16Float,
        RG16Float,
        RGBA16Float,
        R32Float,
        RG32Float,
        RGB32Float,
        RGBA32Float,
        D16Unorm,
        D24UnormS8Uint,
        D32Float,
        D32FloatS8Uint,
        // Three-channel colour: the scene and cel targets and the movie
        // frames. Appended so the earlier values keep their numbers.
        RGB8Unorm
    };

    enum class MemoryUsage : std::uint8_t
    {
        GpuOnly,
        CpuToGpu,
        GpuToCpu
    };

    enum class ShaderStage : std::uint32_t
    {
        None = 0,
        Vertex = 1U << 0,
        Fragment = 1U << 1,
        Compute = 1U << 2,
        AllGraphics = (1U << 0) | (1U << 1)
    };

    enum class Filter : std::uint8_t
    {
        Nearest,
        Linear
    };

    enum class SamplerAddressMode : std::uint8_t
    {
        Repeat,
        MirroredRepeat,
        ClampToEdge,
        ClampToBorder
    };

    enum class BorderColor : std::uint8_t
    {
        TransparentBlack,
        OpaqueBlack,
        OpaqueWhite
    };

    [[nodiscard]] constexpr BufferUsage operator|(BufferUsage left, BufferUsage right) noexcept
    {
        using U = std::underlying_type_t<BufferUsage>;
        return static_cast<BufferUsage>(static_cast<U>(left) | static_cast<U>(right));
    }

    [[nodiscard]] constexpr TextureUsage operator|(TextureUsage left, TextureUsage right) noexcept
    {
        using U = std::underlying_type_t<TextureUsage>;
        return static_cast<TextureUsage>(static_cast<U>(left) | static_cast<U>(right));
    }

    [[nodiscard]] constexpr ShaderStage operator|(ShaderStage left, ShaderStage right) noexcept
    {
        using U = std::underlying_type_t<ShaderStage>;
        return static_cast<ShaderStage>(static_cast<U>(left) | static_cast<U>(right));
    }

    struct BufferDesc final
    {
        std::uint64_t size = 0;
        BufferUsage usage = BufferUsage::None;
        MemoryUsage memoryUsage = MemoryUsage::GpuOnly;
        ResourceState initialState = ResourceState::Undefined;

        bool operator==(const BufferDesc&) const = default;
    };

    struct TextureDesc final
    {
        std::uint32_t width = 1;
        std::uint32_t height = 1;
        std::uint32_t depth = 1;
        std::uint32_t mipLevels = 1;
        std::uint32_t arrayLayers = 1;
        std::uint32_t sampleCount = 1;
        TextureFormat format = TextureFormat::Undefined;
        TextureUsage usage = TextureUsage::None;
        MemoryUsage memoryUsage = MemoryUsage::GpuOnly;
        ResourceState initialState = ResourceState::Undefined;

        bool operator==(const TextureDesc&) const = default;
    };

    struct TextureViewDesc final
    {
        TextureFormat format = TextureFormat::Undefined;
        std::uint32_t baseMipLevel = 0;
        std::uint32_t mipLevelCount = 1;
        std::uint32_t baseArrayLayer = 0;
        std::uint32_t arrayLayerCount = 1;

        bool operator==(const TextureViewDesc&) const = default;
    };

    struct SamplerDesc final
    {
        Filter minFilter = Filter::Linear;
        Filter magFilter = Filter::Linear;
        Filter mipFilter = Filter::Linear;
        SamplerAddressMode addressU = SamplerAddressMode::Repeat;
        SamplerAddressMode addressV = SamplerAddressMode::Repeat;
        SamplerAddressMode addressW = SamplerAddressMode::Repeat;
        BorderColor borderColor = BorderColor::TransparentBlack;
        float minLod = 0.0F;
        float maxLod = 1000.0F;
        float maxAnisotropy = 1.0F;

        bool operator==(const SamplerDesc&) const = default;
    };

    enum class ShaderCodeFormat : std::uint8_t
    {
        SpirV,
        GlslSource,
        Dxil,
        MetalLibrary
    };

    struct ShaderDesc final
    {
        ShaderStage stage = ShaderStage::None;
        std::vector<std::byte> code;
        std::string entryPoint = "main";
        ShaderCodeFormat format = ShaderCodeFormat::SpirV;

        bool operator==(const ShaderDesc&) const = default;
    };

    class Buffer
    {
    public:
        virtual ~Buffer() = default;
        Buffer(const Buffer&) = delete;
        Buffer& operator=(const Buffer&) = delete;
        Buffer(Buffer&&) = delete;
        Buffer& operator=(Buffer&&) = delete;

        [[nodiscard]] virtual const BufferDesc& Desc() const noexcept = 0;

    protected:
        Buffer() = default;
    };

    // A texture's identity as the frontend passes it around: the value a
    // material, a HUD object or a trail keeps where it once kept a GL name.
    // Nonzero for every live texture a device created; the backend alone knows
    // what native object it stands for.
    struct TextureHandle final
    {
        std::int32_t value = 0;

        [[nodiscard]] explicit constexpr operator bool() const noexcept { return value != 0; }
        bool operator==(const TextureHandle&) const = default;
    };

    // Pixels for GraphicsDevice::WriteTexture: tightly packed rows, bottom row
    // first as the renderer has always handed them over.
    struct TextureWrite final
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        TextureFormat format = TextureFormat::RGBA8Unorm;
        const void* data = nullptr;
    };

    class Texture
    {
    public:
        virtual ~Texture() = default;
        Texture(const Texture&) = delete;
        Texture& operator=(const Texture&) = delete;
        Texture(Texture&&) = delete;
        Texture& operator=(Texture&&) = delete;

        [[nodiscard]] virtual const TextureDesc& Desc() const noexcept = 0;
        [[nodiscard]] virtual TextureHandle Handle() const noexcept = 0;

    protected:
        Texture() = default;
    };

    class TextureView
    {
    public:
        virtual ~TextureView() = default;
        TextureView(const TextureView&) = delete;
        TextureView& operator=(const TextureView&) = delete;
        TextureView(TextureView&&) = delete;
        TextureView& operator=(TextureView&&) = delete;

        [[nodiscard]] virtual const TextureViewDesc& Desc() const noexcept = 0;
        [[nodiscard]] virtual const Texture& TextureResource() const noexcept = 0;

    protected:
        TextureView() = default;
    };

    class Sampler
    {
    public:
        virtual ~Sampler() = default;
        Sampler(const Sampler&) = delete;
        Sampler& operator=(const Sampler&) = delete;
        Sampler(Sampler&&) = delete;
        Sampler& operator=(Sampler&&) = delete;

        [[nodiscard]] virtual const SamplerDesc& Desc() const noexcept = 0;

    protected:
        Sampler() = default;
    };

    class Shader
    {
    public:
        virtual ~Shader() = default;
        Shader(const Shader&) = delete;
        Shader& operator=(const Shader&) = delete;
        Shader(Shader&&) = delete;
        Shader& operator=(Shader&&) = delete;

        [[nodiscard]] virtual const ShaderDesc& Desc() const noexcept = 0;

    protected:
        Shader() = default;
    };
}
