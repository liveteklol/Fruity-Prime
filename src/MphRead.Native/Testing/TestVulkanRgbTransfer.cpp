#include "../NativeRuntime/Rhi/Vulkan/VulkanRgbTransfer.hpp"
#include "../NativeRuntime/Rhi/Vulkan/VulkanTransferScratch.hpp"
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <type_traits>

namespace
{
    using namespace MphRead::NativeRuntime::Rhi;
    using namespace MphRead::NativeRuntime::Rhi::Vulkan;
    void Expect(bool good, const char* why) { if (!good) throw std::runtime_error(why); }
    template<class T> T Handle(std::uint64_t value)
    { if constexpr (std::is_pointer_v<T>) return reinterpret_cast<T>(value); else return static_cast<T>(value); }
    template<class F> void Reject(F action)
    { bool rejected = false; try { action(); } catch (const std::exception&) { rejected = true; } Expect(rejected, "Invalid transfer operation succeeded."); }
    void CheckAddresses()
    {
        TextureDesc desc{}; desc.width = 1030; desc.height = 8; desc.depth = 4; desc.mipLevels = 2;
        desc.format = TextureFormat::RGB8Unorm;
        BufferTextureCopy region{}; region.bufferOffset = 11; region.width = 1025; region.height = 3; region.depth = 2;
        region.x = 1; region.y = 2; region.z = 1; region.bytesPerRow = 3081; region.rowsPerImage = 5;
        const auto plan = VulkanRgbTransfer::Describe(desc, 65536, region);
        Expect(plan.RowPitch == 3081 && plan.SlicePitch == 15405 && plan.ScratchBytes == 24600,
            "Logical RGB pitch or native scratch size differs.");
        const auto native = plan.ImageCopy(16);
        Expect(native.bufferOffset == 16 && !native.bufferRowLength && !native.bufferImageHeight
            && native.imageOffset.x == 1 && native.imageOffset.y == 2 && native.imageOffset.z == 1
            && native.imageExtent.depth == 2, "Native image copy lost its tightly packed subresource.");
        std::vector<std::byte> packed(65536), rgba(plan.ScratchBytes + 16, std::byte{255}), output(65536, std::byte{0xB9});
        for (unsigned i = 0; i < packed.size(); ++i) packed[i] = std::byte((i * 17 + 13) % 256);
        unsigned calls = 0, pixels = 0;
        plan.BufferCopies(16, true, [&](auto batch) {
            ++calls; Expect(batch.size() <= 256, "RGB command metadata is not bounded.");
            for (const auto& copy : batch)
            { Expect(copy.size == 3, "Alpha was copied from a logical RGB buffer."); ++pixels; std::memcpy(rgba.data() + copy.dstOffset, packed.data() + copy.srcOffset, copy.size); }
        });
        Expect(calls == 25 && pixels == 6150, "RGB transfer batches lost pixels.");
        plan.BufferCopies(16, false, [&](auto batch) {
            for (const auto& copy : batch) std::memcpy(output.data() + copy.dstOffset, rgba.data() + copy.srcOffset, copy.size);
        });
        auto expected = std::vector<std::byte>(65536, std::byte{0xB9});
        for (unsigned z = 0; z < 2; ++z) for (unsigned y = 0; y < 3; ++y)
        {
            const auto offset = 11 + z * 15405 + y * 3081;
            std::memcpy(expected.data() + offset, packed.data() + offset, 3075);
        }
        Expect(output == expected, "RGB scatter overwrote row/slice/prefix/trailing padding.");
        for (std::size_t i = 19; i < rgba.size(); i += 4) Expect(rgba[i] == std::byte{255}, "RGB gather overwrote alpha.");
        desc.depth = 1; desc.arrayLayers = 2; region = {}; region.mipLevel = 1; region.arrayLayer = 1;
        region.width = 3; region.height = 2;
        const auto layered = VulkanRgbTransfer::Describe(desc, 18, region).ImageCopy(0);
        Expect(layered.imageSubresource.mipLevel == 1 && layered.imageSubresource.baseArrayLayer == 1, "RGB mip/layer was lost.");
        Reject([&] { (void)VulkanRgbTransfer::Describe(desc, 17, region); });
        for (int invalid = 0; invalid < 9; ++invalid)
        {
            auto bad = region;
            if (invalid == 0) bad.bytesPerRow = 10;
            if (invalid == 1) bad.bytesPerRow = 6;
            if (invalid == 2) bad.rowsPerImage = 1;
            if (invalid == 3) bad.arrayLayer = 2;
            if (invalid == 4) bad.mipLevel = 2;
            if (invalid == 5) bad.depth = 2;
            if (invalid == 6) bad.aspect = TextureAspect::Depth;
            if (invalid == 7) bad.bufferOffset = std::numeric_limits<VkDeviceSize>::max();
            if (invalid == 8) bad.x = desc.width;
            Reject([&] { (void)VulkanRgbTransfer::Describe(desc, 65536, bad); });
        }
        Reject([&] { (void)plan.ImageCopy(3); });
        Reject([&] { (void)plan.ImageCopy(std::numeric_limits<VkDeviceSize>::max() - 3); });
        desc.width = 0; Reject([&] { (void)VulkanRgbTransfer::Describe(desc, 65536, region); });
        desc = {}; desc.width = 4; desc.height = 3; desc.format = TextureFormat::RGB32Float;
        region = {}; region.width = 3; region.height = 2; region.x = region.y = 1;
        region.bufferOffset = 7; region.bytesPerRow = 60;
        const auto floats = VulkanRgbTransfer::Describe(desc, 103, region);
        Expect(floats.LogicalBytes == 12 && floats.NativeBytes == 16 && floats.AlphaWord == 0x3F800000U
            && floats.RowPitch == 60 && floats.ScratchBytes == 96, "RGB32F logical/native packing differs.");
        unsigned floatPixels = 0;
        floats.BufferCopies(16, true, [&](auto batch) {
            for (const auto& copy : batch)
            {
                const auto index = floatPixels++;
                Expect(copy.size == 12 && copy.srcOffset == 7 + (index / 3) * 60 + (index % 3) * 12
                    && copy.dstOffset == 16 + index * 16, "RGB32F gather lost pitch/texel/alignment.");
            }
        });
        Expect(floatPixels == 6 && floats.ImageCopy(16).bufferOffset == 16, "RGB32F gather lost pixels.");
        Reject([&] { (void)floats.ImageCopy(4); });
        Reject([&] { (void)VulkanRgbTransfer::Describe(desc, 102, region); });
        region.bytesPerRow = 40; Reject([&] { (void)VulkanRgbTransfer::Describe(desc, 512, region); });
    }
    void CheckScratch()
    {
        std::uint64_t next = 0; unsigned created = 0, destroyed = 0;
        bool fail = false, malformed = false;
        std::map<VkBuffer, VkDeviceSize> pages;
        VulkanTransferScratch arena({[&](VkDeviceSize bytes) {
            if (fail) throw std::runtime_error("injected allocation failure");
            const auto name = Handle<VkBuffer>(++next); pages[name] = bytes; ++created;
            return VulkanTransferScratch::Page{name, Handle<VmaAllocation_T*>(next), malformed ? bytes - 1 : bytes};
        }, [&](const auto& page) { pages.erase(page.Buffer); ++destroyed; }}, 64);
        Reject([&] { (void)arena.Allocate(0); }); Reject([&] { (void)arena.Allocate(std::numeric_limits<VkDeviceSize>::max()); });
        for (unsigned cycle = 0; cycle < 64; ++cycle)
        {
            auto a = arena.Allocate(13), b = arena.Allocate(32), c = arena.Allocate(80);
            Expect(a.Offset == 0 && a.Size == 16 && b.Buffer == a.Buffer && b.Offset == 16 && c.Size == 80,
                "Scratch suballocation changed or overlapped.");
            Expect(arena.PageCount() == 2 && arena.ReservedBytes() == 144 && created == 2,
                "Completed scratch pages were not reused.");
            const auto serial = SubmissionSerial{cycle + 1}; arena.Submitted(serial);
            Reject([&] { (void)arena.Allocate(4); }); Reject([&] { arena.ResetAfterCompletion({cycle}); });
            Expect(pages.size() == 2 && destroyed == 0, "Pending scratch storage was released.");
            arena.ResetAfterCompletion(serial);
        }
        fail = true; Reject([&] { (void)arena.Allocate(1000); });
        Expect(arena.PageCount() == 2 && arena.ReservedBytes() == 144, "Failed scratch allocation changed ownership.");
        fail = false; malformed = true; Reject([&] { (void)arena.Allocate(1000); });
        Expect(pages.size() == 2 && created == 3 && destroyed == 1, "Malformed scratch allocation leaked.");
        malformed = false; arena.ResetAfterCompletion({64});
        const auto prefix = arena.Allocate(4), aligned = arena.Allocate(16, 16), nextAligned = arena.Allocate(16, 16);
        Expect(prefix.Offset == 0 && aligned.Offset == 16 && nextAligned.Offset == 32
            && aligned.Buffer == prefix.Buffer && nextAligned.Buffer == prefix.Buffer && created == 3,
            "Native RGBA32F scratch alignment overlaps previous RGB8 texels or allocates unnecessarily.");
        Reject([&] { (void)arena.Allocate(4, 0); }); Reject([&] { (void)arena.Allocate(4, 3); });
        Reject([&] { (void)arena.Allocate(4, 12); });
        arena.Close(); arena.Close(); Expect(pages.empty() && destroyed == created, "Scratch close leaked native storage.");
        Reject([&] { (void)arena.Allocate(4); }); Reject([&] { arena.ResetAfterCompletion({64}); });
    }
}
int main()
{
    try { CheckAddresses(); CheckScratch(); std::cout << "Vulkan RGB transfer PASS; bounded gather/scatter; pitch/mip/layer/volume; overflow; 64 scratch completion/reuse cycles; failure/close\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
