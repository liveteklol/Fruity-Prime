#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>

namespace MphRead::NativeRuntime::Rhi
{
    enum class ReadbackStatus : std::uint8_t { Empty, Pending, Ready, Complete, Cancelled, Failed };
    struct ReadbackLimits final
    {
        std::uint32_t maxRequests = 8;
        std::uint64_t maxBytes = 64ULL << 20;
    };
    struct ReadbackUsage final
    {
        std::uint32_t requests = 0;
        std::uint64_t bytes = 0;
        std::uint64_t rejected = 0;
    };
    // Backends submit a copy and supply a zero-time completion poll. Copy is
    // called only after completion. Destruction must not wait in steady state.
    class ReadbackTransfer
    {
    public:
        virtual ~ReadbackTransfer() = default;
        virtual bool IsReady() = 0;
        virtual void CopyResult(std::span<std::byte> destination) = 0;
    };
    struct ReadbackState;
    struct ReadbackPayload;
    // Immutable CPU output lease; may cross threads and survive device shutdown.
    // The lease continues to charge the request quota until its last owner ends.
    class ReadbackResult final
    {
    public:
        [[nodiscard]] std::span<const std::byte> Bytes() const noexcept;
        [[nodiscard]] explicit operator bool() const noexcept { return bool(_payload); }
    private:
        friend class ReadbackTicket;
        explicit ReadbackResult(std::shared_ptr<const ReadbackPayload> payload) : _payload(std::move(payload)) {}
        std::shared_ptr<const ReadbackPayload> _payload;
    public:
        ReadbackResult() = default;
    };
    // Ticket/queue operations run on the device thread. A false ticket denotes
    // backpressure, never a synchronous fallback. Cancel drops the consumer;
    // the queue keeps in-flight native storage until real GPU completion.
    class ReadbackTicket final
    {
    public:
        ReadbackTicket() = default;
        [[nodiscard]] explicit operator bool() const noexcept { return bool(_state); }
        [[nodiscard]] ReadbackStatus Status() const noexcept;
        bool IsReady();
        [[nodiscard]] ReadbackResult MapResult();
        void Cancel() noexcept { _state.reset(); }
    private:
        friend class ReadbackQueue;
        explicit ReadbackTicket(std::shared_ptr<ReadbackState> state) : _state(std::move(state)) {}
        std::shared_ptr<ReadbackState> _state;
    };
    class ReadbackQueue final
    {
    public:
        ReadbackQueue();
        ~ReadbackQueue();
        ReadbackQueue(const ReadbackQueue&) = delete;
        ReadbackQueue& operator=(const ReadbackQueue&) = delete;
        // Reserve staging + immutable CPU output before invoking native code.
        // Rejection leaves the GPU and source resource untouched.
        [[nodiscard]] ReadbackTicket Enqueue(std::uint64_t outputBytes, std::uint64_t stagingBytes,
            const std::function<std::unique_ptr<ReadbackTransfer>()>& issue);
        void Poll();
        void SetLimits(ReadbackLimits limits);
        [[nodiscard]] ReadbackUsage Usage() const;
        // Owner establishes GPU idle/loss before Close. Pending/unmapped tickets
        // become Cancelled; already copied CPU leases remain valid.
        void Close() noexcept;
    private:
        struct Impl;
        std::unique_ptr<Impl> _impl;
    };
    [[nodiscard]] std::uint64_t ReadbackColorBytes(std::uint32_t width, std::uint32_t height,
        std::uint32_t pixelBytes);
}
