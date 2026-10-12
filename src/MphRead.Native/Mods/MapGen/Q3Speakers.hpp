#pragma once

#include <cstdint>

namespace MphRead::Mods::MapGen
{
    // A translated Quake 3 level's ambient sound: each target_speaker that
    // loops from the start (spawnflags LOOPED_ON) plays its sound where it
    // stands, as loud as Quake plays a loop (full within 80 units, silent
    // from 1330), the sound read from the level's archives.
    class Q3Speakers final
    {
    public:
        // Starts a room's loops once it is played (again after the game
        // stopped every sound, as it does between rooms).
        static void Ensure(std::int32_t roomId, std::uint64_t frame);

        Q3Speakers() = delete;
    };
}
