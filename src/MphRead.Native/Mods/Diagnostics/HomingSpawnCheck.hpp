#pragma once

#include <cstdint>
#include <string>

namespace MphRead::Mods::Diagnostics
{
    // -homingspawncheck: the homing weapons enemies, bosses and platforms
    // really fire. First a census of every story room's enemy spawners and
    // weapon platforms from the room data; then, in the rooms that hold the
    // weapon users, the real spawner, AI or platform is switched on in front
    // of a player kept alive, and every projectile it fires is recorded: its
    // weapon row, the charge it was spawned with, the steering strength that
    // charge gives, the target it locked and how far it turned in flight.
    class HomingSpawnCheck final
    {
    public:
        HomingSpawnCheck() = delete;

        [[nodiscard]] static std::int32_t Run(std::int32_t seconds);
    };
}
