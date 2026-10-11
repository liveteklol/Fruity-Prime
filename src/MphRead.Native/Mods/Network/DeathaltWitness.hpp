#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace MphRead::Mods::Network
{
    // What the authority has seen of each player's Death Alt: the last frame
    // its own copy of them had it running. A hit claim says a kill was a
    // Death Alt's (HitClaimPacket::CauseDeathalt), which the victim's screen
    // names; a shooter's word for it is taken only when the authority's copy
    // had the power-up too.
    //
    // Items other than health are picked up on every machine, the
    // authority's copy a trip after its owner, so its Death Alt starts and
    // ends a trip late -- and the claim arrives a trip late as well. A
    // second's slack is the jitter between the two.
    class DeathaltWitness final
    {
    public:
        static constexpr std::size_t Slots = 8;
        static constexpr std::uint32_t SlackFrames = 60;

        void Observe(std::int32_t slot, bool running, std::uint32_t frame) noexcept
        {
            if (running && slot >= 0 && static_cast<std::size_t>(slot) < Slots)
            {
                _seen[static_cast<std::size_t>(slot)] = frame == 0 ? 1U : frame;
            }
        }

        [[nodiscard]] bool Vouches(std::int32_t slot, std::uint32_t frame) const noexcept
        {
            if (slot < 0 || static_cast<std::size_t>(slot) >= Slots)
            {
                return false;
            }
            const std::uint32_t seen = _seen[static_cast<std::size_t>(slot)];
            return seen != 0 && frame - seen <= SlackFrames;
        }

        // A new life, a new occupant: nothing seen of them yet.
        void Forget(std::int32_t slot) noexcept
        {
            if (slot >= 0 && static_cast<std::size_t>(slot) < Slots)
            {
                _seen[static_cast<std::size_t>(slot)] = 0;
            }
        }

    private:
        std::array<std::uint32_t, Slots> _seen{};
    };
}
