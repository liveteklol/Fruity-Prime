#include "Mods/Network/Gameplay/Claims/Pickup/PickupClaimReplayLedger.hpp"

namespace MphRead::Mods::Network
{
    const PickupVerdictPacket::Entry* PickupClaimReplayLedger::Find(
        std::int32_t slot, std::uint16_t claimId) const noexcept
    {
        if (slot < 0 || slot >= static_cast<std::int32_t>(SlotCapacity) || claimId == 0)
        {
            return nullptr;
        }

        const Remembered& remembered = _remembered[static_cast<std::size_t>(slot)][claimId % CapacityPerSlot];
        return remembered.ClaimId == claimId ? &remembered.Verdict : nullptr;
    }

    void PickupClaimReplayLedger::Remember(std::int32_t slot, PickupVerdictPacket::Entry verdict) noexcept
    {
        if (slot < 0 || slot >= static_cast<std::int32_t>(SlotCapacity) || verdict.ClaimId == 0)
        {
            return;
        }

        _remembered[static_cast<std::size_t>(slot)][verdict.ClaimId % CapacityPerSlot]
            = Remembered{ verdict.ClaimId, verdict };
    }

    void PickupClaimReplayLedger::Reset() noexcept
    {
        _remembered = {};
    }
}
