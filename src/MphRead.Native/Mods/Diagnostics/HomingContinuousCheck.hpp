#pragma once

#include <cstdint>
#include <string>

namespace MphRead::Mods::Diagnostics
{
    // -homingcontinuouscheck [ROOM]: the Shock Coil held on a real player in
    // a loaded room, its plain and its affinity row: the lock taken every
    // tick, the damage it does and the health it drains back; the lock
    // following a moving target; the narrower cone a morphing target gets;
    // and a target behind a wall, locked but shielded by it.
    class HomingContinuousCheck final
    {
    public:
        HomingContinuousCheck() = delete;

        [[nodiscard]] static std::int32_t Run(const std::string& room);
    };
}
