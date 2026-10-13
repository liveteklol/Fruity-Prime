#pragma once

#include "Mods/Network/Protocol/Packets/ClaimPackets.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace MphRead::Mods::Network
{
    // Remembers authority verdicts so resending one PickupClaim can never
    // apply a second item grant.
    class PickupClaimReplayLedger final
    {
    public:
        static constexpr std::size_t SlotCapacity = 8;
        static constexpr std::size_t CapacityPerSlot = 16;

        [[nodiscard]] const PickupVerdictPacket::Entry* Find(
            std::int32_t slot, std::uint16_t claimId) const noexcept;
        void Remember(std::int32_t slot, PickupVerdictPacket::Entry verdict) noexcept;
        void Reset() noexcept;

    private:
        struct Remembered final
        {
            std::uint16_t ClaimId = 0;
            PickupVerdictPacket::Entry Verdict{};
        };

        std::array<std::array<Remembered, CapacityPerSlot>, SlotCapacity> _remembered{};
    };
}
