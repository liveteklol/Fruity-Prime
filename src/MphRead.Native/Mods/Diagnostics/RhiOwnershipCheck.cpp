#include "RhiOwnershipCheck.hpp"
#include "../../NativeRuntime/Rhi/GraphicsDevice.hpp"
#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace MphRead::Mods::Diagnostics
{
    namespace
    {
        namespace Rhi = NativeRuntime::Rhi;
        // Public frontend objects with no native backend. Passing these is a
        // deterministic test of unchecked downcasts, without overlapping two
        // real backend sessions during passive/active admission.
        template <typename Base, typename Descriptor>
        class Foreign final : public Base
        {
        public:
            Descriptor value{};
            const Descriptor& Desc() const noexcept override { return value; }
        };
        class ForeignTexture final : public Rhi::Texture
        {
        public:
            Rhi::TextureDesc value{};
            const Rhi::TextureDesc& Desc() const noexcept override { return value; }
            Rhi::TextureHandle Handle() const noexcept override { return {123456}; }
        };
        class ForeignView final : public Rhi::TextureView
        {
        public:
            explicit ForeignView(const Rhi::Texture& source) : texture(source) {}
            const Rhi::TextureViewDesc& Desc() const noexcept override { return desc; }
            const Rhi::Texture& TextureResource() const noexcept override { return texture; }
        private:
            const Rhi::Texture& texture;
            Rhi::TextureViewDesc desc{};
        };
        template <typename Action>
        void Reject(Rhi::GraphicsDevice& device, const char* name, Action&& action)
        {
            const auto submitted = device.Statistics().Submitted;
            try { action(); }
            catch (const std::invalid_argument&)
            {
                if (device.Statistics().Submitted != submitted)
                    throw std::runtime_error(std::string("Rejected resource submitted native work: ") + name);
                return;
            }
            throw std::runtime_error(std::string("Foreign/stale resource was accepted: ") + name);
        }
    }
    void CheckResourceOwnership(Rhi::GraphicsDevice& device)
    {
        using namespace Rhi;
        device.WaitIdle();
        const auto baseline = device.Statistics();
        Foreign<Buffer, BufferDesc> buffer;
        buffer.value = {64, BufferUsage::Vertex | BufferUsage::Index | BufferUsage::Uniform
            | BufferUsage::TransferSrc | BufferUsage::TransferDst, MemoryUsage::CpuToGpu};
        ForeignTexture texture;
        texture.value.width = texture.value.height = 2;
        texture.value.format = TextureFormat::RGBA8Unorm;
        texture.value.usage = TextureUsage::Sampled | TextureUsage::ColorAttachment | TextureUsage::TransferSrc | TextureUsage::TransferDst;
        ForeignView view(texture);
        Foreign<Sampler, SamplerDesc> sampler;
        Foreign<Shader, ShaderDesc> vertex, fragment;
        vertex.value.stage = ShaderStage::Vertex; fragment.value.stage = ShaderStage::Fragment;
        Foreign<BindingLayout, BindingLayoutDesc> layout;
        layout.value.entries = {{0, BindingType::UniformBuffer, ShaderStage::Vertex, 1}};
        Foreign<BindingSet, BindingSetDesc> set;
        set.value = {&layout, {{0, BufferBinding{&buffer, 0, 16}}}};
        Foreign<GraphicsPipeline, GraphicsPipelineDesc> pipeline;
        pipeline.value.vertexShader = &vertex; pipeline.value.fragmentShader = &fragment;
        pipeline.value.pipelineLayout.groups = {layout.value};
        std::array<std::byte, 16> data{};
        Reject(device, "WriteBuffer", [&] { device.WriteBuffer(buffer, 0, data); });
        Reject(device, "ReadBuffer", [&] { device.ReadBuffer(buffer, 0, data); });
        Reject(device, "WriteTexture", [&] { device.WriteTexture(texture, {2, 2, TextureFormat::RGBA8Unorm, data.data()}); });
        Reject(device, "ResizeTexture", [&] { device.ResizeTexture(texture, 3, 3); });
        Reject(device, "CreateTextureView", [&] { (void)device.CreateTextureView(texture, {}); });
        Reject(device, "RetainTexture", [&] { (void)device.RetainTexture(std::make_unique<ForeignTexture>()); });
        Reject(device, "RetainTexture null", [&] { (void)device.RetainTexture({}); });
        Reject(device, "CreateBindingSet", [&] { (void)device.CreateBindingSet(set.value); });
        Reject(device, "CreateGraphicsPipeline", [&] { (void)device.CreateGraphicsPipeline(pipeline.value); });
        RenderingColorAttachment color{&view, LoadOp::Load, StoreOp::Store};
        RenderingDepthStencilAttachment depth{}; depth.view = &view;
        RenderingInfo info{}; info.width = info.height = 2; info.colorAttachments = std::span(&color, 1);
        Reject(device, "CanRender", [&] { (void)device.CanRender(info); });
        RenderingInfo depthInfo{}; depthInfo.width = depthInfo.height = 2; depthInfo.depthStencilAttachment = &depth;
        Reject(device, "DepthBits", [&] { (void)device.DepthBits(depthInfo); });
        auto commands = device.CreateCommandList(); commands->Begin();
        auto upload = device.CreateBuffer({64, BufferUsage::TransferSrc, MemoryUsage::CpuToGpu});
        auto readback = device.CreateBuffer({64, BufferUsage::TransferDst, MemoryUsage::GpuToCpu});
        auto image = device.CreateTexture(texture.value);
        auto filtering = device.CreateSampler({});
        Reject(device, "BeginRendering", [&] { commands->BeginRendering(info); });
        Reject(device, "SetPipeline", [&] { commands->SetPipeline(pipeline); });
        Reject(device, "SetBindingSet", [&] { commands->SetBindingSet(0, set); });
        Reject(device, "SetVertexBuffer", [&] { commands->SetVertexBuffer(0, buffer); });
        Reject(device, "SetIndexBuffer", [&] { commands->SetIndexBuffer(buffer, IndexType::UInt32); });
        Reject(device, "BindSampledTexture image", [&] { commands->BindSampledTexture(0, &texture, filtering.get()); });
        Reject(device, "BindSampledTexture sampler", [&] { commands->BindSampledTexture(0, image.get(), &sampler); });
        Reject(device, "Transition buffer", [&] { commands->Transition(buffer, ResourceState::Undefined, ResourceState::CopySrc); });
        Reject(device, "Transition image", [&] { commands->Transition(texture, ResourceState::Undefined, ResourceState::CopySrc); });
        Reject(device, "CopyBuffer source", [&] { commands->CopyBuffer(buffer, 0, *readback, 0, 4); });
        Reject(device, "CopyBuffer destination", [&] { commands->CopyBuffer(*upload, 0, buffer, 0, 4); });
        Reject(device, "CopyBufferToTexture buffer", [&] { commands->CopyBufferToTexture(buffer, *image, {}); });
        Reject(device, "CopyBufferToTexture image", [&] { commands->CopyBufferToTexture(*upload, texture, {}); });
        Reject(device, "CopyTextureToBuffer image", [&] { commands->CopyTextureToBuffer(texture, *readback, {}); });
        Reject(device, "CopyTextureToBuffer buffer", [&] { commands->CopyTextureToBuffer(*image, buffer, {}); });
        Reject(device, "ReadColor", [&] { commands->ReadColor(info, 0, 0, 2, 2, TextureFormat::RGBA8Unorm, data.data()); });
        Reject(device, "EnqueueReadColor", [&] { (void)commands->EnqueueReadColor(info, 0, 0, 2, 2, TextureFormat::RGBA8Unorm); });
        Reject(device, "CopyColorAttachmentToTexture", [&] { commands->CopyColorAttachmentToTexture(texture, 2, 2); });
        commands->End();
        // The view wrapper deliberately survives destruction of its texture.
        // GPU entry points must inspect its lifetime before TextureResource().
        auto stale = device.CreateTextureView(*image, {}); image.reset();
        color.view = stale.get(); depth.view = stale.get();
        Reject(device, "stale CanRender", [&] { (void)device.CanRender(info); });
        Reject(device, "stale DepthBits", [&] { (void)device.DepthBits(depthInfo); });
        commands->Begin();
        Reject(device, "stale BeginRendering", [&] { commands->BeginRendering(info); });
        Reject(device, "stale ReadColor", [&] { commands->ReadColor(info, 0, 0, 2, 2, TextureFormat::RGBA8Unorm, data.data()); });
        Reject(device, "stale EnqueueReadColor", [&] { (void)commands->EnqueueReadColor(info, 0, 0, 2, 2, TextureFormat::RGBA8Unorm); });
        commands->End();
        auto sampledLayout = device.CreateBindingLayout({{{0, BindingType::SampledTexture, ShaderStage::Fragment, 1}}});
        Reject(device, "stale CreateBindingSet", [&] { (void)device.CreateBindingSet({sampledLayout.get(), {{0, TextureBinding{stale.get()}}}}); });
        const auto beforeRelease = device.Statistics();
        stale.reset(); sampledLayout.reset(); filtering.reset(); upload.reset(); readback.reset(); commands.reset();
        const auto released = device.Statistics();
        if (released.DeviceWideWaits != beforeRelease.DeviceWideWaits || released.HostWaits != beforeRelease.HostWaits)
            throw std::runtime_error("Ownership fixture release inserted a GPU wait.");
        device.WaitIdle();
        const auto final = device.Statistics();
        if (final.LiveObjects() != baseline.LiveObjects() || final.Retired || device.DrainErrors())
            throw std::runtime_error("Ownership rejection changed native lifetime or raised graphics errors.");
        std::cout << "[resource ownership] PASS; foreign frontend/empty retain/stale texture view rejected; release waits delta=0; baseline restored\n";
    }
}
