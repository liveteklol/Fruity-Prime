#pragma once

#include "Bindings.hpp"
#include "Pipeline.hpp"
#include "ResourceState.hpp"
#include "Resources.hpp"
#include "Readback.hpp"
#include "GpuDiagnostics.hpp"
#include <stdexcept>

#include <cstdint>
#include <span>

namespace MphRead::NativeRuntime::Rhi
{
    enum class LoadOp : std::uint8_t
    {
        Load,
        Clear,
        DontCare
    };

    enum class StoreOp : std::uint8_t
    {
        Store,
        DontCare
    };

    enum class IndexType : std::uint8_t
    {
        UInt16,
        UInt32
    };

    struct Viewport final
    {
        float x = 0.0F;
        float y = 0.0F;
        float width = 0.0F;
        float height = 0.0F;
        float minDepth = 0.0F;
        float maxDepth = 1.0F;

        bool operator==(const Viewport&) const = default;
    };

    struct Scissor final
    {
        std::int32_t x = 0;
        std::int32_t y = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;

        bool operator==(const Scissor&) const = default;
    };

    struct ClearColor final
    {
        float red = 0.0F;
        float green = 0.0F;
        float blue = 0.0F;
        float alpha = 0.0F;

        bool operator==(const ClearColor&) const = default;
    };

    struct RenderingColorAttachment final
    {
        const TextureView* view = nullptr;
        LoadOp loadOp = LoadOp::Load;
        StoreOp storeOp = StoreOp::Store;
        ClearColor clearValue{};

        bool operator==(const RenderingColorAttachment&) const = default;
    };

    struct RenderingDepthStencilAttachment final
    {
        const TextureView* view = nullptr;
        LoadOp depthLoadOp = LoadOp::Load;
        StoreOp depthStoreOp = StoreOp::Store;
        LoadOp stencilLoadOp = LoadOp::Load;
        StoreOp stencilStoreOp = StoreOp::Store;
        float clearDepth = 1.0F;
        std::uint32_t clearStencil = 0;

        bool operator==(const RenderingDepthStencilAttachment&) const = default;
    };

    struct RenderingInfo final
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::span<const RenderingColorAttachment> colorAttachments{};
        const RenderingDepthStencilAttachment* depthStencilAttachment = nullptr;
        // Render to the window's own surface rather than to textures. The
        // attachments' load ops still apply, with null views standing for the
        // surface's own colour and depth/stencil.
        bool swapchain = false;
        // The region loads, clears and draws are confined to, as
        // VkRenderingInfo::renderArea; a zero width means all of it.
        Scissor renderArea{};
    };

    enum class TextureAspect : std::uint8_t
    {
        Automatic,
        Color,
        Depth,
        Stencil
    };

    struct BufferTextureCopy final
    {
        std::uint64_t bufferOffset = 0;
        std::uint32_t bytesPerRow = 0;
        std::uint32_t rowsPerImage = 0;
        std::uint32_t mipLevel = 0;
        std::uint32_t arrayLayer = 0;
        std::uint32_t x = 0;
        std::uint32_t y = 0;
        std::uint32_t z = 0;
        std::uint32_t width = 1;
        std::uint32_t height = 1;
        std::uint32_t depth = 1;
        // Automatic selects colour for colour images and depth for depth images.
        TextureAspect aspect = TextureAspect::Automatic;

        bool operator==(const BufferTextureCopy&) const = default;
    };

    enum class CommandListReadiness : std::uint8_t { Ready, Busy, Recording, Unavailable };

    class CommandList
    {
    public:
        virtual ~CommandList() = default;
        CommandList(const CommandList&) = delete;
        CommandList& operator=(const CommandList&) = delete;
        CommandList(CommandList&&) = delete;
        CommandList& operator=(CommandList&&) = delete;

        // Begin/End delimit a caller-owned interval, independent of internal
        // submission, frame boundaries and diagnostic readbacks. Nested Begin
        // and End without Begin throw logic_error without submitting work.
        // All recording mutations require this interval. ReadColor and
        // EnqueueReadColor are diagnostic operations allowed outside it.
        virtual void Begin() = 0;
        virtual void End() = 0;
        // Poll only: Busy must not reset a pool, submit work or wait. TryBegin
        // retains Begin's nested-recording contract and atomically admits work
        // on the recording thread. Unsupported backends fail closed.
        [[nodiscard]] virtual CommandListReadiness QueryReadiness() const
        { return CommandListReadiness::Unavailable; }
        virtual bool TryBegin()
        {
            if (QueryReadiness() == CommandListReadiness::Recording)
                throw std::logic_error("Command list is already recording.");
            if (QueryReadiness() != CommandListReadiness::Ready) return false;
            Begin(); return true;
        }
        // Optional producers also use persistent logical Begin/End intervals:
        // they may probe admission before an automatic native-buffer restart.
        [[nodiscard]] virtual bool TryPrepareOptionalWork()
        {
            const auto state = QueryReadiness();
            return state == CommandListReadiness::Ready || state == CommandListReadiness::Recording;
        }

        // Optional diagnostics: labels are no-ops when unavailable. Query sets
        // are device-owned, initialized outside rendering, and never reused.
        virtual void BeginDebugLabel(const DebugLabel& label) { ValidateDebugLabel(label); }
        virtual void EndDebugLabel() {}
        virtual void InsertDebugMarker(const DebugLabel& label) { ValidateDebugLabel(label); }
        virtual void InitializeTimestamps(TimestampQuerySet&)
        { throw std::logic_error("GPU timestamps are unavailable."); }
        virtual void WriteTimestamp(TimestampQuerySet&, std::uint32_t)
        { throw std::logic_error("GPU timestamps are unavailable."); }

        // BeginRendering replaces any preceding rendering interval; EndRendering
        // is idempotent while recording. Draw and current-color copy require an
        // open rendering interval. Transfers/barriers inside it retain previous
        // contents and target so subsequent draws can continue without clearing.
        virtual void BeginRendering(const RenderingInfo& info) = 0;
        virtual void EndRendering() = 0;

        virtual void SetPipeline(const GraphicsPipeline& pipeline) = 0;
        virtual void SetViewport(const Viewport& viewport) = 0;
        // Enables clipping until the next BeginRendering resets its render area.
        virtual void SetScissor(const Scissor& scissor) = 0;

        virtual void SetVertexBuffer(
            std::uint32_t slot, const Buffer& buffer, std::uint64_t offset = 0) = 0;
        virtual void SetIndexBuffer(
            const Buffer& buffer, IndexType indexType, std::uint64_t offset = 0) = 0;

        virtual void SetBindingSet(std::uint32_t slot, const BindingSet& bindingSet) = 0;
        // Logical draw flags/scalars; large transforms remain buffer bindings.
        // Backends map these to push constants or compact uniforms.
        struct alignas(16) SmallDrawConstants final
        {
            float materialAlpha = 1.0F;
            std::int32_t alphaTest = 0;
            std::array<std::uint32_t, 2> reserved{};
        };
        virtual void SetSmallConstants(const SmallDrawConstants&)
        { throw std::logic_error("Small draw constants are unavailable."); }
        virtual void SetStencilReference(std::uint32_t reference) = 0;

        virtual void Draw(std::uint32_t vertexCount, std::uint32_t instanceCount = 1,
            std::uint32_t firstVertex = 0, std::uint32_t firstInstance = 0) = 0;
        virtual void DrawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount = 1,
            std::uint32_t firstIndex = 0, std::int32_t vertexOffset = 0,
            std::uint32_t firstInstance = 0) = 0;

        // Explicit copies require CopySrc / CopyDst states and transfer usage.
        // CopySrc may combine with other read states; copies retain that state.
        // Convenience device uploads and readbacks perform their own transitions.
        virtual void CopyBuffer(const Buffer& source, std::uint64_t sourceOffset,
            Buffer& destination, std::uint64_t destinationOffset, std::uint64_t size) = 0;
        virtual void CopyBufferToTexture(
            const Buffer& source, Texture& destination, const BufferTextureCopy& region) = 0;
        virtual void CopyTextureToBuffer(
            const Texture& source, Buffer& destination, const BufferTextureCopy& region) = 0;

        // Explicit transitions cover the whole resource. before must match the
        // state left by its initial state, prior transition or convenience
        // operation. Unknown bits, equal states and states of another resource
        // type/usage/format are rejected before native mutation. Read states may combine;
        // each write state is exclusive. A resized texture starts Undefined.
        // Present is reserved for swapchain images, not independently allocated textures.
        virtual void Transition(
            Buffer& resource, ResourceState before, ResourceState after) = 0;
        virtual void Transition(
            Texture& resource, ResourceState before, ResourceState after) = 0;

        // Push-descriptor style texture binding: bind a texture and the
        // sampler it is read through to a shader texture slot for the draws
        // that follow. A null texture unbinds the slot.
        virtual void BindSampledTexture(
            std::uint32_t slot, const Texture* texture, const Sampler* sampler) = 0;
        // Read back a region of a rendering target's colour: tightly packed
        // rows, bottom row first. Waits for the GPU.
        virtual void ReadColor(const RenderingInfo& info, std::uint32_t x, std::uint32_t y,
            std::uint32_t width, std::uint32_t height, TextureFormat format, void* destination) = 0;
        [[nodiscard]] virtual bool SupportsAsyncReadback() const noexcept { return false; }
        // Same output packing/orientation as ReadColor; submits the copy without
        // waiting. Source storage can be resized/released after this returns.
        // RGB8/RGBA8 output from a resolved 8-bit color target, base mip/layer.
        [[nodiscard]] virtual ReadbackTicket EnqueueReadColor(const RenderingInfo&, std::uint32_t, std::uint32_t,
            std::uint32_t, std::uint32_t, TextureFormat)
        { throw std::logic_error("Asynchronous color readback is unavailable on this graphics backend."); }
        // Copy a region of the current colour attachment into a texture.
        virtual void CopyColorAttachmentToTexture(
            Texture& destination, std::uint32_t width, std::uint32_t height) = 0;

    protected:
        CommandList() = default;
    };
}
