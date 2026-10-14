#pragma once

#include <cstdint>
#include <string>

namespace MphRead::Mods::Diagnostics
{
    // -homingdisplaycheck [ROOM]: another player's shot as this machine draws
    // it, at 60, 144, 240 and 540 pictures a second through the game's own
    // fixed-step clock, with the confirmed-impact correction and the aim
    // retarget each on and off. Fails if a picture moves the physical shot,
    // if the drawn flight depends on the frame rate, or if a correction stops
    // doing what it is for.
    class HomingDisplayCheck final
    {
    public:
        HomingDisplayCheck() = delete;

        [[nodiscard]] static std::int32_t Run(const std::string& room);
    };
}
