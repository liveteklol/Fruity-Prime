#include "RhiConformanceCheck.hpp"
#include "AsyncReadbackCheck.hpp"
#include "RhiOwnershipCheck.hpp"
#include "RhiStateCheck.hpp"
#include "../../NativeRuntime/Rhi/OpenGL/OpenGlDiagnostics.hpp"
#include "../../NativeRuntime/Rhi/OpenGL/OpenGlDevice.hpp"
#include "../../NativeRuntime/Rhi/Vulkan/VulkanGraphicsDevice.hpp"
#include "../Branding.hpp"
#include "../../Renderer.hpp"
#include "../../NativeRuntime/Rhi/BackendSession.hpp"
#include "../../Testing/RhiConformanceShaderSource.hpp"
#if defined(FRUITY_HAS_VULKAN)
#include "FruityRhiConformanceShaders.hpp"
#endif
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <chrono>
#include <thread>
#include <cmath>

namespace MphRead::Mods::Diagnostics
{
    namespace
    {
        namespace Rhi = NativeRuntime::Rhi;
        void Expect(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
        template <class T> auto Bytes(const T& values) { return std::as_bytes(std::span(values)); }
        Rhi::ShaderDesc Shader(Rhi::GraphicsBackend backend, Rhi::ShaderStage stage)
        {
            Rhi::ShaderDesc desc{};
            desc.stage = stage;
            if (backend == Rhi::GraphicsBackend::OpenGl)
            {
                desc.format = Rhi::ShaderCodeFormat::GlslSource;
                const auto source = stage == Rhi::ShaderStage::Vertex ? Rhi::TestingShaderAssets::Vertex : Rhi::TestingShaderAssets::Fragment;
                desc.code.resize(source.size());
                std::memcpy(desc.code.data(), source.data(), source.size());
            }
            else
            {
#if defined(FRUITY_HAS_VULKAN)
                const auto words = stage == Rhi::ShaderStage::Vertex
                    ? std::span<const std::uint32_t>(Rhi::TestingShaderAssets::vert) : std::span<const std::uint32_t>(Rhi::TestingShaderAssets::frag);
                const auto bytes = std::as_bytes(words);
                desc.code.assign(bytes.begin(), bytes.end());
#else
                throw std::runtime_error("Vulkan shader fixture unavailable in this build.");
#endif
            }
            return desc;
        }
        void ExerciseGpuDiagnostics(Rhi::GraphicsDevice& device)
        {
            using namespace Rhi;
            if (!device.GetCapabilities().supportsTimestampQueries)
            { std::cout << "[gpu diagnostics] timestamps unavailable; optional path skipped\n"; return; }
            auto timestamps = device.CreateTimestampQuerySet(3, "Conformance GPU transfer");
            Expect(timestamps != nullptr, "Supported GPU timestamp creation failed.");
            std::array<std::uint64_t, 3> values{77, 88, 99};
            Expect(timestamps->ReadResults(values) == TimestampStatus::Pending && values[0] == 77,
                "Unwritten GPU timestamps became ready or changed output.");
            auto source = device.CreateBuffer({1048576, BufferUsage::TransferSrc, MemoryUsage::CpuToGpu});
            auto destination = device.CreateBuffer({1048576, BufferUsage::TransferDst, MemoryUsage::GpuToCpu});
            std::vector<std::byte> payload(1048576, std::byte{0x39}); device.WriteBuffer(*source, 0, payload);
            auto commands = device.CreateCommandList();
            (void)device.BeginFrame(); commands->Begin();
            const auto before = device.Statistics();
            commands->InitializeTimestamps(*timestamps); commands->WriteTimestamp(*timestamps, 0);
            commands->BeginDebugLabel({"Conformance outer"}); commands->BeginDebugLabel({"GPU transfer"});
            commands->InsertDebugMarker({"Copy 1 MiB"});
            commands->Transition(*source, ResourceState::Undefined, ResourceState::CopySrc);
            commands->Transition(*destination, ResourceState::Undefined, ResourceState::CopyDst);
            commands->CopyBuffer(*source, 0, *destination, 0, payload.size());
            commands->WriteTimestamp(*timestamps, 1); commands->EndDebugLabel(); commands->EndDebugLabel();
            Expect(timestamps->ReadResults(values) == TimestampStatus::Pending && values[0] == 77,
                "Partial timestamp writes became complete or changed output.");
            commands->WriteTimestamp(*timestamps, 2); device.EndFrame();
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (timestamps->ReadResults(values) == TimestampStatus::Pending)
            {
                Expect(std::chrono::steady_clock::now() < deadline, "GPU timestamps never became available.");
                std::this_thread::yield();
            }
            const auto after = device.Statistics();
            Expect(before.HostWaits == after.HostWaits && before.DeviceWideWaits == after.DeviceWideWaits,
                "GPU timestamp writes or result polling introduced a wait.");
            const auto elapsed = TimestampNanoseconds(values[0], values[1], timestamps->Properties());
            Expect(std::isfinite(elapsed) && elapsed > 0, "GPU transfer duration is invalid.");
            bool rejected = false;
            try { commands->InitializeTimestamps(*timestamps); } catch (const std::logic_error&) { rejected = true; }
            Expect(rejected, "A submitted GPU timestamp pool was reset for reuse.");
            std::vector<std::unique_ptr<TimestampQuerySet>> held;
            for (unsigned i = 1; i < TimestampBudget::Limit; ++i)
            { held.push_back(device.CreateTimestampQuerySet(2, "Bounded diagnostic")); Expect(held.back() != nullptr, "Timestamp capacity too small."); }
            Expect(!device.CreateTimestampQuerySet(2, "Over capacity"), "GPU timestamp capacity exceeded.");
            held.pop_back();
            Expect(!device.CreateTimestampQuerySet(2, "Retired capacity"), "Retired GPU query escaped capacity accounting.");
            held.clear(); timestamps.reset(); commands.reset(); source.reset(); destination.reset();
            device.WaitIdle();
            Expect(!device.Statistics().TimestampSets, "GPU timestamp native resources leaked.");
            std::cout << "[gpu diagnostics] PASS; labels=" << device.GetCapabilities().supportsDebugLabels
                << "; transfer_ns=" << elapsed << "; polling host/device waits delta=0; bounded native sets; release=0\n";
        }
        void ExerciseRecordingInterval(Rhi::GraphicsDevice& device)
        {
            using namespace Rhi;
            auto source = device.CreateBuffer({64, BufferUsage::TransferSrc, MemoryUsage::CpuToGpu});
            auto destination = device.CreateBuffer({64, BufferUsage::TransferDst, MemoryUsage::GpuToCpu});
            const std::array<unsigned, 16> payload{1, 2, 3, 4, 11, 12, 13, 14,
                21, 22, 23, 24, 31, 32, 33, 34};
            device.WriteBuffer(*source, 0, Bytes(payload));
            auto first = device.CreateCommandList();
            auto second = device.CreateCommandList();
            unsigned rejections = 0;
            const auto reject = [&](auto action) {
                const auto before = device.Statistics().Submitted;
                bool rejected = false;
                try { action(); } catch (const std::logic_error&) { rejected = true; }
                Expect(rejected, "Invalid Begin/End interval was accepted.");
                Expect(device.Statistics().Submitted == before, "Rejected Begin/End submitted work.");
                ++rejections;
            };
            reject([&] { first->End(); });
            (void)device.BeginFrame(); first->Begin();
            reject([&] { first->Begin(); });
            first->Transition(*source, ResourceState::Undefined, ResourceState::CopySrc);
            first->Transition(*destination, ResourceState::Undefined, ResourceState::CopyDst);
            const bool labels = device.GetCapabilities().supportsDebugLabels;
            if (labels) first->BeginDebugLabel({"Interval survives internal submission"});
            first->CopyBuffer(*source, 0, *destination, 0, 16);
            // Vulkan must submit the first native buffer before the second list
            // records. The first caller-owned interval is still open.
            second->Begin(); second->CopyBuffer(*source, 16, *destination, 16, 16); second->End();
            reject([&] { first->Begin(); });
            if (labels)
            {
                reject([&] { first->End(); });
                first->EndDebugLabel();
            }
            first->CopyBuffer(*source, 32, *destination, 32, 16);
            device.EndFrame();
            reject([&] { first->Begin(); });
            first->End();
            reject([&] { first->End(); });
            first->Begin(); first->CopyBuffer(*source, 48, *destination, 48, 16); first->End();
            std::array<unsigned, 16> output{};
            device.ReadBuffer(*destination, 0, std::as_writable_bytes(std::span(output)));
            Expect(output == payload, "Interleaved recording intervals lost or reordered GPU copies.");
            // Even an empty interval can be internally submitted at EndFrame.
            (void)device.BeginFrame(); first->Begin(); device.EndFrame(); first->End();
            Expect(device.DrainErrors() == 0, "Recording interval check raised native errors.");
            first.reset(); second.reset(); source.reset(); destination.reset(); device.WaitIdle();
            Expect(device.Statistics().LiveObjects() == 0 && device.Statistics().Retired == 0,
                "Recording interval check leaked native objects.");
            std::cout << "[recording interval] PASS; rejected=" << rejections
                << "; interleaved/frame submissions; ordered GPU copy; empty interval; release=0\n";
        }
        void ExerciseUnframedLifetime(Rhi::GraphicsDevice& device)
        {
            using namespace Rhi;
            const auto baseline = device.Statistics();
            for (unsigned cycle = 0; cycle < 16; ++cycle)
            {
                auto commands = device.CreateCommandList();
                auto source = device.CreateBuffer({16, BufferUsage::TransferSrc, MemoryUsage::CpuToGpu});
                auto destination = device.CreateBuffer({16, BufferUsage::TransferDst, MemoryUsage::GpuToCpu});
                const std::array<unsigned, 4> payload{cycle, 0xAABBCCDD, cycle * 37, 0x10203040};
                device.WriteBuffer(*source, 0, Bytes(payload));
                commands->Begin();
                commands->Transition(*source, ResourceState::Undefined, ResourceState::CopySrc);
                commands->Transition(*destination, ResourceState::Undefined, ResourceState::CopyDst);
                commands->CopyBuffer(*source, 0, *destination, 0, 16); commands->End();
                std::array<unsigned, 4> result{};
                device.ReadBuffer(*destination, 0, std::as_writable_bytes(std::span(result)));
                Expect(result == payload, "Unframed transfer/readback contents differ.");
                const auto beforeRelease = device.Statistics();
                source.reset(); destination.reset(); commands.reset();
                const auto afterRelease = device.Statistics();
                Expect(afterRelease.DeviceWideWaits == beforeRelease.DeviceWideWaits
                    && afterRelease.HostWaits == beforeRelease.HostWaits,
                    "Ordinary unframed resource release waited for the GPU.");
            }
            const auto submitted = device.Statistics();
            Expect(submitted.Submitted > baseline.Submitted && submitted.CompletedFrame == baseline.CompletedFrame,
                "Unframed work must advance submission serials independently of frames.");
            device.WaitIdle();
            const auto completed = device.Statistics();
            Expect(completed.Completed == completed.Submitted && completed.Retired == 0 && completed.LiveObjects() == 0,
                "Explicit idle must complete unframed work and release its native resources.");
        }
        struct HeldResources final
        {
            std::vector<std::shared_ptr<void>> Leases;
            Rhi::CommandList* Commands = nullptr;
            Rhi::GraphicsPipeline* Pipeline = nullptr;
            Rhi::BindingLayout* Layout = nullptr;
            Rhi::BindingSet* Set = nullptr;
            Rhi::Texture* Texture = nullptr;
            Rhi::TextureView* View = nullptr;
            template <typename T> void Keep(std::unique_ptr<T> object) { Leases.emplace_back(std::move(object)); }
        };

        void Exercise(Rhi::GraphicsDevice& device, HeldResources* held = nullptr)
        {
            using namespace Rhi;
            auto commands = device.CreateCommandList();
            // Byte-range GPU copy with distinct source/destination offsets.
            BufferDesc uploadDesc{64, BufferUsage::TransferSrc, MemoryUsage::CpuToGpu};
            BufferDesc readbackDesc{64, BufferUsage::TransferDst, MemoryUsage::GpuToCpu};
            auto upload = device.CreateBuffer(uploadDesc), readback = device.CreateBuffer(readbackDesc);
            std::array<std::byte, 32> payload{};
            for (std::size_t i = 0; i < payload.size(); ++i) payload[i] = static_cast<std::byte>(i * 19 + 7);
            device.WriteBuffer(*upload, 5, payload);
            commands->Begin();
            commands->Transition(*upload, ResourceState::Undefined, ResourceState::CopySrc);
            commands->Transition(*readback, ResourceState::Undefined, ResourceState::CopyDst);
            commands->CopyBuffer(*upload, 5, *readback, 9, payload.size()); commands->End();
            std::array<std::byte, 32> copied{}; device.ReadBuffer(*readback, 9, copied);
            Expect(copied == payload, "GPU buffer copy/readback contents differ.");
            bool rejected = false;
            commands->Begin();
            try { commands->CopyBuffer(*upload, 63, *readback, 0, 2); } catch (const std::out_of_range&) { rejected = true; }
            Expect(rejected, "Out-of-range GPU buffer copy was accepted.");
            commands->End();

            // Pitched image transfer through GPU buffers, preserving padding.
            TextureDesc imageDesc{};
            imageDesc.width = 2; imageDesc.height = 2; imageDesc.format = Rhi::TextureFormat::RGBA8Unorm;
            imageDesc.usage = TextureUsage::Sampled | TextureUsage::TransferSrc | TextureUsage::TransferDst;
            auto image = device.CreateTexture(imageDesc);
            std::array<std::byte, 32> pixels{};
            for (std::size_t i = 0; i < 8; ++i) { pixels[4+i] = static_cast<std::byte>(i*13+1); pixels[16+i] = static_cast<std::byte>(i*17+9); }
            device.WriteBuffer(*upload, 0, pixels);
            commands->Begin(); commands->Transition(*image, ResourceState::Undefined, ResourceState::CopyDst);
            BufferTextureCopy region{}; region.bufferOffset = 4; region.bytesPerRow = 12; region.width = 2; region.height = 2;
            commands->CopyBufferToTexture(*upload, *image, region);
            commands->Transition(*image, ResourceState::CopyDst, ResourceState::CopySrc);
            commands->CopyTextureToBuffer(*image, *readback, region); commands->End();
            std::array<std::byte, 32> imageBytes{}; device.ReadBuffer(*readback, 0, imageBytes);
            Expect(std::memcmp(imageBytes.data()+4, pixels.data()+4, 8) == 0 && std::memcmp(imageBytes.data()+16, pixels.data()+16, 8) == 0,
                "Pitched GPU texture transfer contents differ.");

            auto vertex = device.CreateShader(Shader(device.GetBackend(), ShaderStage::Vertex));
            auto fragment = device.CreateShader(Shader(device.GetBackend(), ShaderStage::Fragment));
            const std::array<float, 6> positions{-1, -1, 3, -1, -1, 3};
            const std::array<std::uint32_t, 4> indices{99, 0, 1, 2};
            BufferDesc verticesDesc{sizeof(positions), BufferUsage::Vertex | BufferUsage::TransferDst, MemoryUsage::CpuToGpu};
            BufferDesc indicesDesc{sizeof(indices), BufferUsage::Index | BufferUsage::TransferDst, MemoryUsage::CpuToGpu};
            BufferDesc uniformDesc{16, BufferUsage::Uniform | BufferUsage::TransferDst, MemoryUsage::CpuToGpu};
            auto vertices = device.CreateBuffer(verticesDesc), index = device.CreateBuffer(indicesDesc);
            auto frame = device.CreateBuffer(uniformDesc), draw = device.CreateBuffer(uniformDesc);
            device.WriteBuffer(*vertices, 0, Bytes(positions)); device.WriteBuffer(*index, 0, Bytes(indices));
            const std::array<float, 4> tint{1, 1, 1, 1}, offset{0, 0, 0, 0};
            device.WriteBuffer(*frame, 0, Bytes(tint)); device.WriteBuffer(*draw, 0, Bytes(offset));
            TextureDesc sampleDesc = imageDesc; sampleDesc.height = 1;
            auto sampled = device.CreateTexture(sampleDesc);
            const std::array<unsigned char, 8> redBlue{255, 0, 0, 255, 0, 0, 255, 255};
            device.WriteTexture(*sampled, {2, 1, Rhi::TextureFormat::RGBA8Unorm, redBlue.data()});
            auto sampleView = device.CreateTextureView(*sampled, {});
            SamplerDesc nearestDesc{}; nearestDesc.minFilter = Filter::Nearest; nearestDesc.magFilter = Filter::Nearest;
            auto nearest = device.CreateSampler(nearestDesc), linear = device.CreateSampler({});
            BindingLayoutDesc frameLayout{{{7, BindingType::UniformBuffer, ShaderStage::Fragment, 1}}};
            // Declaration order is independent of the backend's physical
            // binding order; GL emission flattens by logical binding number.
            BindingLayoutDesc materialLayout{{{10, BindingType::Sampler, ShaderStage::Fragment, 1},
                {4, BindingType::SampledTexture, ShaderStage::Fragment, 1},
                {9, BindingType::Sampler, ShaderStage::Fragment, 1}, {3, BindingType::SampledTexture, ShaderStage::Fragment, 1}}};
            BindingLayoutDesc drawLayout{{{4, BindingType::UniformBuffer, ShaderStage::Vertex, 1}}};
            auto frameGroup = device.CreateBindingLayout(frameLayout), materialGroup = device.CreateBindingLayout(materialLayout), drawGroup = device.CreateBindingLayout(drawLayout);
            auto frameSet = device.CreateBindingSet({frameGroup.get(), {{7, BufferBinding{frame.get(), 0, 16}}}});
            auto drawSet = device.CreateBindingSet({drawGroup.get(), {{4, BufferBinding{draw.get(), 0, 16}}}});
            auto nearestSet = device.CreateBindingSet({materialGroup.get(), {{3, TextureBinding{sampleView.get()}},
                {4, TextureBinding{sampleView.get()}}, {9, SamplerBinding{nearest.get()}}, {10, SamplerBinding{linear.get()}}}});
            auto linearSet = device.CreateBindingSet({materialGroup.get(), {{3, TextureBinding{sampleView.get()}},
                {4, TextureBinding{sampleView.get()}}, {9, SamplerBinding{linear.get()}}, {10, SamplerBinding{nearest.get()}}}});
            GraphicsPipelineDesc pipelineDesc{};
            pipelineDesc.vertexShader = vertex.get(); pipelineDesc.fragmentShader = fragment.get();
            pipelineDesc.pipelineLayout.groups = {frameLayout, materialLayout, drawLayout, {}};
            pipelineDesc.vertexBuffers = {{0, 8, VertexInputRate::Vertex}};
            pipelineDesc.vertexAttributes = {{0, 0, VertexFormat::Float2, 0}};
            pipelineDesc.colorFormats = {Rhi::TextureFormat::RGBA8Unorm}; pipelineDesc.blendAttachments = {{}};
            pipelineDesc.rasterizer.cullMode = CullMode::None;
            auto pipeline = device.CreateGraphicsPipeline(pipelineDesc);
            auto otherPipelineDesc = pipelineDesc;
            otherPipelineDesc.blendAttachments[0].writeMask = ColorWriteMask::None;
            auto otherPipeline = device.CreateGraphicsPipeline(otherPipelineDesc);
            // Pipeline creation borrows shader inputs. A completed executable
            // must survive the public shader wrappers on either backend.
            vertex.reset(); fragment.reset();
            TextureDesc targetDesc{}; targetDesc.width = 16; targetDesc.height = 16; targetDesc.format = Rhi::TextureFormat::RGBA8Unorm;
            targetDesc.usage = TextureUsage::ColorAttachment | TextureUsage::TransferSrc;
            auto target = device.CreateTexture(targetDesc); auto targetView = device.CreateTextureView(*target, {});
            RenderingColorAttachment color{targetView.get(), LoadOp::Clear, StoreOp::Store, {0, 0, 0, 1}};
            RenderingInfo info{}; info.width = 16; info.height = 16; info.colorAttachments = std::span(&color, 1);
            auto otherTarget = device.CreateTexture(targetDesc); auto otherView = device.CreateTextureView(*otherTarget, {});
            RenderingColorAttachment otherColor{otherView.get(), LoadOp::Clear, StoreOp::Store, {0, 1, 0, 1}};
            RenderingInfo otherInfo = info; otherInfo.colorAttachments = std::span(&otherColor, 1);
            const std::array<unsigned char, 8> green{0, 255, 0, 255, 0, 255, 0, 255};
            auto otherSample = device.CreateTexture(sampleDesc);
            device.WriteTexture(*otherSample, {2, 1, Rhi::TextureFormat::RGBA8Unorm, green.data()});
            auto otherSampleView = device.CreateTextureView(*otherSample, {});
            auto otherMaterial = device.CreateBindingSet({materialGroup.get(), {{3, TextureBinding{otherSampleView.get()}},
                {4, TextureBinding{otherSampleView.get()}}, {9, SamplerBinding{nearest.get()}}, {10, SamplerBinding{linear.get()}}}});
            auto idleQuery = device.GetCapabilities().supportsTimestampQueries
                ? device.CreateTimestampQuerySet(2, "Idle recording rejection") : nullptr;
            commands = device.CreateCommandList();
            unsigned idleRejections = 0;
            const auto rejectRecording = [&](const char* name, auto action) {
                const auto before = device.Statistics().Submitted;
                bool rejected = false;
                try { action(); }
                catch (const std::invalid_argument&) { throw std::runtime_error(std::string(name) + ": arguments checked before recording guard."); }
                catch (const std::out_of_range&) { throw std::runtime_error(std::string(name) + ": range checked before recording guard."); }
                catch (const std::logic_error&) { rejected = true; }
                if (!rejected) throw std::runtime_error(std::string(name) + ": recording guard did not reject.");
                Expect(device.Statistics().Submitted == before, "Rejected recording mutation submitted work.");
                ++idleRejections;
            };
            const auto checkIdle = [&] {
                rejectRecording("BeginRendering", [&] { commands->BeginRendering(info); });
                rejectRecording("EndRendering", [&] { commands->EndRendering(); });
                rejectRecording("SetPipeline", [&] { commands->SetPipeline(*pipeline); });
                rejectRecording("SetViewport", [&] { commands->SetViewport({0, 0, 16, 16}); });
                rejectRecording("SetScissor", [&] { commands->SetScissor({0, 0, 16, 16}); });
                rejectRecording("SetVertexBuffer", [&] { commands->SetVertexBuffer(0, *vertices); });
                rejectRecording("SetIndexBuffer", [&] { commands->SetIndexBuffer(*index, IndexType::UInt32); });
                rejectRecording("SetBindingSet", [&] { commands->SetBindingSet(0, *frameSet); });
                rejectRecording("SetStencilReference", [&] { commands->SetStencilReference(0); });
                rejectRecording("Draw", [&] { commands->Draw(3); });
                rejectRecording("DrawIndexed", [&] { commands->DrawIndexed(3); });
                rejectRecording("CopyBuffer", [&] { commands->CopyBuffer(*upload, 0, *readback, 0, 16); });
                rejectRecording("CopyBufferToTexture", [&] { commands->CopyBufferToTexture(*upload, *image, region); });
                rejectRecording("CopyTextureToBuffer", [&] { commands->CopyTextureToBuffer(*image, *readback, region); });
                rejectRecording("TransitionBuffer", [&] { commands->Transition(*upload, ResourceState::CopySrc, ResourceState::CopySrc); });
                rejectRecording("TransitionTexture", [&] { commands->Transition(*image, ResourceState::CopySrc, ResourceState::ShaderRead); });
                rejectRecording("BindSampledTexture", [&] { commands->BindSampledTexture(0, sampled.get(), nearest.get()); });
                rejectRecording("CopyColorAttachmentToTexture", [&] { commands->CopyColorAttachmentToTexture(*image, 2, 2); });
                rejectRecording("BeginDebugLabel", [&] { commands->BeginDebugLabel({"Idle"}); });
                rejectRecording("EndDebugLabel", [&] { commands->EndDebugLabel(); });
                rejectRecording("InsertDebugMarker", [&] { commands->InsertDebugMarker({"Idle"}); });
                if (idleQuery)
                {
                    rejectRecording("InitializeTimestamps", [&] { commands->InitializeTimestamps(*idleQuery); });
                    rejectRecording("WriteTimestamp", [&] { commands->WriteTimestamp(*idleQuery, 0); });
                }
            };
            checkIdle();
            (void)device.BeginFrame();
            auto render = [&](const BindingSet& material, bool indexed) {
                auto clearImage = device.CreateTexture(imageDesc);
                auto clearOutput = device.CreateBuffer({16, BufferUsage::TransferDst, MemoryUsage::GpuToCpu});
                commands->Begin();
                constexpr auto sampledReads = ResourceState::ShaderRead | ResourceState::CopySrc;
                commands->Transition(*sampled, ResourceState::ShaderRead, sampledReads);
                rejectRecording("Draw outside rendering", [&] { commands->Draw(3); });
                rejectRecording("DrawIndexed outside rendering", [&] { commands->DrawIndexed(3); });
                commands->Transition(*clearOutput, ResourceState::Undefined, ResourceState::CopyDst);
                commands->BeginRendering(info); commands->SetPipeline(*pipeline);
                if (idleQuery)
                    rejectRecording("InitializeTimestamps inside rendering", [&] { commands->InitializeTimestamps(*idleQuery); });
                commands->SetViewport({0, 0, 16, 16}); commands->SetVertexBuffer(0, *vertices);
                commands->SetBindingSet(0, *frameSet); commands->SetBindingSet(2, *drawSet);
                auto temporaryLayout = device.CreateBindingLayout(materialLayout);
                auto materialDesc = material.Desc(); materialDesc.layout = temporaryLayout.get();
                auto temporarySet = device.CreateBindingSet(materialDesc);
                temporaryLayout.reset();
                commands->SetBindingSet(1, *temporarySet);
                // Applied bindings are snapshots. Source set/layout wrappers
                // can go away while their actual resources stay alive.
                temporarySet.reset();
                // A pending clear must precede transfer, and the first half of
                // the picture must survive transfers and barriers before the
                // second half is drawn. Resume must LOAD rather than CLEAR.
                commands->CopyBuffer(*upload, 0, *readback, 0, 16);
                commands->CopyColorAttachmentToTexture(*clearImage, 2, 2);
                commands->Transition(*clearImage, ResourceState::ShaderRead, ResourceState::CopySrc);
                BufferTextureCopy clearRegion{}; clearRegion.width = clearRegion.height = 2;
                commands->CopyTextureToBuffer(*clearImage, *clearOutput, clearRegion);
                commands->SetScissor({0, 0, 8, 16});
                if (indexed) { commands->SetIndexBuffer(*index, IndexType::UInt32); commands->DrawIndexed(3, 1, 1); }
                else commands->Draw(3);
                // Changing only the image layout must refresh applied
                // descriptors, without requiring the caller to rebind the set.
                commands->Transition(*sampled, sampledReads, ResourceState::ShaderRead);
                if (indexed) commands->DrawIndexed(3, 1, 1); else commands->Draw(3);
                commands->Transition(*sampled, ResourceState::ShaderRead, sampledReads);
                if (indexed) commands->DrawIndexed(3, 1, 1); else commands->Draw(3);
                commands->Transition(*sampled, sampledReads, ResourceState::CopyDst);
                const auto beforeRejectedDraw = device.Statistics();
                bool unreadableRejected = false;
                try { if (indexed) commands->DrawIndexed(3, 1, 1); else commands->Draw(3); }
                catch (const std::invalid_argument&) { unreadableRejected = true; }
                const auto afterRejectedDraw = device.Statistics();
                Expect(unreadableRejected && beforeRejectedDraw.Submitted == afterRejectedDraw.Submitted
                    && beforeRejectedDraw.HostWaits == afterRejectedDraw.HostWaits
                    && beforeRejectedDraw.DeviceWideWaits == afterRejectedDraw.DeviceWideWaits
                    && beforeRejectedDraw.LiveObjects() == afterRejectedDraw.LiveObjects(),
                    "Unreadable sampled draw reached native submission, waits or allocation.");
                commands->Transition(*sampled, ResourceState::CopyDst, sampledReads);
                if (indexed) commands->DrawIndexed(3, 1, 1); else commands->Draw(3);
                std::array<unsigned char, 4> midway{};
                commands->ReadColor(info, 4, 8, 1, 1, Rhi::TextureFormat::RGBA8Unorm, midway.data());
                auto interleaved = device.CreateCommandList(); interleaved->Begin();
                interleaved->CopyBuffer(*upload, 0, *readback, 0, 16);
                interleaved->BeginRendering(otherInfo); interleaved->SetPipeline(*otherPipeline);
                interleaved->SetViewport({0, 0, 4, 4}); interleaved->SetScissor({0, 0, 1, 1});
                interleaved->SetVertexBuffer(0, *vertices); interleaved->SetBindingSet(0, *frameSet);
                interleaved->SetBindingSet(1, *otherMaterial); interleaved->SetBindingSet(2, *drawSet);
                interleaved->Draw(3); interleaved->EndRendering(); interleaved->End();
                device.EndFrame(); (void)device.BeginFrame();
                // The caller does not rebind pipeline, vertices, index or sets
                // after diagnostic/frame/other-list submissions.
                commands->Transition(*upload, ResourceState::CopySrc, ResourceState::Common);
                commands->Transition(*upload, ResourceState::Common, ResourceState::CopySrc);
                commands->CopyBuffer(*upload, 0, *readback, 0, 16);
                commands->Transition(*image, ResourceState::CopySrc, ResourceState::CopyDst);
                commands->CopyBufferToTexture(*upload, *image, region);
                commands->Transition(*image, ResourceState::CopyDst, ResourceState::CopySrc);
                commands->CopyTextureToBuffer(*image, *readback, region);
                commands->SetScissor({8, 0, 8, 16});
                if (indexed) commands->DrawIndexed(3, 1, 1);
                else commands->Draw(3);
                commands->EndRendering();
                // Sampling and submission restart must preserve combined read
                // permissions, including their explicit tracked-state witness.
                commands->Transition(*sampled, sampledReads, ResourceState::ShaderRead);
                commands->End();
                std::array<std::byte, 16> clearBytes{};
                device.ReadBuffer(*clearOutput, 0, clearBytes);
                for (std::size_t i = 0; i < clearBytes.size(); ++i)
                    Expect(clearBytes[i] == (i % 4 == 3 ? std::byte{255} : std::byte{0}),
                        "Pending clear did not precede current-color GPU copy.");
                std::array<unsigned char, 8> pixel{};
                commands->ReadColor(info, 4, 8, 1, 1, Rhi::TextureFormat::RGBA8Unorm, pixel.data());
                commands->ReadColor(info, 12, 8, 1, 1, Rhi::TextureFormat::RGBA8Unorm, pixel.data() + 4);
                Expect(std::equal(midway.begin(), midway.end(), pixel.begin()),
                    "Native buffer restart changed the first half of the draw.");
                return pixel;
            };
            const auto nearestPixel = render(*nearestSet, false), linearPixel = render(*linearSet, true);
            checkIdle(); idleQuery.reset();
            Expect(nearestPixel[0] < 3 && nearestPixel[2] > 252 && nearestPixel[3] == 255, "Nonindexed draw or nearest sampler contents differ.");
            Expect(linearPixel[0] >= 126 && linearPixel[0] <= 129 && linearPixel[2] >= 126 && linearPixel[2] <= 129 && linearPixel[3] == 255,
                "Indexed draw or independent linear sampler contents differ.");
            Expect(nearestPixel[4] >= 126 && nearestPixel[4] <= 129 && nearestPixel[6] >= 126 && nearestPixel[6] <= 129 && nearestPixel[7] == 255
                && linearPixel[4] < 3 && linearPixel[6] > 252 && linearPixel[7] == 255,
                "The same image did not support two simultaneous sampler states.");
            std::cout << "[recording mutation] PASS; rejected=" << idleRejections
                << "; pending-clear/transfer; source set/layout release; readback/frame/other-list draw restart without rebind; sampler pixels\n";
            unsigned expiredBindingRejections = 0;
            for (unsigned kind = 0; kind < 3; ++kind)
            {
                auto temporaryBuffer = kind == 0 ? device.CreateBuffer(uniformDesc) : nullptr;
                auto temporaryView = kind == 1 ? device.CreateTextureView(*sampled, {}) : nullptr;
                auto temporarySampler = kind == 2 ? device.CreateSampler(nearestDesc) : nullptr;
                if (temporaryBuffer) device.WriteBuffer(*temporaryBuffer, 0, Bytes(tint));
                auto desc = kind == 0 ? frameSet->Desc() : nearestSet->Desc();
                if (kind == 0) desc.entries[0].resource = BufferBinding{temporaryBuffer.get(), 0, 16};
                if (kind == 1) desc.entries[0].resource = TextureBinding{temporaryView.get()};
                if (kind == 2) desc.entries[2].resource = SamplerBinding{temporarySampler.get()};
                auto temporarySet = device.CreateBindingSet(desc);
                commands->Begin(); commands->BeginRendering(info); commands->SetPipeline(*pipeline);
                commands->SetViewport({0, 0, 16, 16}); commands->SetVertexBuffer(0, *vertices);
                commands->SetBindingSet(0, *frameSet); commands->SetBindingSet(1, *nearestSet); commands->SetBindingSet(2, *drawSet);
                const unsigned group = kind == 0 ? 0 : 1;
                commands->SetBindingSet(group, *temporarySet);
                temporaryBuffer.reset(); temporaryView.reset(); temporarySampler.reset();
                const auto submitted = device.Statistics().Submitted;
                const auto rejectExpired = [&](auto action) {
                    bool rejected = false;
                    try { action(); } catch (const std::invalid_argument&) { rejected = true; }
                    Expect(rejected && device.Statistics().Submitted == submitted,
                        "Expired binding resource was used or rejecting it submitted GPU work.");
                    ++expiredBindingRejections;
                };
                rejectExpired([&] { commands->SetBindingSet(group, *temporarySet); });
                rejectExpired([&] { commands->Draw(3); });
                commands->EndRendering(); commands->End();
            }
            std::cout << "[binding lifetime] PASS; rejected=" << expiredBindingRejections
                << "; buffer/view/sampler wrapper expiry before set or resumed draw\n";
            {
                auto discardedImage = device.CreateTexture(sampleDesc);
                auto survivingView = device.CreateTextureView(*discardedImage, {});
                auto survivingSet = device.CreateBindingSet({materialGroup.get(), {{3, TextureBinding{survivingView.get()}},
                    {4, TextureBinding{survivingView.get()}}, {9, SamplerBinding{nearest.get()}}, {10, SamplerBinding{linear.get()}}}});
                discardedImage.reset();
                commands->Begin(); commands->SetPipeline(*pipeline);
                const auto submitted = device.Statistics().Submitted;
                bool staleRejected = false;
                try { commands->SetBindingSet(1, *survivingSet); }
                catch (const std::invalid_argument&) { staleRejected = true; }
                Expect(staleRejected && device.Statistics().Submitted == submitted,
                    "An existing binding set reused a view after texture destruction or submitted work while rejecting it.");
                commands->End();
            }
            rejected = false;
            commands->Begin(); commands->SetPipeline(*pipeline);
            try { commands->SetBindingSet(4, *frameSet); } catch (const std::invalid_argument&) { rejected = true; }
            Expect(rejected, "Out-of-range binding group was accepted.");
            commands->End();
            device.EndFrame(); device.WaitIdle();
            Expect(device.DrainErrors() == 0, "RHI conformance raised native graphics errors.");
            if (held)
            {
                // Deliberately leave a copy recorded at the shutdown boundary.
                // The session must submit and complete it before native release.
                commands->Begin();
                commands->CopyBuffer(*upload, 0, *readback, 0, upload->Desc().size);
                held->Commands = commands.get(); held->Pipeline = pipeline.get();
                held->Layout = frameGroup.get(); held->Set = frameSet.get(); held->Texture = target.get();
                held->View = sampleView.get();
                held->Keep(std::move(commands)); held->Keep(std::move(pipeline));
                for (auto* value : {&upload, &readback, &vertices, &index, &frame, &draw}) held->Keep(std::move(*value));
                for (auto* value : {&image, &sampled, &target}) held->Keep(std::move(*value));
                for (auto* value : {&sampleView, &targetView}) held->Keep(std::move(*value));
                for (auto* value : {&nearest, &linear}) held->Keep(std::move(*value));
                for (auto* value : {&frameGroup, &materialGroup, &drawGroup}) held->Keep(std::move(*value));
                for (auto* value : {&frameSet, &drawSet, &nearestSet, &linearSet}) held->Keep(std::move(*value));
                held->Keep(device.CreateShader(Shader(device.GetBackend(), ShaderStage::Vertex)));
                held->Keep(device.CreateShader(Shader(device.GetBackend(), ShaderStage::Fragment)));
                targetDesc.format = Rhi::TextureFormat::D24UnormS8Uint;
                targetDesc.usage = TextureUsage::DepthStencilAttachment;
                held->Keep(device.CreateTexture(targetDesc));
            }
        }

        void ExerciseSessionLifetime(const Rhi::BackendProvider& provider)
        {
            using namespace Rhi;
            const bool gl = provider.Backend() == GraphicsBackend::OpenGl;
            for (unsigned cycle = 0; cycle < 8; ++cycle)
            {
                auto outgoing = provider.CreateSession({true});
                HeldResources held;
                auto& oldDevice = outgoing->Device();
                if (!gl) Expect(outgoing->ValidationEnabled(), "Session lifetime check requires Vulkan validation.");
                Exercise(oldDevice, &held);
                auto pendingTimestamps = oldDevice.CreateTimestampQuerySet(2, "Session shutdown query");
                if (pendingTimestamps)
                {
                    held.Commands->InitializeTimestamps(*pendingTimestamps);
                    held.Commands->WriteTimestamp(*pendingTimestamps, 0);
                }
                bool rejected = false;
                if (gl)
                {
                    auto duplicate = provider.CreateSession({});
                    try { (void)duplicate->Device(); } catch (const std::logic_error&) { rejected = true; }
                    Expect(rejected, "Two session devices claimed the same OpenGL context.");
                }
                auto released = gl ? OpenGL::NativeReleaseCheck(oldDevice) : Vulkan::SessionReleaseCheck(oldDevice);
                const auto oldHandle = held.Texture->Handle();
                const auto textureDesc = held.Texture->Desc();
                // Exercise explicit owner teardown and the window's defensive
                // close path while its session wrapper is still alive.
                if (gl && cycle % 2) OpenGL::ReleaseContextDevice();
                else { outgoing->Shutdown(); outgoing->Shutdown(); }
                released();
                if (pendingTimestamps)
                {
                    std::array<std::uint64_t, 2> result{19, 23};
                    Expect(pendingTimestamps->ReadResults(result) == TimestampStatus::Cancelled && result[0] == 19,
                        "GPU timestamp wrapper survived native shutdown or touched output.");
                }
                Expect(!outgoing->ValidationErrors(), "Session shutdown reported native resource lifetime errors.");
                Expect(!held.Texture->Handle(), "Detached texture still exposed its old native name.");
                rejected = false;
                try { held.Commands->Begin(); } catch (const std::logic_error&) { rejected = true; }
                Expect(rejected, "Command recording survived its ended session.");

                auto incoming = provider.CreateSession({true});
                auto& device = incoming->Device();
                if (!gl) Expect(incoming->ValidationEnabled(), "Incoming session check requires Vulkan validation.");
                outgoing->Shutdown(); outgoing->Shutdown();
                rejected = false;
                try { held.Commands->SetViewport({0, 0, 16, 16}); } catch (const std::logic_error&) { rejected = true; }
                Expect(rejected, "Ended command list changed the incoming session's viewport.");
                rejected = false;
                try { held.Commands->BindSampledTexture(0, nullptr, nullptr); } catch (const std::logic_error&) { rejected = true; }
                Expect(rejected, "Ended command list changed the incoming session's texture bindings.");
                auto commands = device.CreateCommandList(); commands->Begin();
                rejected = false;
                try { commands->SetPipeline(*held.Pipeline); } catch (const std::invalid_argument&) { rejected = true; }
                Expect(rejected, "Old session pipeline was accepted by a new device.");
                auto state = held.Pipeline->Desc();
                auto vertex = device.CreateShader(Shader(device.GetBackend(), ShaderStage::Vertex));
                auto fragment = device.CreateShader(Shader(device.GetBackend(), ShaderStage::Fragment));
                state.vertexShader = vertex.get(); state.fragmentShader = fragment.get();
                auto pipeline = device.CreateGraphicsPipeline(state); commands->SetPipeline(*pipeline);
                rejected = false;
                try { commands->SetBindingSet(0, *held.Set); } catch (const std::invalid_argument&) { rejected = true; }
                Expect(rejected, "Old session binding set was accepted by a new device.");
                auto guard = device.CreateBuffer({16, BufferUsage::Uniform | BufferUsage::TransferSrc, MemoryUsage::CpuToGpu});
                auto guardRead = device.CreateBuffer({16, BufferUsage::TransferDst, MemoryUsage::GpuToCpu});
                rejected = false;
                try { (void)device.CreateBindingSet({held.Layout, {{7, BufferBinding{guard.get(), 0, 16}}}}); }
                catch (const std::invalid_argument&) { rejected = true; }
                Expect(rejected, "Old session binding layout was accepted by a new device.");
                rejected = false;
                try { (void)device.CreateTextureView(*held.Texture, {}); } catch (const std::invalid_argument&) { rejected = true; }
                Expect(rejected, "Old session texture view was accepted by a new device.");
                rejected = false;
                try { device.ResizeTexture(*held.Texture, 16, 16); } catch (const std::invalid_argument&) { rejected = true; }
                Expect(rejected, "Old session texture storage was resized by a new device.");
                auto imageLayout = device.CreateBindingLayout({{{3, BindingType::SampledTexture, ShaderStage::Fragment, 1}}});
                rejected = false;
                try { (void)device.CreateBindingSet({imageLayout.get(), {{3, TextureBinding{held.View}}}}); }
                catch (const std::invalid_argument&) { rejected = true; }
                Expect(rejected, "Old session image view was accepted by a new device.");
                imageLayout.reset();
                commands->End();
                auto guardTexture = device.CreateTexture(textureDesc, oldHandle);
                const std::array<unsigned, 4> payload{cycle, 7, 23, 0xAABBCCDD};
                device.WriteBuffer(*guard, 0, Bytes(payload));
                const auto before = device.Statistics();
                held.Leases.clear();
                const auto after = device.Statistics();
                Expect(after.LiveObjects() == before.LiveObjects() && after.Retired == before.Retired,
                    "Late old-session destruction altered incoming resources.");
                std::array<unsigned, 4> result{};
                commands->Begin();
                commands->Transition(*guard, ResourceState::Undefined, ResourceState::CopySrc);
                commands->Transition(*guardRead, ResourceState::Undefined, ResourceState::CopyDst);
                commands->CopyBuffer(*guard, 0, *guardRead, 0, 16); commands->End();
                device.ReadBuffer(*guardRead, 0, std::as_writable_bytes(std::span(result)));
                Expect(result == payload && device.FindTexture(oldHandle) == guardTexture.get() && !device.DrainErrors(),
                    "Late old-session destruction deleted a reused incoming native name.");
                guard.reset(); guardRead.reset(); guardTexture.reset(); commands.reset(); pipeline.reset();
                vertex.reset(); fragment.reset(); device.TrimCaches();
                Exercise(device);
                device.TrimCaches();
                device.WaitIdle();
                Expect(!device.Statistics().LiveObjects() && !device.Statistics().Retired,
                    "Incoming session did not release its resources.");
                incoming->Shutdown();
                Expect(!incoming->ValidationErrors(), "Incoming session shutdown reported validation errors.");
            }
            std::cout << "[rhi conformance] " << (gl ? "OpenGL" : "Vulkan")
                << " session ownership PASS; 8 shutdown/recreate cycles; pending copy; native release; late wrappers inert; stale bindings rejected\n";
        }
    }
    int RunRhiConformanceCheck()
    {
        try
        {
            RendererPlatform::WindowSettings settings{};
            settings.ClientSize = {64, 64}; settings.StartVisible = false;
            settings.Title = std::string(Branding::Name) + " RHI conformance";
            settings.Profile = RendererPlatform::WindowSettings::ContextProfile::Compatability;
            settings.ApiMajor = 4; settings.ApiMinor = 5;
            auto window = RendererPlatform::CreateWindow(settings);
            for (const auto backend : {Rhi::GraphicsBackend::OpenGl, Rhi::GraphicsBackend::Vulkan})
            {
                const auto* provider = Rhi::FindBackendProvider(backend);
                if (!provider) continue;
                auto session = provider->CreateSession({true});
                auto& device = session->Device();
                if (backend == Rhi::GraphicsBackend::Vulkan) Expect(session->ValidationEnabled(), "Conformance requires Vulkan validation.");
                if (backend == Rhi::GraphicsBackend::OpenGl) Rhi::OpenGL::CheckMemoryAdmission(device);
                else Rhi::Vulkan::CheckMemoryAdmission(device);
                ExerciseUnframedLifetime(device);
                ExerciseRecordingInterval(device);
                CheckRhiResourceStates(device);
                CheckRhiRgbCopies(device);
                CheckRhiFormatCopies(device);
                CheckRhiPackedDepthStencilCopies(device);
                CheckRhiSubresourceCapabilities(device);
                CheckRhiSubresourceReadbacks(device);
                CheckResourceOwnership(device);
                ExerciseGpuDiagnostics(device);
                CheckAsyncReadback(device);
                Exercise(device);
                device.TrimCaches();
                device.WaitIdle();
                const auto live = device.Statistics();
                Expect(live.LiveObjects() == 0 && live.Retired == 0,
                    "RHI conformance did not release its resources.");
                session->Shutdown();
                Expect(session->ValidationErrors() == 0, "RHI conformance shutdown validation failed.");
                std::cout << "[rhi conformance] " << (backend == Rhi::GraphicsBackend::OpenGl ? "OpenGL" : "Vulkan")
                    << " PASS; unframed submission lifetime; GPU buffers/copies/pitched texture transfers; four-group layout; UBO/image/sampler; Draw/DrawIndexed; sampler pixels; release=0\n";
                ExerciseSessionLifetime(*provider);
                CheckReadbackSessionLifetime(*provider);
            }
            return 0;
        }
        catch (const std::exception& error) { std::cerr << "[rhi conformance] FAIL: " << error.what() << '\n'; return 1; }
    }
}
