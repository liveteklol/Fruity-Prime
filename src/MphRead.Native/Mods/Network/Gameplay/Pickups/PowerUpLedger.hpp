#pragma once

#include "Mods/Network/Domain/PowerUps.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace MphRead::Mods::Network
{
    // The authority's record of every power-up it granted: when each began
    // (the world frame of the contact, not of the moment it was judged) and
    // when it ends. What a client's word about a power-up is held against --
    // a doubled hit, a Death Alt kill -- at the frame that word is about.
    class PowerUpLedger final
    {
    public:
        static constexpr std::size_t Slots = 8;
        // Grants remembered per player and power-up, for hits judged late.
        static constexpr std::size_t History = 4;

        struct Grant
        {
            // Never 0 for a grant: 0 is "nothing granted yet".
            std::uint8_t Sequence = 0;
            std::uint32_t Start = 0;
            std::uint32_t Expire = 0;
            // The life it was granted in: a death ends it, and no later life
            // of the same player has it.
            std::uint16_t Life = 0;

            [[nodiscard]] constexpr bool RunningAt(std::uint32_t frame) const noexcept
            {
                return Sequence != 0 && Start <= frame && frame < Expire;
            }
        };

        // A new grant from `start` for `duration` frames in life `life`,
        // replacing any running.
        Grant Begin(std::int32_t slot, PowerUp kind, std::uint32_t start, std::uint16_t duration,
            std::uint16_t life) noexcept;
        // The running grant ended early, at `frame` (a death).
        void End(std::int32_t slot, PowerUp kind, std::uint32_t frame) noexcept;
        // Every grant still running for the player ends at `frame`: a new life.
        void EndAll(std::int32_t slot, std::uint32_t frame) noexcept;
        [[nodiscard]] Grant Current(std::int32_t slot, PowerUp kind) const noexcept;
        // Whether it was running at `frame`: from its start, up to and not
        // including its end. Exact -- a claim's late arrival is absorbed by
        // how long the claim is held, not by stretching the grant.
        [[nodiscard]] bool RanAt(std::int32_t slot, PowerUp kind, std::uint32_t frame, std::uint16_t life) const noexcept;
        // A new occupant: nothing granted to them yet.
        void Forget(std::int32_t slot) noexcept;
        void Reset() noexcept;

    private:
        struct Record
        {
            std::array<Grant, History> Grants{};
            std::size_t Newest = 0;
            std::uint8_t Sequence = 0;
        };

        [[nodiscard]] static bool Valid(std::int32_t slot) noexcept
        {
            return slot >= 0 && static_cast<std::size_t>(slot) < Slots;
        }

        std::array<std::array<Record, PowerUpCount>, Slots> _records{};
    };
}
