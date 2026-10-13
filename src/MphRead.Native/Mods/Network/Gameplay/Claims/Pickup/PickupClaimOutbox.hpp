#pragma once

#include "Mods/Network/Protocol/Packets/ClaimPackets.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace MphRead::Mods::Network
{
    // Owns only client-side pickup claims awaiting an authority verdict.
    class PickupClaimOutbox final
    {
    public:
        static constexpr std::size_t Capacity = 8;
        static constexpr std::int32_t MaxSends = 6;
        static constexpr std::int32_t MaxAge = 120;

        [[nodiscard]] std::uint16_t Enqueue(PickupClaimPacket claim) noexcept;
        [[nodiscard]] std::int32_t Compose(std::span<std::uint8_t> dest, std::int32_t resendInterval);
        [[nodiscard]] bool Resolve(std::uint16_t claimId) noexcept;
        template <typename OnExpired>
        void Tick(const OnExpired& onExpired)
        {
            for (Entry& entry : _entries)
            {
                if (!entry.Live)
                {
                    continue;
                }
                entry.Age++;
                if (entry.Age > MaxAge)
                {
                    const std::uint16_t claimId = entry.Claim.ClaimId;
                    entry.Live = false;
                    _unanswered++;
                    onExpired(claimId);
                }
            }
        }

        void Reset() noexcept;
        [[nodiscard]] std::int64_t Declared() const noexcept { return _declared; }
        [[nodiscard]] std::int64_t Unanswered() const noexcept { return _unanswered; }
        [[nodiscard]] std::size_t PendingCount() const noexcept;

    private:
        struct Entry final
        {
            bool Live = false;
            PickupClaimPacket Claim{};
            std::int32_t Age = 0;
            std::int32_t Sends = 0;
            std::int32_t LastSentAge = 0;
        };

        std::array<Entry, Capacity> _entries{};
        std::uint16_t _nextId = 0;
        std::int64_t _declared = 0;
        std::int64_t _unanswered = 0;
    };
}
