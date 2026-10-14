#pragma once

#include "Mods/Network/Gameplay/Player/PlayerCombatPolicy.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace MphRead::Mods
{
    struct SpectatorSlotSnapshot final
    {
        bool Exists = false;
        bool Active = false;
        bool Spawned = false;
        bool Spectating = false;
        bool RosterOccupied = false;
        std::int32_t Health = 0;
        std::uint16_t Generation = 0;
        std::uint16_t LifeId = 0;
    };

    struct SpectatorTargetIdentity final
    {
        std::int32_t Slot = -1;
        std::uint16_t Generation = 0;
        std::uint16_t LifeId = 0;
    };

    class SpectatorTargetPolicy final
    {
    public:
        SpectatorTargetPolicy() = delete;

        [[nodiscard]] static constexpr bool IsEligible(const SpectatorSlotSnapshot& slot,
            std::int32_t index, std::int32_t localSlot, bool networked) noexcept
        {
            return index != localSlot && slot.Exists
                && Network::PlayerCombatPolicy::IsInPlay(
                    slot.Active, slot.Spawned, slot.Health, slot.Spectating)
                && (!networked || (slot.RosterOccupied && slot.Generation != 0 && slot.LifeId != 0));
        }

        [[nodiscard]] static constexpr bool Matches(const SpectatorTargetIdentity& target,
            const SpectatorSlotSnapshot& slot, std::int32_t index, std::int32_t localSlot,
            bool networked) noexcept
        {
            return target.Slot == index && IsEligible(slot, index, localSlot, networked)
                && target.Generation == slot.Generation && target.LifeId == slot.LifeId;
        }

        [[nodiscard]] static std::int32_t Find(std::span<const SpectatorSlotSnapshot> candidates,
            std::int32_t fromSlot, std::int32_t localSlot, bool networked, bool forward) noexcept
        {
            if (candidates.empty())
            {
                return -1;
            }

            const std::int32_t count = static_cast<std::int32_t>(candidates.size());
            const bool hasOrigin = fromSlot >= 0 && fromSlot < count;
            const std::int32_t origin = hasOrigin
                ? fromSlot : (forward ? count - 1 : 0);
            for (std::int32_t offset = 1; offset <= count; ++offset)
            {
                std::int32_t index = forward ? origin + offset : origin - offset;
                if (index >= count)
                {
                    index -= count;
                }
                else if (index < 0)
                {
                    index += count;
                }
                if (hasOrigin && index == origin)
                {
                    continue;
                }
                if (IsEligible(candidates[static_cast<std::size_t>(index)], index, localSlot, networked))
                {
                    return index;
                }
            }
            return -1;
        }
    };
}
