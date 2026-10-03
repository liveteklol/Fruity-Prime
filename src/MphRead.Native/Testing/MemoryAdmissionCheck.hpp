#pragma once
#include "../NativeRuntime/Rhi/GraphicsDevice.hpp"
#include "../NativeRuntime/Rhi/BackendError.hpp"
#include <array>
#include <iostream>
#include <stdexcept>

namespace MphRead::NativeRuntime::Rhi::Testing
{
    // Real GPU fixture shared by backend diagnostics. The injected limit only
    // exercises admission; native allocation failures remain a separate gate.
    template<class SetCeiling, class Capture>
    void CheckMemoryAdmission(GraphicsDevice& device, SetCeiling setCeiling, Capture capture)
    {
        const auto expect = [](bool value, const char* reason) { if (!value) throw std::runtime_error(reason); };
        auto budget = device.MemoryBudget();
        expect(budget.HeapCount && budget.HeapCount <= MemoryBudgetSnapshot::MaxHeaps, "Missing backend memory snapshot.");
        TextureDesc desc{}; desc.width = desc.height = 16; desc.format = TextureFormat::RGBA8Unorm;
        desc.usage = TextureUsage::Sampled | TextureUsage::ColorAttachment | TextureUsage::TransferSrc | TextureUsage::TransferDst;
        auto texture = device.CreateTexture(desc); auto view = device.CreateTextureView(*texture, {});
        std::array<std::array<unsigned char, 4>, 256> data{};
        for (auto& pixel : data) pixel = {23, 57, 91, 255};
        device.WriteTexture(*texture, {16, 16, TextureFormat::RGBA8Unorm, data.data()});
        device.WaitIdle();
        const auto identity = capture(*texture, *view);
        const auto handle = texture->Handle();
        const auto before = device.MemoryUsageTelemetry();
        expect(!before.PendingRequests && !before.PendingBytes, "Memory reservation survived completion.");
        {
            struct Reset final { SetCeiling& Setter; ~Reset() { Setter(0); } } reset{setCeiling};
            setCeiling(1);
            unsigned rejected = 0;
            const auto attempt = [&](auto operation) {
                try { operation(); }
                catch (const BackendError& error)
                {
                    expect(error.Backend() == device.GetBackend() && error.Kind() == BackendErrorKind::OutOfMemory
                        && error.NativeCode() == 0, "Admission failure invented or lost a native error code.");
                    ++rejected;
                }
            };
            attempt([&] { device.ResizeTexture(*texture, 32, 32); });
            attempt([&] {
                BufferDesc buffer{}; buffer.size = 64; buffer.usage = BufferUsage::Vertex;
                buffer.memoryUsage = MemoryUsage::GpuOnly; (void)device.CreateBuffer(buffer);
            });
            expect(rejected == 2 && capture(*texture, *view) == identity && texture->Handle() == handle
                && texture->Desc() == desc && &view->TextureResource() == texture.get(),
                "Memory denial changed the old image/view/extent/handle or admitted an allocation.");
        }
        const auto after = device.MemoryUsageTelemetry();
        expect(after.DeniedRequests == before.DeniedRequests + 2 && after.AcceptedRequests == before.AcceptedRequests
            && after.NativeFailures == before.NativeFailures && !after.PendingRequests && !after.PendingBytes,
            "Denied memory allocation reached the native allocator or leaked a reservation.");
        RenderingColorAttachment color{view.get(), LoadOp::Load, StoreOp::Store, {}};
        RenderingInfo info{}; info.width = info.height = 16; info.colorAttachments = std::span(&color, 1);
        auto commands = device.CreateCommandList(); std::array<unsigned char, 4> pixel{};
        commands->ReadColor(info, 8, 8, 1, 1, TextureFormat::RGBA8Unorm, pixel.data());
        expect(pixel == data[0], "Denied resize damaged the old image pixels.");
        commands.reset(); view.reset(); texture.reset(); device.WaitIdle();
        budget = device.MemoryBudget();
        std::cout << "[memory admission] " << (device.GetBackend() == GraphicsBackend::OpenGl ? "OpenGL" : "Vulkan")
            << " PASS; heaps=" << budget.HeapCount << "; source=" << static_cast<int>(budget.Heaps[0].Source)
            << "; budget=" << budget.Heaps[0].BudgetBytes << "; usage=" << budget.Heaps[0].UsageBytes
            << "; backing=" << budget.Heaps[0].ReservedBytes
            << "; denied resize/buffer=2; old native/image/view/extent/handle/pixels retained; pending=0\n";
    }
}
