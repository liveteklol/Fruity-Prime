#pragma once

#include "Mods/Network/Domain/NetworkCapacity.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace MphRead::Mods::Network
{
    // Owns the server-side generation and life identity associated with each wire slot.
    class ServerSlotIdentity final
    {
    public:
        static constexpr std::size_t SlotCapacity = NetworkSlotCapacity;

        void EnsureGeneration(std::size_t slot) noexcept;
        [[nodiscard]] std::uint16_t AdvanceGeneration(std::size_t slot) noexcept;
        [[nodiscard]] std::uint16_t Generation(std::size_t slot) const noexcept;
        [[nodiscard]] std::uint16_t Life(std::size_t slot) const noexcept;
        [[nodiscard]] bool IsValidSlot(std::size_t slot) const noexcept;
        [[nodiscard]] bool MatchesGeneration(std::size_t slot, std::uint16_t generation) const noexcept;
        void SetLife(std::size_t slot, std::uint16_t life) noexcept;
        void ResetLife(std::size_t slot) noexcept;
        void ResetLives() noexcept;

    private:
        std::array<std::uint16_t, SlotCapacity> _generations{};
        std::array<std::uint16_t, SlotCapacity> _lives{};
    };
}
