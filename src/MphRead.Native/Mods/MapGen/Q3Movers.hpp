#pragma once

#include "../../Formats/Types.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace MphRead
{
    class Node;
}

namespace MphRead::Mods::MapGen
{
    class Q3Bsp;

    // A Quake 3 level's movers, moved as Quake moves them (g_mover.c): a
    // func_bobbing up and down its axis, a func_rotating round its origin, a
    // func_pendulum swinging, a func_door opening as someone comes near and
    // closing after its wait. Each brush model is drawn in a node of the
    // room of its own, q3model<N> (MapPacker); this sets where it is.
    class Q3Movers final
    {
    public:
        // Whether a level's brush model moves: a func_bobbing, func_rotating
        // or func_pendulum, or a func_door that opens by itself (no
        // targetname to wait for, no health to be shot open).
        [[nodiscard]] static bool IsMover(const Q3Bsp* bsp, std::int32_t model);

        // Whether a mover's brush model is stored about its entity's origin
        // (q3map moves one with an origin there) rather than where it stands.
        [[nodiscard]] static bool IsRelative(const Q3Bsp* bsp, std::int32_t model);

        // Brings a translated room's movers to this frame (60 a second), the
        // doors opened by whoever stands at them (positions in this game's
        // space); once a frame, whoever asks first -- the movers' collision
        // (Q3MoverEntity), which a server runs too, or the room drawing them.
        static void Update(
            std::int32_t roomId,
            std::uint64_t frame,
            const std::vector<OpenTK::Mathematics::Vector3>& players);

        // Where a mover's brush model is now, as a transform of this game's
        // space; nothing for one that does not move.
        [[nodiscard]] static std::optional<OpenTK::Mathematics::Matrix4> Motion(
            std::int32_t roomId, std::int32_t model);

        // Moves a room's mover nodes (q3model<N>) to where they are.
        static void Apply(
            std::int32_t roomId,
            const std::vector<std::shared_ptr<MphRead::Node>>& nodes);

        Q3Movers() = delete;
    };
}
