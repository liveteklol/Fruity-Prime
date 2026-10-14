#pragma once

#include <cstdint>
#include <string>

namespace MphRead::Mods::Diagnostics
{
    // -homingparitycheck [ROOM]: the game's own homing projectiles, spawned
    // and flown by the projectile code at 60 Hz in a loaded room, measured
    // against the same shots flown at the DS's 30 Hz -- path, heading, what
    // they hit first and when, and when a target behind is let go. Prints
    // the differences and fails on any beyond what a 60 Hz step can explain.
    class HomingParityCheck final
    {
    public:
        HomingParityCheck() = delete;

        [[nodiscard]] static std::int32_t Run(const std::string& room);
    };
}
