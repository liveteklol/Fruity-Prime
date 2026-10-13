#include "Mods/Network/Gameplay/Claims/Hit/HitClaimDeathState.hpp"

namespace MphRead::Mods::Network
{
    HitClaimSlotTransition HitClaimDeathState::UpdateInPlay(std::int32_t slot, bool inPlay,
        std::uint32_t authorityFrame) noexcept
    {
        if (!IsValidSlot(slot))
        {
            return HitClaimSlotTransition::None;
        }
        const auto index = static_cast<std::size_t>(slot);
        HitClaimSlotTransition transition = HitClaimSlotTransition::None;
        if (_wasInPlay[index] && !inPlay)
        {
            _dead[index] = true;
            _deathFrame[index] = authorityFrame;
            transition = HitClaimSlotTransition::Died;
        }
        else if (!_wasInPlay[index] && inPlay)
        {
            _dead[index] = false;
            _deathFrame[index] = 0;
            transition = HitClaimSlotTransition::Respawned;
        }
        _wasInPlay[index] = inPlay;
        return transition;
    }

    bool HitClaimDeathState::FiredAfterOwnDeath(std::int32_t slot, std::uint32_t firedFrame) const noexcept
    {
        if (!IsValidSlot(slot))
        {
            return false;
        }
        const auto index = static_cast<std::size_t>(slot);
        return _dead[index] && _deathFrame[index] != 0 && firedFrame >= _deathFrame[index];
    }

    bool HitClaimDeathState::WasDead(std::int32_t slot) const noexcept
    {
        return IsValidSlot(slot) && _dead[static_cast<std::size_t>(slot)];
    }

    void HitClaimDeathState::ForgetSlot(std::int32_t slot) noexcept
    {
        if (!IsValidSlot(slot))
        {
            return;
        }
        const auto index = static_cast<std::size_t>(slot);
        _dead[index] = false;
        _deathFrame[index] = 0;
        _wasInPlay[index] = false;
    }

    void HitClaimDeathState::Reset() noexcept
    {
        _dead.fill(false);
        _deathFrame.fill(0);
        _wasInPlay.fill(false);
    }

    bool HitClaimDeathState::IsValidSlot(std::int32_t slot) noexcept
    {
        return slot >= 0 && static_cast<std::size_t>(slot) < SlotCapacity;
    }
}
