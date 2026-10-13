#include "Mods/Network/Gameplay/StatusEffects/FreezePredictionLedger.hpp"

#include <algorithm>

namespace MphRead::Mods::Network
{
    bool FreezePredictionLedger::IsValidSlot(std::int32_t slot) noexcept
    {
        return slot >= 0 && slot < static_cast<std::int32_t>(NetworkSlotCapacity);
    }

    void FreezePredictionLedger::NotePredicted(std::int32_t slot, std::uint32_t frame) noexcept
    {
        if (IsValidSlot(slot))
        {
            _predictedAt[static_cast<std::size_t>(slot)] = std::max(1U, frame);
        }
    }

    bool FreezePredictionLedger::HasPrediction(std::int32_t slot) const noexcept
    {
        return IsValidSlot(slot) && _predictedAt[static_cast<std::size_t>(slot)] != 0;
    }

    bool FreezePredictionLedger::Awaiting(
        std::int32_t slot, std::uint32_t currentFrame, std::uint32_t roundTripFrames) const noexcept
    {
        if (!IsValidSlot(slot))
        {
            return false;
        }
        return HasPrediction(slot)
            && currentFrame - _predictedAt[static_cast<std::size_t>(slot)] <= roundTripFrames + ArrivalSlackFrames;
    }

    void FreezePredictionLedger::ForgetSlot(std::int32_t slot) noexcept
    {
        if (IsValidSlot(slot))
        {
            _predictedAt[static_cast<std::size_t>(slot)] = 0;
        }
    }

    void FreezePredictionLedger::Reset() noexcept
    {
        _predictedAt = {};
    }
}
