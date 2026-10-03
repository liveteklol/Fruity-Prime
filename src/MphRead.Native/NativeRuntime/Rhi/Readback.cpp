#include "Readback.hpp"
#include <algorithm>
#include <exception>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace MphRead::NativeRuntime::Rhi
{
    namespace
    {
        struct Quota final
        {
            mutable std::mutex Mutex;
            ReadbackLimits Limits;
            ReadbackUsage Usage;
        };
        struct Charge final
        {
            std::shared_ptr<Quota> Owner;
            std::uint64_t Bytes = 0;
            ~Charge()
            {
                if (!Owner) return;
                std::lock_guard lock(Owner->Mutex);
                Owner->Usage.bytes -= Bytes;
                --Owner->Usage.requests;
            }
            void ReleaseStaging(std::uint64_t bytes)
            {
                std::lock_guard lock(Owner->Mutex);
                Owner->Usage.bytes -= bytes;
                Bytes -= bytes;
            }
        };
    }
    struct ReadbackPayload final
    {
        std::shared_ptr<Charge> Reservation;
        std::vector<std::byte> Data;
    };
    struct ReadbackState final
    {
        ReadbackStatus Status = ReadbackStatus::Pending;
        std::shared_ptr<Charge> Reservation;
        std::uint64_t OutputBytes = 0, StagingBytes = 0;
        std::unique_ptr<ReadbackTransfer> Transfer;
        std::shared_ptr<const ReadbackPayload> Payload;
        std::exception_ptr Failure;
        bool Poll()
        {
            if (Status == ReadbackStatus::Failed) std::rethrow_exception(Failure);
            if (Status == ReadbackStatus::Pending)
                try { if (Transfer->IsReady()) Status = ReadbackStatus::Ready; }
                catch (...) { Failure = std::current_exception(); Status = ReadbackStatus::Failed; throw; }
            return Status == ReadbackStatus::Ready || Status == ReadbackStatus::Complete;
        }
    };
    struct ReadbackQueue::Impl final
    {
        std::shared_ptr<Quota> Budget = std::make_shared<Quota>();
        std::vector<std::shared_ptr<ReadbackState>> Pending;
        bool Closed = false;
    };
    std::span<const std::byte> ReadbackResult::Bytes() const noexcept
    { return _payload ? std::span<const std::byte>(_payload->Data) : std::span<const std::byte>{}; }
    ReadbackStatus ReadbackTicket::Status() const noexcept
    { return _state ? _state->Status : ReadbackStatus::Empty; }
    bool ReadbackTicket::IsReady() { return _state && _state->Poll(); }
    ReadbackResult ReadbackTicket::MapResult()
    {
        if (!IsReady()) throw std::logic_error("Readback result is not ready (or its device was closed).");
        if (_state->Status == ReadbackStatus::Ready)
        {
            auto result = std::make_shared<ReadbackPayload>();
            result->Data.resize(static_cast<std::size_t>(_state->OutputBytes));
            result->Reservation = _state->Reservation;
            try { _state->Transfer->CopyResult(result->Data); }
            catch (...) { _state->Failure = std::current_exception(); _state->Status = ReadbackStatus::Failed; throw; }
            _state->Transfer.reset();
            _state->Reservation->ReleaseStaging(_state->StagingBytes);
            _state->Payload = std::move(result);
            _state->Status = ReadbackStatus::Complete;
        }
        return ReadbackResult(_state->Payload);
    }
    ReadbackQueue::ReadbackQueue() : _impl(std::make_unique<Impl>()) {}
    ReadbackQueue::~ReadbackQueue() { Close(); }
    ReadbackTicket ReadbackQueue::Enqueue(std::uint64_t outputBytes, std::uint64_t stagingBytes,
        const std::function<std::unique_ptr<ReadbackTransfer>()>& issue)
    {
        if (_impl->Closed) throw std::logic_error("Readback queue is closed.");
        if (!issue || !outputBytes || !stagingBytes || outputBytes > std::numeric_limits<std::size_t>::max()
            || stagingBytes > std::numeric_limits<std::uint64_t>::max() - outputBytes)
            throw std::invalid_argument("Invalid readback size or transfer factory.");
        Poll();
        auto charge = std::make_shared<Charge>();
        {
            std::lock_guard lock(_impl->Budget->Mutex);
            auto& used = _impl->Budget->Usage;
            const auto& limit = _impl->Budget->Limits;
            const auto bytes = outputBytes + stagingBytes;
            if (used.requests >= limit.maxRequests || bytes > limit.maxBytes
                || used.bytes > limit.maxBytes - bytes)
            { ++used.rejected; return {}; }
            charge->Owner = _impl->Budget; charge->Bytes = bytes;
            ++used.requests; used.bytes += bytes;
        }
        auto state = std::make_shared<ReadbackState>();
        state->Reservation = std::move(charge);
        state->OutputBytes = outputBytes; state->StagingBytes = stagingBytes;
        _impl->Pending.push_back(state); // bookkeeping before native submission
        try
        {
            state->Transfer = issue();
            if (!state->Transfer) throw std::logic_error("Readback factory returned no transfer.");
        }
        catch (...) { _impl->Pending.pop_back(); throw; }
        return ReadbackTicket(std::move(state));
    }
    void ReadbackQueue::Poll()
    {
        for (auto it = _impl->Pending.begin(); it != _impl->Pending.end();)
        {
            const auto& state = *it;
            // Keep even an abandoned copy until its real completion. A failed
            // transfer stays owned until the explicit shutdown/loss boundary.
            if (state->Status == ReadbackStatus::Complete
                || (state->Status != ReadbackStatus::Failed && state->Poll() && state.use_count() == 1))
                it = _impl->Pending.erase(it);
            else ++it;
        }
    }
    void ReadbackQueue::SetLimits(ReadbackLimits limits)
    {
        if (!limits.maxRequests || !limits.maxBytes) throw std::invalid_argument("Readback limits must be positive.");
        std::lock_guard lock(_impl->Budget->Mutex);
        _impl->Budget->Limits = limits;
    }
    ReadbackUsage ReadbackQueue::Usage() const
    { std::lock_guard lock(_impl->Budget->Mutex); return _impl->Budget->Usage; }
    void ReadbackQueue::Close() noexcept
    {
        if (_impl->Closed) return;
        _impl->Closed = true;
        for (const auto& state : _impl->Pending)
        {
            state->Transfer.reset();
            if (state->Status != ReadbackStatus::Complete)
            {
                state->Status = ReadbackStatus::Cancelled;
                state->Reservation.reset();
            }
        }
        _impl->Pending.clear();
    }
    std::uint64_t ReadbackColorBytes(std::uint32_t width, std::uint32_t height, std::uint32_t pixelBytes)
    {
        if (!width || !height || !pixelBytes) throw std::invalid_argument("Empty readback extent.");
        const auto pixels = static_cast<std::uint64_t>(width) * height;
        if (pixels > std::numeric_limits<std::size_t>::max() / pixelBytes)
            throw std::overflow_error("Readback extent overflows host address space.");
        return pixels * pixelBytes;
    }
}
