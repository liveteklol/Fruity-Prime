#include "../NativeRuntime/Rhi/Readback.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

using namespace MphRead::NativeRuntime::Rhi;
namespace
{
    void Expect(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
    struct Control { bool ready = false, failPoll = false, failCopy = false; int copies = 0, destroyed = 0, issued = 0; };
    struct Fake final : ReadbackTransfer
    {
        Control& control;
        explicit Fake(Control& value) : control(value) { ++control.issued; }
        ~Fake() { ++control.destroyed; }
        bool IsReady() override { if (control.failPoll) throw std::runtime_error("injected completion failure"); return control.ready; }
        void CopyResult(std::span<std::byte> output) override
        {
            if (control.failCopy) throw std::runtime_error("injected mapping failure");
            ++control.copies;
            for (std::size_t i = 0; i < output.size(); ++i) output[i] = std::byte(i * 17);
        }
    };
    template<class F> void Reject(F&& operation)
    { bool rejected = false; try { operation(); } catch (const std::exception&) { rejected = true; } Expect(rejected, "Expected rejection."); }
}
int main()
{
    try
    {
        Control control;
        ReadbackResult lease;
        {
            ReadbackQueue queue;
            queue.SetLimits({1, 32});
            const auto issue = [&] { return std::make_unique<Fake>(control); };
            auto ticket = queue.Enqueue(16, 16, issue);
            Expect(ticket && !ticket.IsReady() && control.copies == 0, "Pending copy was mapped or reported ready.");
            Reject([&] { (void)ticket.MapResult(); });
            Expect(!queue.Enqueue(1, 1, issue) && control.issued == 1 && queue.Usage().rejected == 1,
                "Backpressure called native code or was not counted.");
            control.ready = true;
            Expect(ticket.IsReady() && control.copies == 0, "Readiness must not map output.");
            lease = ticket.MapResult();
            Expect(control.copies == 1 && control.destroyed == 1 && queue.Usage().bytes == 16,
                "Completed copy did not release staging or used a repeated copy.");
            (void)ticket.MapResult();
            Expect(control.copies == 1, "Mapping copied the same output twice.");
            ticket.Cancel(); queue.Poll();
            Expect(!queue.Enqueue(1, 1, issue), "Live output lease did not apply backpressure.");
            queue.Close();
            Expect(lease.Bytes().size() == 16 && lease.Bytes()[3] == std::byte{51}, "Shutdown invalidated immutable output.");
        }
        Expect(lease.Bytes()[3] == std::byte{51}, "Queue destruction invalidated leased CPU output.");
        bool workerRead = false;
        std::thread writer([pixels = std::move(lease), &workerRead]() mutable {
            workerRead = pixels.Bytes().size() == 16 && pixels.Bytes()[3] == std::byte{51};
            pixels = {}; // last output owner can release its quota on the writer thread
        });
        writer.join();
        Expect(workerRead, "Writer thread lost immutable output after queue destruction.");
        ReadbackQueue queue;
        queue.SetLimits({8, 15});
        const auto beforeByteRejection = control.issued;
        Expect(!queue.Enqueue(8, 8, [&] { return std::make_unique<Fake>(control); })
            && control.issued == beforeByteRejection, "Byte quota issued native work before rejection.");
        queue.SetLimits({2, 32});
        control.ready = false;
        auto abandoned = queue.Enqueue(8, 8, [&] { return std::make_unique<Fake>(control); });
        const auto destroyed = control.destroyed;
        abandoned.Cancel(); queue.Poll();
        Expect(control.destroyed == destroyed && queue.Usage().requests == 1, "Cancellation freed an in-flight copy.");
        control.ready = true; queue.Poll();
        Expect(control.destroyed == destroyed + 1 && queue.Usage().bytes == 0, "Abandoned copy was not released on completion.");
        Reject([&] { (void)queue.Enqueue(8, 8, []() -> std::unique_ptr<ReadbackTransfer> { throw std::runtime_error("admission fault"); }); });
        Expect(queue.Usage().requests == 0 && queue.Usage().bytes == 0, "Native failure leaked admission.");
        Reject([&] { (void)queue.Enqueue(1, std::numeric_limits<std::uint64_t>::max(), [&] { return std::make_unique<Fake>(control); }); });
        Reject([&] { (void)ReadbackColorBytes(0, 1, 4); });
        Reject([&] { (void)ReadbackColorBytes(UINT32_MAX, UINT32_MAX, 4); });
        control.failPoll = true;
        auto failure = queue.Enqueue(4, 4, [&] { return std::make_unique<Fake>(control); });
        Reject([&] { (void)failure.IsReady(); });
        Expect(failure.Status() == ReadbackStatus::Failed, "Completion failure lost its status.");
        queue.Close();
        Expect(failure.Status() == ReadbackStatus::Cancelled && queue.Usage().requests == 0, "Shutdown left native pending state.");
        ReadbackQueue mapping;
        control.failPoll = false; control.failCopy = true;
        auto badMap = mapping.Enqueue(4, 4, [&] { return std::make_unique<Fake>(control); });
        Reject([&] { (void)badMap.MapResult(); });
        Expect(badMap.Status() == ReadbackStatus::Failed, "Mapping failure lost its status.");
        mapping.Close();
        Expect(!badMap.IsReady(), "Cancelled mapping remained usable.");
        std::cout << "Readback ticket/lease/quota/cancellation/failure/overflow PASS\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
