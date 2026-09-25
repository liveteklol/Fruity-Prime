#pragma once

#include "World.h"
#include "formats/Rooms.h"
#include "render/Scene.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace fp {

// A room made ready for a match: SceneSetup's room, entities, collision and
// node data for `mode` and `players` (the layers go by the player count), and
// the World that plays it with `hunter` as the main player.
struct MatchRoom {
    std::unique_ptr<Scene> scene;
    std::unique_ptr<World> world;
    std::optional<PlayerSpawn> spawn; // the first player spawn, if the room has one
};

// SceneSetup.LoadNodeData's file: Capture's own paths in eight rooms (the C#
// misspells two of them ".bi)", so it finds none for Data Shrine and Head
// Shot), Outer Reach's for Nodes and Defender, else the room's.
std::string matchNodePath(const RoomMetadata& room, GameMode mode);

// Loads it all; nullopt (with the reason on stderr) when the room cannot be loaded.
// `networked`: a match on a server (World::setNetworked, every hunter's models preloaded).
std::optional<MatchRoom> loadMatchRoom(const std::filesystem::path& root, const RoomMetadata& room, GameMode mode, int players,
    int hunter, bool networked);

} // namespace fp
