#include "Mods/Network/Server/Core/ServerSlotIdentity.hpp"

#include "Mods/Network/Session/NetLifecycleTracker.hpp"

namespace MphRead::Mods::Network
{
    void ServerSlotIdentity::EnsureGeneration(std::size_t slot) noexcept
    {
        if (IsValidSlot(slot) && _generations[slot] == 0)
        {
            _generations[slot] = 1;
        }
    }

    std::uint16_t ServerSlotIdentity::AdvanceGeneration(std::size_t slot) noexcept
    {
        if (!IsValidSlot(slot))
        {
            return 0;
        }
        _generations[slot] = NetLifecycleTracker::Next(_generations[slot]);
        _lives[slot] = 0;
        return _generations[slot];
    }

    std::uint16_t ServerSlotIdentity::Generation(std::size_t slot) const noexcept
    {
        return IsValidSlot(slot) ? _generations[slot] : 0;
    }

    std::uint16_t ServerSlotIdentity::Life(std::size_t slot) const noexcept
    {
        return IsValidSlot(slot) ? _lives[slot] : 0;
    }

    bool ServerSlotIdentity::IsValidSlot(std::size_t slot) const noexcept
    {
        return slot < SlotCapacity;
    }

    bool ServerSlotIdentity::MatchesGeneration(std::size_t slot, std::uint16_t generation) const noexcept
    {
        return IsValidSlot(slot) && _generations[slot] == generation;
    }

    void ServerSlotIdentity::SetLife(std::size_t slot, std::uint16_t life) noexcept
    {
        if (IsValidSlot(slot))
        {
            _lives[slot] = life;
        }
    }

    void ServerSlotIdentity::ResetLife(std::size_t slot) noexcept
    {
        SetLife(slot, 0);
    }

    void ServerSlotIdentity::ResetLives() noexcept
    {
        _lives.fill(0);
    }
}
