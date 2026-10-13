#pragma once

#include "Mods/Network/Domain/NetworkCapacity.hpp"

#include <array>
#include <cstdint>

namespace MphRead::Mods::Network
{
    enum class HitClaimSlotTransition : std::uint8_t
    {
        None,
        Died,
        Respawned
    };

    // Owns the death boundary used to reject claims fired after the shooter died.
    class HitClaimDeathState final
    {
    public:
        static constexpr std::size_t SlotCapacity = NetworkSlotCapacity;

        [[nodiscard]] HitClaimSlotTransition UpdateInPlay(std::int32_t slot, bool inPlay,
            std::uint32_t authorityFrame) noexcept;
        [[nodiscard]] bool FiredAfterOwnDeath(std::int32_t slot, std::uint32_t firedFrame) const noexcept;
        [[nodiscard]] bool WasDead(std::int32_t slot) const noexcept;
        void ForgetSlot(std::int32_t slot) noexcept;
        void Reset() noexcept;

    private:
        [[nodiscard]] static bool IsValidSlot(std::int32_t slot) noexcept;

        std::array<bool, SlotCapacity> _dead{};
        std::array<std::uint32_t, SlotCapacity> _deathFrame{};
        std::array<bool, SlotCapacity> _wasInPlay{};
    };
}
