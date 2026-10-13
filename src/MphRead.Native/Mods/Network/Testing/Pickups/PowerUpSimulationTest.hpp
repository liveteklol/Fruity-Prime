#pragma once

#include <cstdint>
#include <string>

namespace MphRead::Mods::Network
{
    // A power-up on a simulating authority: claimed by a client's player and
    // granted from the moment of contact, answered the same way when the
    // claim is sent again, refused to a second claimant and to claims that
    // are stale, too far or for another spawn; touched by a player the
    // authority sees itself; and published in the snapshot.
    class PowerUpSimulationTest final
    {
    public:
        PowerUpSimulationTest() = delete;

        // 0: passed; 1: failed; 2: the room puts out no power-up.
        [[nodiscard]] static std::int32_t Run(const std::string& room);
    };
}
