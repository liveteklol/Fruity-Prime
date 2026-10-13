#pragma once

#include "Mods/Network/Domain/ShotKey.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace MphRead::Mods::Network
{
    // Tracks authority hits already made real by a shooter's claim so that
    // the authority's later copy of the same shot cannot apply them twice.
    class RescuedHitLedger final
    {
    public:
        static constexpr std::size_t Capacity = 32;
        static constexpr std::uint32_t MaxAgeFrames = 720;

        void Record(std::uint8_t attacker, std::uint8_t victim, ShotKey key,
            std::uint16_t victimGeneration, std::uint16_t victimLife, std::uint32_t frame) noexcept;
        [[nodiscard]] bool Consume(ShotKey key, std::uint8_t victim,
            std::uint16_t victimGeneration, std::uint16_t victimLife, std::uint32_t frame) noexcept;
        void ForgetSlot(std::uint8_t slot, bool preserveAttackerFlights) noexcept;
        void Clear() noexcept;
        void Reset() noexcept;

    private:
        struct Entry final
        {
            std::uint8_t Attacker = 0;
            std::uint8_t Victim = 0;
            ShotKey Key{};
            std::uint16_t VictimGeneration = 0;
            std::uint16_t VictimLife = 0;
            std::uint32_t RecordedAt = 0;
            std::uint32_t Owed = 0;
        };

        std::array<Entry, Capacity> _entries{};
        std::size_t _next = 0;
    };
}
