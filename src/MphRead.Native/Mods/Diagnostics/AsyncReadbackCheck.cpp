#include "AsyncReadbackCheck.hpp"
#include <string>
#include "../../NativeRuntime/Rhi/GraphicsDevice.hpp"
#include "../../NativeRuntime/Rhi/BackendSession.hpp"
#include <array>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace MphRead::Mods::Diagnostics
{
    namespace
    {
        namespace Rhi = NativeRuntime::Rhi;
        void Expect(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
        Rhi::ReadbackResult Await(Rhi::ReadbackTicket& ticket)
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!ticket.IsReady())
            {
                if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("Async GPU copy did not complete in five seconds.");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return ticket.MapResult();
        }
    }
    void CheckAsyncReadback(Rhi::GraphicsDevice& device)
    {
        using namespace Rhi;
        Expect(device.SupportsAsyncReadback(), "Conformance requires real asynchronous readback.");
        device.WaitIdle(); // fixture setup boundary, excluded from steady-state deltas
        device.SetReadbackLimits({2, 1024});
        auto source = device.CreateBuffer({64, BufferUsage::TransferSrc, MemoryUsage::CpuToGpu});
        std::array<std::byte, 64> expected{};
        for (std::size_t i = 0; i < expected.size(); ++i) expected[i] = std::byte(i * 19 + 5);
        device.WriteBuffer(*source, 0, expected);
        const auto baseline = device.Statistics();
        auto first = device.EnqueueReadback(*source, 7, 27);
        auto second = device.EnqueueReadback(*source, 1, 11);
        Expect(first && second, "Async buffer copies were not admitted.");
        const auto issued = device.Statistics();
        Expect(!device.EnqueueReadback(*source, 0, 1), "Readback slot backpressure did not reject a third request.");
        const auto rejected = device.Statistics();
        Expect(rejected.Submitted == issued.Submitted && rejected.LiveObjects() == issued.LiveObjects(),
            "Rejected readback issued work or allocated GPU storage.");
        source.reset(); // native retirement must cover both submitted copies
        auto a = Await(first), b = Await(second);
        Expect(a.Bytes().size() == 27 && std::equal(a.Bytes().begin(), a.Bytes().end(), expected.begin() + 7), "Async buffer offset contents differ.");
        Expect(b.Bytes().size() == 11 && std::equal(b.Bytes().begin(), b.Bytes().end(), expected.begin() + 1), "Second async buffer contents differ.");
        first.Cancel(); second.Cancel(); device.PollReadbacks();
        auto pressureSource = device.CreateBuffer({8, BufferUsage::TransferSrc, MemoryUsage::CpuToGpu});
        Expect(!device.EnqueueReadback(*pressureSource, 0, 1), "Output leases failed to retain slot pressure.");
        pressureSource.reset();
        a = {}; b = {}; device.PollReadbacks();
        Expect(device.ReadbackStatistics().requests == 0 && device.ReadbackStatistics().bytes == 0, "Completed leases leaked readback quota.");
        device.SetReadbackLimits({8, 1});
        auto byteSource = device.CreateBuffer({8, BufferUsage::TransferSrc, MemoryUsage::CpuToGpu});
        const auto beforeByteRejection = device.Statistics();
        Expect(!device.EnqueueReadback(*byteSource, 0, 1), "Readback byte budget did not reject before native issue.");
        const auto afterByteRejection = device.Statistics();
        Expect(beforeByteRejection.Submitted == afterByteRejection.Submitted
            && beforeByteRejection.LiveObjects() == afterByteRejection.LiveObjects(), "Byte admission rejection changed GPU resources.");
        byteSource.reset();
        device.SetReadbackLimits({8, 64ULL << 20});
        std::vector<std::unique_ptr<CommandList>> lists;
        for (const auto format : {Rhi::TextureFormat::RGBA8Unorm, Rhi::TextureFormat::BGRA8Unorm, Rhi::TextureFormat::RGB8Unorm})
        {
            TextureDesc desc{};
            desc.width = 641; desc.height = 127; desc.format = format;
            desc.usage = TextureUsage::ColorAttachment | TextureUsage::TransferSrc;
            auto image = device.CreateTexture(desc);
            auto view = device.CreateTextureView(*image, {});
            auto commands = device.CreateCommandList(); commands->Begin();
            RenderingColorAttachment color{view.get(), LoadOp::Clear, StoreOp::Store, {1, 0, 0, 0}};
            RenderingInfo target{}; target.width = desc.width; target.height = desc.height; target.colorAttachments = std::span(&color, 1);
            commands->BeginRendering(target); commands->EndRendering();
            color.clearValue = {0, 1, 0, 1};
            target.renderArea = {3, 2, 637, 40};
            commands->BeginRendering(target); commands->EndRendering();
            auto ticket = commands->EnqueueReadColor(target, 3, 2, 637, 123, Rhi::TextureFormat::RGB8Unorm);
            auto rgbaTicket = commands->EnqueueReadColor(target, 3, 2, 637, 123, Rhi::TextureFormat::RGBA8Unorm);
            Expect(bool(ticket), "Async pitched color copy was rejected.");
            Expect(bool(rgbaTicket), "Async RGBA copy was rejected.");
            device.ResizeTexture(*image, 32, 16); // old generation is still read by the GPU
            view.reset(); image.reset();
            const auto pixels = Await(ticket).Bytes();
            // Keep the ticket here: it owns the same immutable payload as the temporary lease.
            Expect(pixels.size() == 637 * 123 * 3, "Async RGB packing size differs.");
            for (std::size_t i = 0; i < pixels.size(); ++i)
                Expect(pixels[i] == (i % 3 == (i / (637 * 3) < 40 ? 1U : 0U) ? std::byte{255} : std::byte{0}),
                    "Async color/subregion/old generation pixels differ.");
            const auto rgba = Await(rgbaTicket);
            Expect(rgba.Bytes().size() == 637 * 123 * 4, "Async RGBA packing size differs.");
            for (std::size_t i = 0; i < rgba.Bytes().size(); ++i)
            {
                const bool green = i / (637 * 4) < 40;
                const bool opaque = green || format == Rhi::TextureFormat::RGB8Unorm;
                const auto expectedByte = i % 4 == (green ? 1U : 0U) || (i % 4 == 3 && opaque)
                    ? std::byte{255} : std::byte{0};
                Expect(rgba.Bytes()[i] == expectedByte, "Async RGBA channel order or alpha differs.");
            }
            lists.push_back(std::move(commands));
        }
        // More captures than a Vulkan list's two recording slots: admission
        // must return backpressure instead of using normal frame-throttle waits.
        {
            TextureDesc desc{};
            desc.width = 17; desc.height = 7; desc.format = Rhi::TextureFormat::RGBA8Unorm;
            desc.usage = TextureUsage::ColorAttachment | TextureUsage::TransferSrc;
            auto image = device.CreateTexture(desc);
            auto view = device.CreateTextureView(*image, {});
            auto commands = device.CreateCommandList(); commands->Begin();
            RenderingColorAttachment color{view.get(), LoadOp::Clear, StoreOp::Store, {0, 0, 1, 1}};
            RenderingInfo target{}; target.width = desc.width; target.height = desc.height;
            target.colorAttachments = std::span(&color, 1);
            commands->BeginRendering(target); commands->EndRendering();
            std::vector<ReadbackTicket> tickets;
            for (unsigned request = 0; request < 32; ++request)
                if (auto ticket = commands->EnqueueReadColor(target, 0, 0, 17, 7, Rhi::TextureFormat::RGB8Unorm))
                    tickets.push_back(std::move(ticket));
            Expect(!tickets.empty() && tickets.size() <= 8, "Capture burst admission violated its request bound.");
            view.reset(); image.reset();
            for (auto& ticket : tickets)
            {
                const auto result = Await(ticket);
                for (std::size_t i = 0; i < result.Bytes().size(); ++i)
                    Expect(result.Bytes()[i] == (i % 3 == 2 ? std::byte{255} : std::byte{0}), "Capture burst lost output pixels.");
            }
            lists.push_back(std::move(commands));
        }
        const auto after = device.Statistics();
        Expect(after.HostWaits == baseline.HostWaits && after.DeviceWideWaits == baseline.DeviceWideWaits,
            "Asynchronous issue/poll/map/resize/release waited for the GPU.");
        lists.clear(); device.PollReadbacks(); device.TrimCaches(); device.WaitIdle();
        Expect(device.ReadbackStatistics().requests == 0 && device.ReadbackStatistics().bytes == 0,
            "Color readback left leased output or staging storage.");
        {
            const auto left = device.Statistics();
            Expect(left.LiveObjects() == 0 && left.Retired == 0, "Async readback fixture leaked GPU resources: textures="
                + std::to_string(left.Textures) + " buffers=" + std::to_string(left.Buffers) + " samplers="
                + std::to_string(left.Samplers) + " shaders=" + std::to_string(left.Shaders) + " programs="
                + std::to_string(left.Programs) + " framebuffers=" + std::to_string(left.Framebuffers) + " timestamps="
                + std::to_string(left.TimestampSets) + " retired=" + std::to_string(left.Retired) + ".");
        }
        std::cout << "[async readback] PASS; buffer offsets/source release; RGB odd rows/subregion; RGBA channels/alpha; RGBA/BGRA/RGB source resize/release; lease/slot backpressure; 32-request capture burst; host/device waits delta=0; release=0\n";
    }
    void CheckReadbackSessionLifetime(const Rhi::BackendProvider& provider)
    {
        using namespace Rhi;
        for (unsigned cycle = 0; cycle < 4; ++cycle)
        {
            auto session = provider.CreateSession({true});
            auto& device = session->Device();
            auto source = device.CreateBuffer({16, BufferUsage::TransferSrc, MemoryUsage::CpuToGpu});
            const std::array<std::uint32_t, 4> values{cycle, 0x12345678, 0x87654321, 0xABCDEF00};
            device.WriteBuffer(*source, 0, std::as_bytes(std::span(values)));
            auto mapped = device.EnqueueReadback(*source, 0, 16);
            auto lease = Await(mapped);
            auto pending = device.EnqueueReadback(*source, 0, 16);
            auto abandoned = device.EnqueueReadback(*source, 0, 16); abandoned.Cancel();
            session->Shutdown();
            Expect(pending.Status() == ReadbackStatus::Cancelled && !pending.IsReady(), "Shutdown left an unmapped native ticket usable.");
            Expect(mapped.Status() == ReadbackStatus::Complete && mapped.MapResult().Bytes().size() == 16, "Shutdown invalidated completed output.");
            Expect(std::memcmp(lease.Bytes().data(), values.data(), 16) == 0, "Late CPU output lease changed during shutdown.");
            Expect(session->ValidationErrors() == 0, "Readback shutdown reported Vulkan lifetime errors.");
            source.reset(); pending.Cancel(); mapped.Cancel(); lease = {};
        }
        std::cout << "[async readback] session lifetime PASS; four shutdown/recreate cycles; pending cancel/abandoned copy/late CPU output\n";
    }
}
