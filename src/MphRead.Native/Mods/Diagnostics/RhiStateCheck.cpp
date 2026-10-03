#include "RhiStateCheck.hpp"
#include <algorithm>
#include <thread>
#include <chrono>
#include "../../NativeRuntime/Rhi/GraphicsDevice.hpp"
#include "../../NativeRuntime/Rhi/CommandList.hpp"
#include <array>
#include <vector>
#include <iostream>
#include <stdexcept>
#include <cstring>

namespace MphRead::Mods::Diagnostics
{
    namespace
    {
        // A buffer's contents through the asynchronous readback: the path any
        // texture format takes once a GPU copy has put it in a buffer.
        std::vector<std::byte> AsyncRead(NativeRuntime::Rhi::GraphicsDevice& device,
            NativeRuntime::Rhi::Buffer& buffer, std::uint64_t bytes)
        {
            auto ticket = device.EnqueueReadback(buffer, 0, bytes);
            if (!ticket) throw std::runtime_error("Async readback of a format copy was not admitted.");
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!ticket.IsReady())
            {
                if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("Async format readback did not complete.");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            const auto result = ticket.MapResult();
            return {result.Bytes().begin(), result.Bytes().end()};
        }
    }

    void CheckRhiFormatCopies(NativeRuntime::Rhi::GraphicsDevice& device)
    {
        using namespace NativeRuntime::Rhi;
        struct Case { TextureFormat Format; const char* Name; unsigned Components, ComponentBytes; bool Float, Depth; };
        constexpr Case cases[]{
            {TextureFormat::R8Unorm, "R8", 1, 1}, {TextureFormat::RG8Unorm, "RG8", 2, 1},
            {TextureFormat::RGB8Unorm, "RGB8", 3, 1}, {TextureFormat::RGBA8Unorm, "RGBA8", 4, 1},
            {TextureFormat::RGBA8Srgb, "RGBA8-sRGB", 4, 1}, {TextureFormat::BGRA8Unorm, "BGRA8", 4, 1},
            {TextureFormat::BGRA8Srgb, "BGRA8-sRGB", 4, 1},
            {TextureFormat::R16Float, "R16F", 1, 2, true}, {TextureFormat::RG16Float, "RG16F", 2, 2, true},
            {TextureFormat::RGBA16Float, "RGBA16F", 4, 2, true},
            {TextureFormat::R32Float, "R32F", 1, 4, true}, {TextureFormat::RG32Float, "RG32F", 2, 4, true},
            {TextureFormat::RGB32Float, "RGB32F", 3, 4, true}, {TextureFormat::RGBA32Float, "RGBA32F", 4, 4, true},
            {TextureFormat::D16Unorm, "D16", 1, 2, false, true}, {TextureFormat::D32Float, "D32F", 1, 4, true, true}};
        for (const auto& test : cases)
        {
            std::cout << "[format copy] " << test.Name << " begin\n" << std::flush;
            const unsigned bytes = test.Components * test.ComponentBytes;
            const unsigned alignment = test.Depth ? 4 : bytes;
            TextureDesc desc{}; desc.width = 4; desc.height = 3; desc.format = test.Format;
            // In particular, a transfer-only depth image must not silently
            // become a renderbuffer which cannot implement these operations.
            desc.usage = TextureUsage::TransferSrc | TextureUsage::TransferDst;
            auto texture = device.CreateTexture(desc);
            std::array<std::byte, 512> input{}, zero{}, sentinel{}, expected{}, actual{};
            input.fill(std::byte{0xA7}); sentinel.fill(std::byte{0xD3}); expected = sentinel;
            const unsigned inputOffset = alignment * 4, outputOffset = alignment * 3;
            for (unsigned y = 0; y < 3; ++y)
                for (unsigned x = 0; x < 4; ++x)
                    for (unsigned c = 0; c < bytes; ++c) expected[outputOffset + y * 6 * bytes + x * bytes + c] = std::byte{0};
            for (unsigned y = 0; y < 2; ++y)
                for (unsigned x = 0; x < 3; ++x)
                    for (unsigned c = 0; c < test.Components; ++c)
                    {
                        const unsigned position = inputOffset + y * 5 * bytes + x * bytes + c * test.ComponentBytes;
                        const unsigned index = (y * 3 + x + c) % 4;
                        if (test.Float && test.ComponentBytes == 2)
                        {
                            constexpr std::uint16_t halves[]{0x3400, 0x3800, 0x3A00, 0x3C00};
                            std::memcpy(input.data() + position, &halves[index], 2);
                        }
                        else if (test.Float)
                        {
                            constexpr float floats[]{0.25F, 0.5F, 0.75F, 1.0F};
                            std::memcpy(input.data() + position, &floats[index], 4);
                        }
                        else if (test.Depth)
                        {
                            constexpr std::uint16_t depths[]{0, 0x4000, 0x8000, 0xFFFF};
                            std::memcpy(input.data() + position, &depths[index], 2);
                        }
                        else input[position] = static_cast<std::byte>((17 * (y * 3 + x) + 61 * c + 7) & 255);
                        const unsigned output = outputOffset + (y + 1) * 6 * bytes + (x + 1) * bytes + c * test.ComponentBytes;
                        std::memcpy(expected.data() + output, input.data() + position, test.ComponentBytes);
                    }
            auto source = device.CreateBuffer({512, BufferUsage::TransferSrc, MemoryUsage::CpuToGpu});
            auto black = device.CreateBuffer({512, BufferUsage::TransferSrc, MemoryUsage::CpuToGpu});
            auto padding = device.CreateBuffer({512, BufferUsage::TransferSrc, MemoryUsage::CpuToGpu});
            auto output = device.CreateBuffer({512, BufferUsage::TransferDst | BufferUsage::TransferSrc, MemoryUsage::GpuToCpu});
            device.WriteBuffer(*source, 0, input); device.WriteBuffer(*black, 0, zero); device.WriteBuffer(*padding, 0, sentinel);
            auto commands = device.CreateCommandList(); commands->Begin();
            for (auto* buffer : {source.get(), black.get(), padding.get()})
                commands->Transition(*buffer, ResourceState::Undefined, ResourceState::CopySrc);
            commands->Transition(*output, ResourceState::Undefined, ResourceState::CopyDst);
            commands->Transition(*texture, ResourceState::Undefined, ResourceState::CopyDst);
            commands->CopyBuffer(*padding, 0, *output, 0, 512);
            BufferTextureCopy initial{}; initial.width = 4; initial.height = 3;
            initial.aspect = test.Depth ? TextureAspect::Depth : TextureAspect::Color;
            commands->CopyBufferToTexture(*black, *texture, initial);
            BufferTextureCopy upload = initial; upload.bufferOffset = inputOffset; upload.bytesPerRow = bytes * 5;
            upload.x = upload.y = 1; upload.width = 3; upload.height = 2;
            const auto before = device.Statistics();
            commands->CopyBufferToTexture(*source, *texture, upload);
            commands->Transition(*texture, ResourceState::CopyDst, ResourceState::CopySrc);
            auto download = initial; download.bufferOffset = outputOffset; download.bytesPerRow = bytes * 6;
            commands->CopyTextureToBuffer(*texture, *output, download);
            const auto after = device.Statistics();
            if (after.HostWaits != before.HostWaits || after.DeviceWideWaits != before.DeviceWideWaits)
                throw std::runtime_error("Format GPU copy inserted a host/device wait.");
            commands->End(); device.ReadBuffer(*output, 0, actual);
            if (actual != expected || device.DrainErrors())
                throw std::runtime_error(std::string("Format GPU copy changed pixels, padding or untouched image region: ") + test.Name);
            {
                auto list = device.CreateCommandList(); list->Begin();
                list->Transition(*output, ResourceState::CopyDst, ResourceState::CopySrc); list->End();
                const auto async = AsyncRead(device, *output, 512);
                if (async.size() != 512 || !std::equal(async.begin(), async.end(), expected.begin()))
                    throw std::runtime_error(std::string("Async readback of a format copy differs: ") + test.Name);
            }
            std::cout << "[format copy] " << test.Name << " PASS; exact pixels/padding/subrectangle; async readback equal; transfer-only; copy waits=0\n";
        }
        std::cout << "[format copy] PASS; 16 color/single-depth formats; packed depth/stencil is a separate contract\n";
    }

    // Mip 1 / layer 1 of a mipmapped array, written and read back on its own
    // through a GPU copy and the async readback, the base subresource intact.
    void CheckRhiSubresourceReadbacks(NativeRuntime::Rhi::GraphicsDevice& device)
    {
        using namespace NativeRuntime::Rhi;
        const auto& caps = device.GetCapabilities();
        if (caps.maxTextureMipLevels < 2 || caps.maxTextureArrayLayers < 2)
        {
            std::cout << "[subresource readback] skipped; single-level 2D images on this backend (Capabilities)\n";
            return;
        }
        TextureDesc desc{}; desc.width = 8; desc.height = 4; desc.mipLevels = 2; desc.arrayLayers = 2;
        desc.format = TextureFormat::RGBA8Unorm; desc.usage = TextureUsage::TransferSrc | TextureUsage::TransferDst;
        auto texture = device.CreateTexture(desc);
        std::array<std::byte, 64> base{}, mip{}, zero{};
        for (std::size_t i = 0; i < base.size(); ++i) { base[i] = std::byte(0x40 + i % 7); }
        for (std::size_t i = 0; i < 4 * 2 * 4; ++i) mip[i] = std::byte(i * 13 + 1);
        auto baseIn = device.CreateBuffer({128, BufferUsage::TransferSrc, MemoryUsage::CpuToGpu});
        auto mipIn = device.CreateBuffer({64, BufferUsage::TransferSrc, MemoryUsage::CpuToGpu});
        auto baseOut = device.CreateBuffer({128, BufferUsage::TransferDst | BufferUsage::TransferSrc, MemoryUsage::GpuToCpu});
        auto mipOut = device.CreateBuffer({64, BufferUsage::TransferDst | BufferUsage::TransferSrc, MemoryUsage::GpuToCpu});
        std::array<std::byte, 128> baseImage{};
        for (std::size_t i = 0; i < baseImage.size(); ++i) baseImage[i] = std::byte(0x40 + i % 7);
        device.WriteBuffer(*baseIn, 0, baseImage); device.WriteBuffer(*mipIn, 0, mip);
        auto commands = device.CreateCommandList(); commands->Begin();
        for (auto* buffer : {baseIn.get(), mipIn.get()}) commands->Transition(*buffer, ResourceState::Undefined, ResourceState::CopySrc);
        for (auto* buffer : {baseOut.get(), mipOut.get()}) commands->Transition(*buffer, ResourceState::Undefined, ResourceState::CopyDst);
        commands->Transition(*texture, ResourceState::Undefined, ResourceState::CopyDst);
        BufferTextureCopy baseRegion{}; baseRegion.width = 8; baseRegion.height = 4;
        commands->CopyBufferToTexture(*baseIn, *texture, baseRegion);
        BufferTextureCopy mipRegion{}; mipRegion.width = 4; mipRegion.height = 2; mipRegion.mipLevel = 1; mipRegion.arrayLayer = 1;
        commands->CopyBufferToTexture(*mipIn, *texture, mipRegion);
        commands->Transition(*texture, ResourceState::CopyDst, ResourceState::CopySrc);
        commands->CopyTextureToBuffer(*texture, *mipOut, mipRegion);
        commands->CopyTextureToBuffer(*texture, *baseOut, baseRegion);
        commands->Transition(*baseOut, ResourceState::CopyDst, ResourceState::CopySrc);
        commands->Transition(*mipOut, ResourceState::CopyDst, ResourceState::CopySrc);
        commands->End();
        const auto mipRead = AsyncRead(device, *mipOut, 32);
        const auto baseRead = AsyncRead(device, *baseOut, 128);
        if (!std::equal(mipRead.begin(), mipRead.end(), mip.begin()))
            throw std::runtime_error("Mip 1 / layer 1 readback differs from what was written there.");
        if (!std::equal(baseRead.begin(), baseRead.end(), baseImage.begin()))
            throw std::runtime_error("Writing mip 1 / layer 1 changed the base subresource.");
        if (device.DrainErrors()) throw std::runtime_error("Subresource readback raised native errors.");
        std::cout << "[subresource readback] PASS; mip 1 / layer 1 written and read back on its own; base subresource intact\n";
    }

    // What Capabilities reports for subresources is what CreateTexture keeps:
    // the limit itself is accepted, one past it is refused at creation.
    void CheckRhiSubresourceCapabilities(NativeRuntime::Rhi::GraphicsDevice& device)
    {
        using namespace NativeRuntime::Rhi;
        const Capabilities& caps = device.GetCapabilities();
        if (!caps.maxTextureMipLevels || !caps.maxTextureArrayLayers)
            throw std::runtime_error("Capabilities report no mip level or array layer at all.");
        const auto refused = [&device](const TextureDesc& desc)
        {
            try { (void)device.CreateTexture(desc); } catch (const std::invalid_argument&) { return true; }
            catch (const std::out_of_range&) { return true; }
            return false;
        };
        TextureDesc base{}; base.width = 8; base.height = 8; base.format = TextureFormat::RGBA8Unorm;
        base.usage = TextureUsage::Sampled | TextureUsage::TransferDst;
        unsigned checked = 0;
        // Mip levels: a chain the extent allows, up to the limit.
        auto mips = base; mips.mipLevels = std::min(caps.maxTextureMipLevels, 4U);
        (void)device.CreateTexture(mips); ++checked;
        if (caps.maxTextureMipLevels < 4) { auto over = base; over.mipLevels = caps.maxTextureMipLevels + 1;
            if (!refused(over)) throw std::runtime_error("A mip chain beyond Capabilities was created."); ++checked; }
        auto layers = base; layers.arrayLayers = std::min(caps.maxTextureArrayLayers, 4U);
        (void)device.CreateTexture(layers); ++checked;
        if (caps.maxTextureArrayLayers < 4) { auto over = base; over.arrayLayers = caps.maxTextureArrayLayers + 1;
            if (!refused(over)) throw std::runtime_error("Array layers beyond Capabilities were created."); ++checked; }
        auto volume = base; volume.depth = 4;
        if (caps.maxTexture3DDimension >= 4) (void)device.CreateTexture(volume);
        else if (!refused(volume)) throw std::runtime_error("A 3D texture was created without the capability.");
        ++checked;
        std::cout << "[subresource caps] PASS; mips<=" << caps.maxTextureMipLevels << " layers<=" << caps.maxTextureArrayLayers
            << " 3D<=" << caps.maxTexture3DDimension << "; " << checked << " creations matched Capabilities\n";
    }

    // Packed depth/stencil: each aspect copied on its own, where the backend
    // says it can; refused at creation where it says it cannot.
    void CheckRhiPackedDepthStencilCopies(NativeRuntime::Rhi::GraphicsDevice& device)
    {
        using namespace NativeRuntime::Rhi;
        const bool supported = device.GetCapabilities().supportsPackedDepthStencilTransfer;
        for (const TextureFormat format : {TextureFormat::D24UnormS8Uint, TextureFormat::D32FloatS8Uint})
        {
            const bool d24 = format == TextureFormat::D24UnormS8Uint;
            TextureDesc desc{}; desc.width = 4; desc.height = 3; desc.format = format;
            desc.usage = TextureUsage::TransferSrc | TextureUsage::TransferDst | TextureUsage::DepthStencilAttachment;
            if (!supported)
            {
                bool refused = false;
                try { (void)device.CreateTexture(desc); } catch (const std::invalid_argument&) { refused = true; }
                if (!refused) throw std::runtime_error("A packed depth/stencil transfer image was created without the capability.");
                continue;
            }
            auto texture = device.CreateTexture(desc);
            ResourceState imageState = ResourceState::Undefined;
            for (const TextureAspect aspect : {TextureAspect::Depth, TextureAspect::Stencil})
            {
                const unsigned bytes = aspect == TextureAspect::Stencil ? 1 : 4;
                std::array<std::byte, 256> input{}, zero{}, sentinel{}, expected{}, actual{};
                sentinel.fill(std::byte{0xD3}); expected = sentinel;
                const unsigned inputOffset = 16, outputOffset = 8;
                for (unsigned y = 0; y < 3; ++y)
                    for (unsigned x = 0; x < 4; ++x)
                        for (unsigned c = 0; c < bytes; ++c) expected[outputOffset + y * 6 * bytes + x * bytes + c] = std::byte{0};
                for (unsigned y = 0; y < 2; ++y)
                    for (unsigned x = 0; x < 3; ++x)
                    {
                        const unsigned position = inputOffset + y * 5 * bytes + x * bytes;
                        const unsigned output = outputOffset + (y + 1) * 6 * bytes + (x + 1) * bytes;
                        if (aspect == TextureAspect::Stencil)
                            input[position] = static_cast<std::byte>(17 * (y * 3 + x) + 3);
                        else if (d24)
                        {
                            const std::uint32_t value = (0x123456U * (y * 3 + x + 1)) & 0xFFFFFFU;
                            std::memcpy(input.data() + position, &value, 4);
                        }
                        else
                        {
                            const float value = 0.125F * static_cast<float>(y * 3 + x + 1);
                            std::memcpy(input.data() + position, &value, 4);
                        }
                        std::memcpy(expected.data() + output, input.data() + position, bytes);
                    }
                auto source = device.CreateBuffer({256, BufferUsage::TransferSrc, MemoryUsage::CpuToGpu});
                auto black = device.CreateBuffer({256, BufferUsage::TransferSrc, MemoryUsage::CpuToGpu});
                auto padding = device.CreateBuffer({256, BufferUsage::TransferSrc, MemoryUsage::CpuToGpu});
                auto output = device.CreateBuffer({256, BufferUsage::TransferDst, MemoryUsage::GpuToCpu});
                device.WriteBuffer(*source, 0, input); device.WriteBuffer(*black, 0, zero); device.WriteBuffer(*padding, 0, sentinel);
                auto commands = device.CreateCommandList(); commands->Begin();
                for (auto* buffer : {source.get(), black.get(), padding.get()})
                    commands->Transition(*buffer, ResourceState::Undefined, ResourceState::CopySrc);
                commands->Transition(*output, ResourceState::Undefined, ResourceState::CopyDst);
                commands->Transition(*texture, imageState, ResourceState::CopyDst);
                commands->CopyBuffer(*padding, 0, *output, 0, 256);
                BufferTextureCopy initial{}; initial.width = 4; initial.height = 3; initial.aspect = aspect;
                commands->CopyBufferToTexture(*black, *texture, initial);
                BufferTextureCopy upload = initial; upload.bufferOffset = inputOffset; upload.bytesPerRow = bytes * 5;
                upload.x = upload.y = 1; upload.width = 3; upload.height = 2;
                commands->CopyBufferToTexture(*source, *texture, upload);
                commands->Transition(*texture, ResourceState::CopyDst, ResourceState::CopySrc);
                auto download = initial; download.bufferOffset = outputOffset; download.bytesPerRow = bytes * 6;
                commands->CopyTextureToBuffer(*texture, *output, download);
                imageState = ResourceState::CopySrc;
                commands->End(); device.ReadBuffer(*output, 0, actual);
                // The high byte of a D24 depth texel is undefined on download.
                if (aspect == TextureAspect::Depth && d24)
                    for (unsigned y = 0; y < 3; ++y)
                        for (unsigned x = 0; x < 4; ++x)
                            actual[outputOffset + y * 6 * 4 + x * 4 + 3] = expected[outputOffset + y * 6 * 4 + x * 4 + 3];
                if (actual != expected || device.DrainErrors())
                    throw std::runtime_error(std::string("Packed depth/stencil aspect copy changed texels or padding: ")
                        + (d24 ? "D24S8 " : "D32FS8 ") + (aspect == TextureAspect::Stencil ? "stencil" : "depth"));
            }
        }
        std::cout << "[packed depth/stencil] " << (supported
            ? "PASS; D24S8/D32FS8 depth and stencil aspects copied separately; subrectangle/padding exact"
            : "PASS; transfer usage refused at creation (no per-aspect transfer on this backend)") << '\n';
    }

    void CheckRhiRgbCopies(NativeRuntime::Rhi::GraphicsDevice& device)
    {
        using namespace NativeRuntime::Rhi;
        TextureDesc desc{}; desc.width = 4; desc.height = 3; desc.format = TextureFormat::RGB8Unorm;
        desc.usage = TextureUsage::Sampled | TextureUsage::ColorAttachment | TextureUsage::TransferSrc | TextureUsage::TransferDst;
        auto image = device.CreateTexture(desc);
        std::array<std::byte, 36> black{};
        device.WriteTexture(*image, {4, 3, TextureFormat::RGB8Unorm, black.data()});
        auto input = device.CreateBuffer({64, BufferUsage::TransferSrc, MemoryUsage::CpuToGpu});
        auto output = device.CreateBuffer({64, BufferUsage::TransferDst, MemoryUsage::GpuToCpu});
        auto padding = device.CreateBuffer({64, BufferUsage::TransferSrc, MemoryUsage::CpuToGpu});
        std::array<std::byte, 64> payload{}, sentinel{}, actual{}, expected{};
        payload.fill(std::byte{0x4D}); sentinel.fill(std::byte{0xA7}); expected = sentinel;
        for (unsigned row = 0; row < 2; ++row)
            for (unsigned byte = 0; byte < 9; ++byte)
            {
                const auto value = static_cast<std::byte>(17 * (row * 9 + byte) + 5);
                payload[11 + row * 15 + byte] = value; expected[7 + row * 18 + byte] = value;
            }
        device.WriteBuffer(*input, 0, payload); device.WriteBuffer(*padding, 0, sentinel);
        auto commands = device.CreateCommandList(); commands->Begin();
        commands->Transition(*input, ResourceState::Undefined, ResourceState::CopySrc);
        commands->Transition(*output, ResourceState::Undefined, ResourceState::CopyDst);
        commands->Transition(*padding, ResourceState::Undefined, ResourceState::CopySrc);
        commands->CopyBuffer(*padding, 0, *output, 0, 64);
        commands->Transition(*image, ResourceState::ShaderRead, ResourceState::CopyDst);
        BufferTextureCopy upload{}; upload.bufferOffset = 11; upload.bytesPerRow = 15;
        upload.x = 1; upload.y = 1; upload.width = 3; upload.height = 2;
        const auto before = device.Statistics();
        commands->CopyBufferToTexture(*input, *image, upload);
        commands->Transition(*image, ResourceState::CopyDst, ResourceState::ShaderRead | ResourceState::CopySrc);
        auto download = upload; download.bufferOffset = 7; download.bytesPerRow = 18;
        commands->CopyTextureToBuffer(*image, *output, download);
        const auto after = device.Statistics();
        if (after.HostWaits != before.HostWaits || after.DeviceWideWaits != before.DeviceWideWaits)
            throw std::runtime_error("RGB GPU transfer inserted a host/device wait.");
        unsigned invalidCopies = 0;
        const auto rejectCopy = [&](auto action) {
            const auto old = device.Statistics(); bool rejected = false;
            try { action(); } catch (const std::invalid_argument&) { rejected = true; }
            catch (const std::out_of_range&) { rejected = true; }
            const auto now = device.Statistics();
            if (!rejected || old.Submitted != now.Submitted || old.HostWaits != now.HostWaits
                || old.DeviceWideWaits != now.DeviceWideWaits || old.LiveObjects() != now.LiveObjects())
                throw std::runtime_error("Invalid RGB copy changed GPU allocation, submission or waits.");
            ++invalidCopies;
        };
        for (unsigned invalid = 0; invalid < 7; ++invalid)
        {
            auto bad = download;
            if (invalid == 0) bad.bufferOffset = 61;
            if (invalid == 1) bad.bytesPerRow = 10;
            if (invalid == 2) bad.mipLevel = 1;
            if (invalid == 3) bad.arrayLayer = 1;
            if (invalid == 4) bad.aspect = TextureAspect::Stencil;
            if (invalid == 5) bad.width = 0;
            if (invalid == 6) bad.x = 2;
            rejectCopy([&] { commands->CopyTextureToBuffer(*image, *output, bad); });
            commands->Transition(*image, ResourceState::ShaderRead | ResourceState::CopySrc, ResourceState::CopyDst);
            rejectCopy([&] { commands->CopyBufferToTexture(*input, *image, bad); });
            commands->Transition(*image, ResourceState::CopyDst, ResourceState::ShaderRead | ResourceState::CopySrc);
        }
        commands->CopyTextureToBuffer(*image, *output, download);
        commands->End(); device.ReadBuffer(*output, 0, actual);
        if (actual != expected) throw std::runtime_error("RGB GPU copy changed channels or surrounding buffer padding.");
        auto view = device.CreateTextureView(*image, {});
        RenderingColorAttachment color{view.get()};
        RenderingInfo info{}; info.width = 4; info.height = 3; info.colorAttachments = std::span(&color, 1);
        std::array<std::byte, 48> rgba{}; commands->ReadColor(info, 0, 0, 4, 3, TextureFormat::RGBA8Unorm, rgba.data());
        for (unsigned y = 0; y < 3; ++y)
            for (unsigned x = 0; x < 4; ++x)
            {
                const auto offset = (y * 4 + x) * 4;
                for (unsigned c = 0; c < 3; ++c)
                {
                    const auto value = x && y ? payload[11 + (y - 1) * 15 + (x - 1) * 3 + c] : std::byte{0};
                    if (rgba[offset + c] != value) throw std::runtime_error("RGB GPU upload damaged untouched image pixels.");
                }
                if (rgba[offset + 3] != std::byte{255}) throw std::runtime_error("Logical RGB alpha must remain opaque.");
            }
        commands->Begin(); commands->Transition(*image, ResourceState::ShaderRead | ResourceState::CopySrc, ResourceState::ShaderRead);
        commands->End();
        if (device.GetBackend() == GraphicsBackend::Vulkan)
        {
            for (const bool volume : {false, true})
            {
                auto layered = desc; layered.width = 12; layered.height = 8; layered.depth = volume ? 4 : 1;
                layered.arrayLayers = volume ? 1 : 3; layered.mipLevels = 2;
                auto subImage = device.CreateTexture(layered);
                auto subInput = device.CreateBuffer({256, BufferUsage::TransferSrc, MemoryUsage::CpuToGpu});
                auto subOutput = device.CreateBuffer({256, BufferUsage::TransferDst, MemoryUsage::GpuToCpu});
                auto subPadding = device.CreateBuffer({256, BufferUsage::TransferSrc, MemoryUsage::CpuToGpu});
                std::array<std::byte, 256> subPayload{}, subSentinel{}, subActual{}, subExpected{};
                subPayload.fill(std::byte{0x6A}); subSentinel.fill(std::byte{0xA3}); subExpected = subSentinel;
                BufferTextureCopy sub{}; sub.mipLevel = 1; sub.arrayLayer = volume ? 0 : 2;
                sub.bufferOffset = 11; sub.bytesPerRow = 18; sub.rowsPerImage = 5;
                sub.x = sub.y = 1; sub.width = 4; sub.height = 3; sub.depth = volume ? 2 : 1;
                auto subDownload = sub; subDownload.bufferOffset = 7; subDownload.bytesPerRow = 21; subDownload.rowsPerImage = 6;
                for (unsigned z = 0; z < sub.depth; ++z) for (unsigned y = 0; y < 3; ++y) for (unsigned byte = 0; byte < 12; ++byte)
                {
                    const auto value = std::byte((z * 73 + y * 19 + byte * 11) % 256);
                    subPayload[11 + z * 90 + y * 18 + byte] = value;
                    subExpected[7 + z * 126 + y * 21 + byte] = value;
                }
                device.WriteBuffer(*subInput, 0, subPayload); device.WriteBuffer(*subPadding, 0, subSentinel);
                commands->Begin();
                commands->Transition(*subInput, ResourceState::Undefined, ResourceState::CopySrc);
                commands->Transition(*subPadding, ResourceState::Undefined, ResourceState::CopySrc);
                commands->Transition(*subOutput, ResourceState::Undefined, ResourceState::CopyDst);
                commands->CopyBuffer(*subPadding, 0, *subOutput, 0, 256);
                commands->Transition(*subImage, ResourceState::Undefined, ResourceState::CopyDst);
                commands->CopyBufferToTexture(*subInput, *subImage, sub);
                commands->Transition(*subImage, ResourceState::CopyDst, ResourceState::ShaderRead | ResourceState::CopySrc);
                commands->CopyTextureToBuffer(*subImage, *subOutput, subDownload); commands->End();
                device.ReadBuffer(*subOutput, 0, subActual);
                if (subActual != subExpected) throw std::runtime_error("RGB mip/layer/volume GPU copy changed pixels or padding.");
            }
            std::cout << "[rgb copy] Vulkan mip/array/3D PASS; nonbase subresources; independent row/slice padding\n";
        }

        // Exercise more than one metadata batch and oversized scratch pages,
        // then reuse both native command slots repeatedly without growth.
        constexpr unsigned wide = 4097, tall = 5;
        auto largeDesc = desc; largeDesc.width = wide; largeDesc.height = tall;
        auto largeImage = device.CreateTexture(largeDesc);
        const unsigned pitch = (wide + 2) * 3, size = 11 + pitch * tall;
        auto largeInput = device.CreateBuffer({size, BufferUsage::TransferSrc, MemoryUsage::CpuToGpu});
        auto largeOutput = device.CreateBuffer({size, BufferUsage::TransferDst, MemoryUsage::GpuToCpu});
        auto largePadding = device.CreateBuffer({size, BufferUsage::TransferSrc, MemoryUsage::CpuToGpu});
        std::vector<std::byte> largePayload(size), largeSentinel(size, std::byte{0xA9}), largeActual(size), largeExpected = largeSentinel;
        for (unsigned y = 0; y < tall; ++y) for (unsigned byte = 0; byte < wide * 3; ++byte)
        {
            const auto value = std::byte((y * 17 + byte * 13) % 256);
            largePayload[11 + y * pitch + byte] = value; largeExpected[11 + y * pitch + byte] = value;
        }
        device.WriteBuffer(*largeInput, 0, largePayload); device.WriteBuffer(*largePadding, 0, largeSentinel);
        commands->Begin(); commands->Transition(*largeInput, ResourceState::Undefined, ResourceState::CopySrc);
        commands->Transition(*largePadding, ResourceState::Undefined, ResourceState::CopySrc);
        commands->Transition(*largeOutput, ResourceState::Undefined, ResourceState::CopyDst);
        commands->Transition(*largeImage, ResourceState::Undefined, ResourceState::CopyDst); commands->End();
        BufferTextureCopy largeRegion{}; largeRegion.bufferOffset = 11; largeRegion.bytesPerRow = pitch;
        largeRegion.width = wide; largeRegion.height = tall;
        unsigned stableBuffers = 0;
        for (unsigned cycle = 0; cycle < 64; ++cycle)
        {
            commands->Begin(); commands->CopyBuffer(*largePadding, 0, *largeOutput, 0, size);
            commands->CopyBufferToTexture(*largeInput, *largeImage, largeRegion);
            commands->Transition(*largeImage, ResourceState::CopyDst, ResourceState::ShaderRead | ResourceState::CopySrc);
            commands->CopyTextureToBuffer(*largeImage, *largeOutput, largeRegion);
            commands->Transition(*largeImage, ResourceState::ShaderRead | ResourceState::CopySrc, ResourceState::CopyDst); commands->End();
            device.ReadBuffer(*largeOutput, 0, largeActual);
            if (largeActual != largeExpected) throw std::runtime_error("Batched/oversized RGB GPU copy changed pixels or padding.");
            const auto buffers = device.Statistics().Buffers + device.Statistics().TransferScratchPages;
            if (cycle == 3) stableBuffers = buffers;
            if (cycle > 3 && buffers != stableBuffers) throw std::runtime_error("RGB command scratch buffer count kept growing.");
        }
        std::cout << "[rgb copy] batched/oversized/reuse PASS; 64 cycles; scratch buffer count stable\n";
        if (device.DrainErrors()) throw std::runtime_error("RGB GPU copy raised native errors.");
        std::cout << "[rgb copy] PASS; rejected=" << invalidCopies
            << "; three-byte pixels; unaligned offsets; independent row pitches; subrectangle; padding; opaque alpha; combined state; copy waits=0\n";
    }

    void CheckRhiResourceStates(NativeRuntime::Rhi::GraphicsDevice& device)
    {
        using namespace NativeRuntime::Rhi;
        unsigned rejected = 0;
        const auto reject = [&](auto action) {
            bool caught = false; const auto before = device.Statistics();
            try { action(); } catch (const std::invalid_argument&) { caught = true; }
            if (!caught || device.DrainErrors()) throw std::runtime_error("Invalid resource state reached the native API.");
            const auto after = device.Statistics();
            if (before.Submitted != after.Submitted || before.HostWaits != after.HostWaits
                || before.DeviceWideWaits != after.DeviceWideWaits || before.LiveObjects() != after.LiveObjects())
                throw std::runtime_error("Invalid state changed submissions, waits or native ownership.");
            ++rejected;
        };
        constexpr auto unknown = static_cast<ResourceState>(1U << 31);
        TextureDesc desc{}; desc.width = desc.height = 2; desc.format = TextureFormat::RGBA8Unorm;
        desc.usage = TextureUsage::Sampled | TextureUsage::TransferSrc | TextureUsage::TransferDst;
        for (const auto state : {unknown, ResourceState::CopyDst | ResourceState::ShaderRead,
            ResourceState::VertexBuffer, ResourceState::ConstantBuffer})
        { auto invalid = desc; invalid.initialState = state; reject([&] { (void)device.CreateTexture(invalid); }); }
        for (const auto state : {unknown, ResourceState::CopyDst | ResourceState::CopySrc,
            ResourceState::ColorAttachment, ResourceState::DepthStencilRead, ResourceState::Present})
            reject([&] { (void)device.CreateBuffer({16, BufferUsage::TransferSrc, MemoryUsage::CpuToGpu, state}); });
        for (const auto state : {ResourceState::VertexBuffer, ResourceState::IndexBuffer, ResourceState::ConstantBuffer,
            ResourceState::ShaderRead, ResourceState::ShaderWrite, ResourceState::CopyDst})
            reject([&] { (void)device.CreateBuffer({16, BufferUsage::TransferSrc, MemoryUsage::CpuToGpu, state}); });
        for (const auto state : {ResourceState::ColorAttachment, ResourceState::DepthStencilRead,
            ResourceState::DepthStencilWrite, ResourceState::ShaderWrite, ResourceState::Present})
        { auto invalid = desc; invalid.initialState = state; reject([&] { (void)device.CreateTexture(invalid); }); }
        for (const auto state : {ResourceState::CopySrc, ResourceState::CopyDst})
        { auto invalid = desc; invalid.usage = TextureUsage::Sampled; invalid.initialState = state;
            reject([&] { (void)device.CreateTexture(invalid); }); }
        for (const auto format : {TextureFormat::RGBA8Unorm, TextureFormat::D24UnormS8Uint})
        { auto invalid = desc; invalid.format = format;
            invalid.usage = format == TextureFormat::RGBA8Unorm ? TextureUsage::DepthStencilAttachment : TextureUsage::ColorAttachment;
            reject([&] { (void)device.CreateTexture(invalid); }); }
        reject([&] { auto invalid = desc; invalid.usage = static_cast<TextureUsage>(1U << 31); (void)device.CreateTexture(invalid); });
        reject([&] { (void)device.CreateBuffer({16, static_cast<BufferUsage>(1U << 31)}); });
        // These logical formats have no shader image format qualifier. A
        // sampled/color texture must not become an invalid storage binding.
        for (const auto format : {TextureFormat::RGB8Unorm, TextureFormat::RGB32Float,
            TextureFormat::RGBA8Srgb, TextureFormat::BGRA8Unorm, TextureFormat::BGRA8Srgb, TextureFormat::D16Unorm,
            TextureFormat::D24UnormS8Uint, TextureFormat::D32Float, TextureFormat::D32FloatS8Uint})
        {
            auto invalid = desc; invalid.format = format; invalid.usage = TextureUsage::Storage;
            reject([&] { (void)device.CreateTexture(invalid); });
        }

        auto source = device.CreateBuffer({16, BufferUsage::TransferSrc, MemoryUsage::CpuToGpu, ResourceState::Common});
        auto output = device.CreateBuffer({16, BufferUsage::TransferDst, MemoryUsage::GpuToCpu});
        auto image = device.CreateTexture(desc);
        std::array<std::byte, 16> payload{}, actual{};
        for (unsigned i = 0; i < payload.size(); ++i) payload[i] = static_cast<std::byte>(17 * i + 3);
        device.WriteBuffer(*source, 0, payload); // Host writes preserve Common.
        auto commands = device.CreateCommandList(); commands->Begin();
        BufferTextureCopy region{}; region.width = region.height = 2;
        reject([&] { commands->CopyBuffer(*source, 0, *output, 0, 16); });
        reject([&] { commands->CopyBufferToTexture(*source, *image, region); });
        reject([&] { commands->CopyTextureToBuffer(*image, *output, region); });
        reject([&] { commands->Transition(*source, ResourceState::Undefined, ResourceState::CopySrc); });
        for (const auto state : {ResourceState::VertexBuffer, ResourceState::IndexBuffer, ResourceState::ConstantBuffer,
            ResourceState::ShaderRead, ResourceState::ShaderWrite, ResourceState::CopyDst})
            reject([&] { commands->Transition(*source, ResourceState::Common, state); });
        for (const auto state : {unknown, ResourceState::Common, ResourceState::ColorAttachment,
            ResourceState::CopyDst | ResourceState::ShaderRead})
            reject([&] { commands->Transition(*source, ResourceState::Common, state); });
        commands->Transition(*source, ResourceState::Common, ResourceState::CopySrc);
        commands->Transition(*output, ResourceState::Undefined, ResourceState::CopyDst);
        for (const auto state : {unknown, ResourceState::Undefined, ResourceState::VertexBuffer,
            ResourceState::ShaderRead | ResourceState::ShaderWrite})
            reject([&] { commands->Transition(*image, ResourceState::Undefined, state); });
        for (const auto state : {ResourceState::ColorAttachment, ResourceState::DepthStencilRead,
            ResourceState::DepthStencilWrite, ResourceState::ShaderWrite, ResourceState::Present})
            reject([&] { commands->Transition(*image, ResourceState::Undefined, state); });
        commands->Transition(*image, ResourceState::Undefined, ResourceState::CopyDst);
        reject([&] { commands->Transition(*image, ResourceState::Undefined, ResourceState::CopySrc); });
        commands->CopyBufferToTexture(*source, *image, region);
        reject([&] { commands->CopyTextureToBuffer(*image, *output, region); });
        commands->Transition(*image, ResourceState::CopyDst, ResourceState::ShaderRead | ResourceState::CopySrc);
        commands->Transition(*image, ResourceState::ShaderRead | ResourceState::CopySrc, ResourceState::CopySrc);
        commands->CopyTextureToBuffer(*image, *output, region); commands->End();
        device.ReadBuffer(*output, 0, actual);
        if (actual != payload) throw std::runtime_error("Rejected state transitions damaged subsequent GPU copies.");

        // CopySrc is a read permission, including when another read use is
        // enabled. Exercise each explicit copy without narrowing the state.
        constexpr auto bufferReads = ResourceState::VertexBuffer | ResourceState::CopySrc;
        constexpr auto textureReads = ResourceState::ShaderRead | ResourceState::CopySrc;
        auto combined = device.CreateBuffer({16, BufferUsage::Vertex | BufferUsage::TransferSrc,
            MemoryUsage::CpuToGpu, bufferReads});
        device.WriteBuffer(*combined, 0, payload);
        commands->Begin(); commands->CopyBuffer(*combined, 0, *output, 0, 16); commands->End();
        device.ReadBuffer(*output, 0, actual);
        if (actual != payload) throw std::runtime_error("Combined buffer read states changed copied bytes.");
        commands->Begin(); commands->Transition(*image, ResourceState::CopySrc, ResourceState::CopyDst);
        commands->CopyBufferToTexture(*combined, *image, region);
        commands->Transition(*image, ResourceState::CopyDst, textureReads);
        commands->CopyTextureToBuffer(*image, *output, region);
        // The copy must retain the combined state, including on another list.
        commands->End();
        auto combinedCommands = device.CreateCommandList(); combinedCommands->Begin();
        combinedCommands->Transition(*image, textureReads, ResourceState::CopySrc);
        combinedCommands->Transition(*combined, bufferReads, ResourceState::Common); combinedCommands->End();
        device.ReadBuffer(*output, 0, actual);
        if (actual != payload) throw std::runtime_error("Combined image read states changed transferred pixels.");

        auto other = device.CreateCommandList(); other->Begin();
        other->Transition(*source, ResourceState::CopySrc, ResourceState::Common); other->End();
        commands->Begin(); reject([&] { commands->Transition(*source, ResourceState::CopySrc, ResourceState::Common); });
        commands->Transition(*source, ResourceState::Common, ResourceState::CopySrc); commands->End();
        auto initializedDesc = desc; initializedDesc.initialState = ResourceState::ShaderRead;
        auto initialized = device.CreateTexture(initializedDesc);
        commands->Begin(); commands->Transition(*initialized, ResourceState::ShaderRead, ResourceState::CopyDst); commands->End();
        auto sampledDesc = desc; sampledDesc.usage = TextureUsage::Sampled;
        auto sampled = device.CreateTexture(sampledDesc);
        commands->Begin();
        for (const auto state : {ResourceState::CopySrc, ResourceState::CopyDst})
            reject([&] { commands->Transition(*sampled, ResourceState::Undefined, state); });
        commands->Transition(*sampled, ResourceState::Undefined, ResourceState::ShaderRead); commands->End();

        auto storageDesc = desc; storageDesc.usage = TextureUsage::Storage | TextureUsage::TransferSrc | TextureUsage::TransferDst;
        storageDesc.initialState = ResourceState::ShaderRead;
        auto storage = device.CreateTexture(storageDesc);
        commands->Begin(); commands->Transition(*storage, ResourceState::ShaderRead, ResourceState::CopyDst);
        commands->CopyBufferToTexture(*source, *storage, region);
        commands->Transition(*storage, ResourceState::CopyDst, ResourceState::ShaderWrite);
        commands->Transition(*storage, ResourceState::ShaderWrite, ResourceState::ShaderRead);
        commands->Transition(*storage, ResourceState::ShaderRead, textureReads);
        commands->CopyTextureToBuffer(*storage, *output, region); commands->End();
        device.ReadBuffer(*output, 0, actual);
        if (actual != payload) throw std::runtime_error("Storage texture state transitions changed transferred pixels.");

        device.ResizeTexture(*image, 2, 2); // Same extent retains its state.
        commands->Begin(); commands->Transition(*image, ResourceState::CopySrc, ResourceState::Common); commands->End();
        device.ResizeTexture(*image, 3, 2); // New storage starts Undefined.
        commands->Begin(); reject([&] { commands->Transition(*image, ResourceState::Common, ResourceState::CopyDst); });
        commands->Transition(*image, ResourceState::Undefined, ResourceState::CopyDst); commands->End();
        std::array<std::byte, 24> pixels{}; device.WriteTexture(*image, {3, 2, TextureFormat::RGBA8Unorm, pixels.data()});
        commands->Begin(); commands->Transition(*image, ResourceState::ShaderRead, ResourceState::Common); commands->End();
        device.WriteTexture(*image, {3, 2, TextureFormat::RGBA8Unorm, pixels.data()}); // Preserve established Common.
        commands->Begin(); commands->Transition(*image, ResourceState::Common, ResourceState::ShaderRead); commands->End();

        auto gpu = device.CreateBuffer({16, BufferUsage::TransferDst | BufferUsage::TransferSrc, MemoryUsage::GpuOnly});
        device.WriteBuffer(*gpu, 0, payload);
        commands->Begin(); commands->Transition(*gpu, ResourceState::CopyDst, ResourceState::CopySrc);
        commands->CopyBuffer(*gpu, 0, *output, 0, 16); commands->End(); device.ReadBuffer(*output, 0, actual);
        if (actual != payload || device.DrainErrors()) throw std::runtime_error("GPU upload state or pixels differ.");
        std::cout << "[resource states] PASS; rejected=" << rejected
            << "; initial/tracked state; invalid bits/type/write/usage/format; copies preserved; combined reads; resize/upload/storage states\n";
    }
}
