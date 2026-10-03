#pragma once

#include "Backend.hpp"
#include "Bindings.hpp"
#include "Capabilities.hpp"
#include "CommandList.hpp"
#include "FrameContext.hpp"
#include "PresentationScheduler.hpp"
#include "MemoryBudget.hpp"
#include "Pipeline.hpp"
#include "Resources.hpp"
#include "Readback.hpp"
#include "Swapchain.hpp"

#include <memory>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>

namespace MphRead::NativeRuntime::Rhi
{
    class GraphicsDevice
    {
    public:
        virtual ~GraphicsDevice() = default;
        GraphicsDevice(const GraphicsDevice&) = delete;
        GraphicsDevice& operator=(const GraphicsDevice&) = delete;
        GraphicsDevice(GraphicsDevice&&) = delete;
        GraphicsDevice& operator=(GraphicsDevice&&) = delete;

        [[nodiscard]] virtual GraphicsBackend GetBackend() const noexcept = 0;
        [[nodiscard]] virtual const Capabilities& GetCapabilities() const noexcept = 0;
        [[nodiscard]] virtual MemoryBudgetSnapshot MemoryBudget() const { return {}; }
        [[nodiscard]] virtual MemoryTelemetry MemoryUsageTelemetry() const { return {}; }
        // Null means unsupported or bounded native capacity is occupied.
        [[nodiscard]] virtual std::unique_ptr<TimestampQuerySet> CreateTimestampQuerySet(std::uint32_t, std::string_view)
        { return {}; }

        [[nodiscard]] virtual std::unique_ptr<Buffer> CreateBuffer(const BufferDesc& desc) = 0;
        [[nodiscard]] virtual std::unique_ptr<Texture> CreateTexture(const TextureDesc& desc) = 0;
        [[nodiscard]] virtual std::unique_ptr<TextureView> CreateTextureView(
            Texture& texture, const TextureViewDesc& desc) = 0;
        [[nodiscard]] virtual std::unique_ptr<Sampler> CreateSampler(const SamplerDesc& desc) = 0;
        [[nodiscard]] virtual std::unique_ptr<Shader> CreateShader(const ShaderDesc& desc) = 0;
        [[nodiscard]] virtual std::unique_ptr<BindingLayout> CreateBindingLayout(
            const BindingLayoutDesc& desc) = 0;
        [[nodiscard]] virtual std::unique_ptr<BindingSet> CreateBindingSet(
            const BindingSetDesc& desc) = 0;
        [[nodiscard]] virtual std::unique_ptr<GraphicsPipeline> CreateGraphicsPipeline(
            const GraphicsPipelineDesc& desc) = 0;
        [[nodiscard]] virtual std::unique_ptr<CommandList> CreateCommandList() = 0;

        // A texture whose handle the caller chose, for the few callers that
        // keep a reserved range of handles of their own (the map thumbnails).
        // Throws if a live texture already has that handle.
        [[nodiscard]] virtual std::unique_ptr<Texture> CreateTexture(
            const TextureDesc& desc, TextureHandle handle) = 0;
        // The live texture with this handle, whoever created it, or null.
        [[nodiscard]] virtual Texture* FindTexture(TextureHandle handle) noexcept = 0;
        // Hand a texture to the device to keep for as long as the device
        // lives: a texture written under a caller's reserved handle outlives
        // every scene that draws it.
        virtual Texture& RetainTexture(std::unique_ptr<Texture> texture) = 0;

        // Replace a texture's contents and, if the size differs, its extent.
        // The texture keeps its handle: whatever holds it keeps working.
        virtual void WriteTexture(Texture& texture, const TextureWrite& write) = 0;
        // Upload or read a byte range in a buffer. Backends may implement
        // uploads through a staging allocation when the destination is GPU-only.
        virtual void WriteBuffer(Buffer&, std::uint64_t, std::span<const std::byte>)
        {
            throw std::logic_error("Buffer uploads are not implemented by this graphics backend.");
        }
        // Synchronous readback for diagnostics and CPU consumers. The range
        // must fit in a buffer created with MemoryUsage::GpuToCpu.
        virtual void ReadBuffer(Buffer&, std::uint64_t, std::span<std::byte>)
        {
            throw std::logic_error("Buffer readback is not implemented by this graphics backend.");
        }
        [[nodiscard]] virtual bool SupportsAsyncReadback() const noexcept { return false; }
        [[nodiscard]] virtual ReadbackTicket EnqueueReadback(Buffer&, std::uint64_t, std::uint64_t)
        { throw std::logic_error("Asynchronous readback is unavailable on this graphics backend."); }
        virtual void PollReadbacks() {}
        virtual void SetReadbackLimits(ReadbackLimits)
        { throw std::logic_error("Asynchronous readback is unavailable on this graphics backend."); }
        [[nodiscard]] virtual ReadbackUsage ReadbackStatistics() const { return {}; }
        // Re-specify a render target's storage at a new extent, contents
        // undefined, keeping its handle and every view of it.
        virtual void ResizeTexture(Texture& texture, std::uint32_t width, std::uint32_t height) = 0;

        // The frame lifetime contract (FrameContext.hpp). BeginFrame retires
        // the frame that last used its slot and destroys what that frame had
        // retired; EndFrame marks the end of the frame's GPU work; WaitIdle
        // waits for all of it and destroys everything retired.
        virtual FrameContext BeginFrame() = 0;
        virtual void EndFrame() = 0;
        virtual void WaitIdle() = 0;
        // A bounded presentation frame budget uses the most recent accepted
        // queue submission, never the serial of the next recycled frame slot.
        // No new submission or device-wide idle is permitted here.
        [[nodiscard]] virtual bool WaitForLatestSubmission(std::uint64_t timeoutNanoseconds)
        { (void)timeoutNanoseconds; return false; }
        [[nodiscard]] virtual PresentationWaitStatistics PresentationWaits() const noexcept { return {}; }
        [[nodiscard]] virtual LowLatencyCapabilities LowLatencyCaps() const noexcept
        { return {}; }
        [[nodiscard]] virtual GpuResourceStatistics Statistics() const = 0;
        // Release cached objects not needed by live frontend resources. Native
        // destruction still follows submission completion; this does not wait.
        virtual void TrimCaches() {}

        // Who made the device and what it runs, for the debug log.
        [[nodiscard]] virtual std::string AdapterDescription() = 0;
        // Take every pending device error and return the first as the
        // backend's own code (0: none). Diagnostics only.
        [[nodiscard]] virtual std::int32_t DrainErrors() = 0;

        // Whether this combination of attachments can be rendered to on this
        // device: the question a driver answers about a depth texture it may
        // refuse to attach.
        [[nodiscard]] virtual bool CanRender(const RenderingInfo& info) = 0;
        // The depth buffer's bit depth as the device reports it for these
        // attachments, or 0 when it will not say.
        [[nodiscard]] virtual std::uint32_t DepthBits(const RenderingInfo& info) = 0;

    protected:
        GraphicsDevice() = default;
    };
}
