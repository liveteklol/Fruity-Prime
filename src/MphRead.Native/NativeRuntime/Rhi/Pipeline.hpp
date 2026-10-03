#pragma once

#include "Bindings.hpp"
#include "Resources.hpp"

#include <cstdint>
#include <vector>

namespace MphRead::NativeRuntime::Rhi
{
    enum class PrimitiveTopology : std::uint8_t
    {
        PointList,
        LineList,
        LineStrip,
        TriangleList,
        TriangleStrip
    };

    enum class CullMode : std::uint8_t
    {
        None,
        Front,
        Back
    };

    enum class FrontFace : std::uint8_t
    {
        Clockwise,
        CounterClockwise
    };

    enum class FillMode : std::uint8_t
    {
        Solid,
        Wireframe
    };

    enum class CompareOp : std::uint8_t
    {
        Never,
        Less,
        Equal,
        LessEqual,
        Greater,
        NotEqual,
        GreaterEqual,
        Always
    };

    enum class StencilOp : std::uint8_t
    {
        Keep,
        Zero,
        Replace,
        IncrementClamp,
        DecrementClamp,
        Invert,
        IncrementWrap,
        DecrementWrap
    };

    enum class BlendFactor : std::uint8_t
    {
        Zero,
        One,
        SrcColor,
        OneMinusSrcColor,
        DstColor,
        OneMinusDstColor,
        SrcAlpha,
        OneMinusSrcAlpha,
        DstAlpha,
        OneMinusDstAlpha,
        ConstantColor,
        OneMinusConstantColor,
        ConstantAlpha,
        OneMinusConstantAlpha
    };

    enum class BlendOp : std::uint8_t
    {
        Add,
        Subtract,
        ReverseSubtract,
        Min,
        Max
    };

    enum class ColorWriteMask : std::uint8_t
    {
        None = 0,
        Red = 1U << 0,
        Green = 1U << 1,
        Blue = 1U << 2,
        Alpha = 1U << 3,
        All = (1U << 0) | (1U << 1) | (1U << 2) | (1U << 3)
    };

    enum class VertexFormat : std::uint8_t
    {
        Float,
        Float2,
        Float3,
        Float4,
        UByte4Norm,
        Short2Norm,
        Short4Norm,
        UInt
    };

    enum class VertexInputRate : std::uint8_t
    {
        Vertex,
        Instance
    };

    [[nodiscard]] constexpr ColorWriteMask operator|(
        ColorWriteMask left, ColorWriteMask right) noexcept
    {
        return static_cast<ColorWriteMask>(
            static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right));
    }

    struct VertexBufferLayoutDesc final
    {
        std::uint32_t slot = 0;
        std::uint32_t stride = 0;
        VertexInputRate inputRate = VertexInputRate::Vertex;

        bool operator==(const VertexBufferLayoutDesc&) const = default;
    };

    struct VertexAttributeDesc final
    {
        std::uint32_t location = 0;
        std::uint32_t bufferSlot = 0;
        VertexFormat format = VertexFormat::Float;
        std::uint32_t offset = 0;

        bool operator==(const VertexAttributeDesc&) const = default;
    };

    // The alpha test, which the DS hardware has and Vulkan does not: the
    // fragment shader discards against it. Equal/Less against 1.0 are the
    // only two comparisons the renderer asks for, and their boundaries are
    // the fixed-function ones (EqualOne keeps alpha == 1.0 exactly).
    enum class AlphaTestMode : std::uint8_t
    {
        Disabled,
        EqualOne,
        LessThanOne
    };

    struct RasterizerStateDesc final
    {
        CullMode cullMode = CullMode::Back;
        FrontFace frontFace = FrontFace::CounterClockwise;
        FillMode fillMode = FillMode::Solid;
        bool depthClampEnable = false;
        // glPolygonOffset / VkPipelineRasterizationStateCreateInfo::depthBias*.
        bool depthBiasEnable = false;
        float depthBiasConstant = 0.0F;
        float depthBiasSlope = 0.0F;
        float lineWidth = 1.0F;

        bool operator==(const RasterizerStateDesc&) const = default;
    };

    struct StencilFaceStateDesc final
    {
        StencilOp failOp = StencilOp::Keep;
        StencilOp depthFailOp = StencilOp::Keep;
        StencilOp passOp = StencilOp::Keep;
        CompareOp compareOp = CompareOp::Always;

        bool operator==(const StencilFaceStateDesc&) const = default;
    };

    struct DepthStencilStateDesc final
    {
        bool depthTestEnable = false;
        bool depthWriteEnable = false;
        CompareOp depthCompareOp = CompareOp::LessEqual;
        bool stencilTestEnable = false;
        std::uint8_t stencilReadMask = 0xFF;
        std::uint8_t stencilWriteMask = 0xFF;
        StencilFaceStateDesc front{};
        StencilFaceStateDesc back{};

        bool operator==(const DepthStencilStateDesc&) const = default;
    };

    struct BlendAttachmentDesc final
    {
        bool blendEnable = false;
        BlendFactor srcColorFactor = BlendFactor::One;
        BlendFactor dstColorFactor = BlendFactor::Zero;
        BlendOp colorOp = BlendOp::Add;
        BlendFactor srcAlphaFactor = BlendFactor::One;
        BlendFactor dstAlphaFactor = BlendFactor::Zero;
        BlendOp alphaOp = BlendOp::Add;
        ColorWriteMask writeMask = ColorWriteMask::All;

        bool operator==(const BlendAttachmentDesc&) const = default;
    };

    struct GraphicsPipelineDesc final
    {
        // Creation inputs borrowed for native compilation. A compiled
        // pipeline's Desc keeps its value state and clears these pointers;
        // releasing public shader wrappers does not release its executable.
        const Shader* vertexShader = nullptr;
        const Shader* fragmentShader = nullptr;
        PipelineLayout pipelineLayout{};
        PrimitiveTopology topology = PrimitiveTopology::TriangleList;
        RasterizerStateDesc rasterizer{};
        DepthStencilStateDesc depthStencil{};
        std::vector<VertexBufferLayoutDesc> vertexBuffers;
        std::vector<VertexAttributeDesc> vertexAttributes;
        std::vector<BlendAttachmentDesc> blendAttachments;
        std::vector<TextureFormat> colorFormats;
        TextureFormat depthStencilFormat = TextureFormat::Undefined;
        std::uint32_t sampleCount = 1;
        AlphaTestMode alphaTest = AlphaTestMode::Disabled;
        // Portable budget; zero means this pipeline has no small constants.
        std::uint32_t smallConstantBytes = 0;

        bool operator==(const GraphicsPipelineDesc&) const = default;
    };

    class GraphicsPipeline
    {
    public:
        virtual ~GraphicsPipeline() = default;
        GraphicsPipeline(const GraphicsPipeline&) = delete;
        GraphicsPipeline& operator=(const GraphicsPipeline&) = delete;
        GraphicsPipeline(GraphicsPipeline&&) = delete;
        GraphicsPipeline& operator=(GraphicsPipeline&&) = delete;

        [[nodiscard]] virtual const GraphicsPipelineDesc& Desc() const noexcept = 0;

    protected:
        GraphicsPipeline() = default;
    };
}
