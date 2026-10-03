#include "../NativeRuntime/Rhi/Vulkan/VulkanUploadArena.hpp"
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
#include <type_traits>

namespace
{
    using namespace MphRead::NativeRuntime::Rhi;
    using Arena = Vulkan::VulkanUploadArena;
    void Expect(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
    template<class F> void Reject(F action)
    {
        bool rejected = false;
        try { action(); } catch (const std::exception&) { rejected = true; }
        Expect(rejected, "Invalid upload request was accepted.");
    }
    template<class T> T Handle(std::uint64_t value)
    {
        if constexpr (std::is_pointer_v<T>) return reinterpret_cast<T>(value);
        else return static_cast<T>(value);
    }
    struct Memory final
    {
        std::map<VkBuffer, std::vector<std::byte>> Pages;
        struct Range { VkBuffer Buffer; VkDeviceSize Offset, Size; };
        std::vector<Range> Flushed;
        unsigned Created = 0, Destroyed = 0, FlushAttempts = 0;
        bool FailCreate = false, BadMap = false;
        unsigned FailFlushAttempt = 0;
        Arena::Dispatch Dispatch()
        {
            return {
                [this](VkDeviceSize size) {
                    if (FailCreate) throw std::runtime_error("injected create OOM");
                    const auto buffer = Handle<VkBuffer>(++Created);
                    auto& storage = Pages[buffer]; storage.resize(static_cast<std::size_t>(size));
                    return Arena::Page{buffer, Handle<VmaAllocation_T*>(Created), BadMap ? nullptr : storage.data(), size};
                },
                [this](const Arena::Page& page) {
                    Expect(Pages.erase(page.Buffer) == 1, "Native upload page was destroyed twice.");
                    ++Destroyed;
                },
                [this](const Arena::Page& page, VkDeviceSize offset, VkDeviceSize size) {
                    if (++FlushAttempts == FailFlushAttempt) throw std::runtime_error("injected flush error");
                    Expect(size && offset <= page.Size && size <= page.Size - offset, "Flush is outside mapped page.");
                    Flushed.push_back({page.Buffer, offset, size});
                }};
        }
    };
    void PackingAndReuse()
    {
        Memory memory;
        Arena arena(memory.Dispatch(), 64);
        const auto a = arena.Allocate(9, 4);
        const auto b = arena.Allocate(16, 16);
        const auto c = arena.Allocate(24, 12);
        Expect(a.Offset == 0 && b.Offset == 16 && c.Offset == 36 && a.Buffer == c.Buffer,
            "Upload slice alignment changed or overlapped.");
        std::memset(a.Data, 1, 9); std::memset(b.Data, 2, 16); std::memset(c.Data, 3, 24);
        const auto overflow = arena.Allocate(128, 16);
        Expect(overflow.Offset == 0 && overflow.Buffer != a.Buffer && arena.ReservedBytes() == 192,
            "Oversized upload did not get one sufficient page.");
        Expect(memory.Pages[a.Buffer][8] == std::byte{1} && memory.Pages[a.Buffer][16] == std::byte{2}
            && memory.Pages[a.Buffer][59] == std::byte{3}, "Growth invalidated mapped slice addresses.");
        Reject([&] { arena.Submitted({1}); });
        arena.FlushPending();
        Expect(memory.Flushed.size() == 2 && memory.Flushed[0].Offset == 0 && memory.Flushed[0].Size == 60
            && memory.Flushed[1].Size == 128, "Flush failed to coalesce allocated ranges.");
        arena.FlushPending(); Expect(memory.Flushed.size() == 2, "Clean pages were flushed again.");
        arena.Submitted({7});
        Reject([&] { arena.Allocate(1); }); Reject([&] { arena.FlushPending(); });
        Reject([&] { arena.ResetAfterCompletion({6}); });
        arena.ResetAfterCompletion({7});
        Expect(arena.Allocate(9, 4).Buffer == a.Buffer, "Completed page was not reused.");
        Expect(arena.Allocate(128).Buffer == overflow.Buffer && memory.Created == 2, "Overflow high water was not reused.");
        arena.FlushPending();
        Reject([&] { arena.Submitted({7}); }); arena.Submitted({9}); arena.ResetAfterCompletion({11});
        arena.Close(); arena.Close();
        Expect(memory.Pages.empty() && memory.Destroyed == 2 && !arena.PageCount() && !arena.ReservedBytes(),
            "Upload arena retained native pages after close.");
        Reject([&] { arena.Allocate(1); }); Reject([&] { arena.ResetAfterCompletion({20}); });
    }
    void FailuresAndSlots()
    {
        Memory memory;
        Arena first(memory.Dispatch(), 32), second(memory.Dispatch(), 32);
        const auto old = first.Allocate(32);
        memory.FailCreate = true;
        Reject([&] { first.Allocate(40); });
        Expect(first.PageCount() == 1 && first.ReservedBytes() == 32 && memory.Pages.count(old.Buffer),
            "Failed native admission changed existing upload ownership.");
        memory.FailCreate = false;
        memory.BadMap = true;
        Reject([&] { first.Allocate(40); });
        Expect(memory.Pages.size() == 1 && memory.Destroyed == 1, "Incomplete native mapping leaked its page.");
        memory.BadMap = false;
        const auto added = first.Allocate(40);
        memory.FailFlushAttempt = 2;
        Reject([&] { first.FlushPending(); });
        Reject([&] { first.Submitted({2}); });
        memory.FailFlushAttempt = 0; first.FlushPending();
        Expect(memory.Flushed.size() == 2 && memory.Flushed.back().Buffer == added.Buffer,
            "Partial flush failure lost the remaining dirty range.");
        first.Submitted({2});
        const auto independent = second.Allocate(8);
        Expect(independent.Buffer != old.Buffer && independent.Buffer != added.Buffer, "Slots share mapped pages.");
        Reject([&] { second.Allocate(0); }); Reject([&] { second.Allocate(4, 0); });
        second.FlushPending(); Reject([&] { second.Submitted({0}); }); second.Submitted({1});
        second.ResetAfterCompletion({1});
        Reject([&] { first.ResetAfterCompletion({1}); }); first.ResetAfterCompletion({2});
        // CPU dispatch cannot prove GPU idleness; explicit close assumes the
        // real owner has drained/discarded both slots, as the production caller does.
        first.Close(); second.Close();
        Expect(memory.Pages.empty() && memory.Created == memory.Destroyed, "Slot teardown leaked pages.");
    }
}
int main()
{
    try
    {
        PackingAndReuse(); FailuresAndSlots();
        std::cout << "Vulkan upload arena: alignment, overflow reuse, completion, native faults, independent slots, close PASS\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
