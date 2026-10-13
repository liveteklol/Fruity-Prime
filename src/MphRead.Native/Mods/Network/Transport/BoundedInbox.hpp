#pragma once

#include "Mods/Network/Transport/InboxShedding.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>

namespace MphRead::Mods::Network
{
    enum class InboxEnqueueResult : std::uint8_t
    {
        Enqueued,
        ReplacedSuperseded,
        RejectedSuperseded,
        RejectedCriticalOverflow
    };

    struct BoundedInboxStatistics final
    {
        std::size_t Depth = 0;
        std::size_t Capacity = 0;
        std::int64_t ExistingSupersededDrops = 0;
        std::int64_t IncomingSupersededDrops = 0;
        std::int64_t CriticalOverflowDrops = 0;
    };

    // Owns inbox capacity, packet ordering, and overflow accounting. Callers
    // use the same operation for socket, delayed-arrival, and replay ingress.
    template <typename Packet>
    class BoundedInbox final
    {
    public:
        explicit BoundedInbox(std::size_t capacity) noexcept
            : _capacity(capacity)
        {
        }

        BoundedInbox(const BoundedInbox&) = delete;
        BoundedInbox(BoundedInbox&&) = delete;
        BoundedInbox& operator=(const BoundedInbox&) = delete;
        BoundedInbox& operator=(BoundedInbox&&) = delete;

        template <typename PacketValue, typename TypeOf>
        [[nodiscard]] InboxEnqueueResult TryEnqueue(PacketValue&& packet, TypeOf&& typeOf)
        {
            std::lock_guard lock(_lock);
            if (_packets.size() < _capacity)
            {
                _packets.push_back(std::forward<PacketValue>(packet));
                return InboxEnqueueResult::Enqueued;
            }

            const auto victim = ShedIndex(_packets, typeOf);
            if (victim.has_value())
            {
                _packets.erase(_packets.begin() + static_cast<std::ptrdiff_t>(*victim));
                _packets.push_back(std::forward<PacketValue>(packet));
                ++_existingSupersededDrops;
                return InboxEnqueueResult::ReplacedSuperseded;
            }

            if (Superseded(typeOf(packet)))
            {
                ++_incomingSupersededDrops;
                return InboxEnqueueResult::RejectedSuperseded;
            }

            ++_criticalOverflowDrops;
            return InboxEnqueueResult::RejectedCriticalOverflow;
        }

        [[nodiscard]] bool TryDequeue(std::optional<Packet>& packet)
        {
            std::lock_guard lock(_lock);
            if (_packets.empty())
            {
                packet.reset();
                return false;
            }
            packet.emplace(std::move(_packets.front()));
            _packets.pop_front();
            return true;
        }

        [[nodiscard]] BoundedInboxStatistics Statistics() const
        {
            std::lock_guard lock(_lock);
            return {
                _packets.size(), _capacity, _existingSupersededDrops,
                _incomingSupersededDrops, _criticalOverflowDrops
            };
        }

    private:
        const std::size_t _capacity;
        mutable std::mutex _lock;
        std::deque<Packet> _packets;
        std::int64_t _existingSupersededDrops = 0;
        std::int64_t _incomingSupersededDrops = 0;
        std::int64_t _criticalOverflowDrops = 0;
    };
}
