#include "MatchRoom.h"

#include "NetGame.h"
#include "SceneBuilder.h"
#include "formats/Entities.h"

#include <cstdio>

namespace fp {

std::string matchNodePath(const RoomMetadata& room, GameMode mode)
{
    std::string nodePath = room.nodePath();
    static const std::pair<int, const char*> ctfNodes[] = {{93, "mp1_CTF_node.bin"}, {99, "mp6_CTF_node.bin"}, {101, "mp8_CTF_node.bin"},
        {102, "mp9_CTF_node.bin"}, {105, "mp12_CTF_node.bin"}, {107, "mp14_CTF_node.bin"}, {108, "ctf1_CTF_node.bin"},
        {119, "e3Level_CTF_Node.bin"}};
    for (const auto& [id, file] : ctfNodes) {
        if (mode == GameMode::Capture && room.id == id) {
            nodePath = std::string("levels/nodeData/") + file;
        }
    }
    if (room.id == 107
        && (mode == GameMode::Nodes || mode == GameMode::NodesTeams || mode == GameMode::Defender || mode == GameMode::DefenderTeams)) {
        nodePath = "levels/nodeData/mp14_KOTH_node.bin";
    }
    return nodePath;
}

std::optional<MatchRoom> loadMatchRoom(const std::filesystem::path& root, const RoomMetadata& room, GameMode mode, int players,
    int hunter, bool networked)
try {
    Model model = [&]() -> Model {
        const std::filesystem::path modelFile = resolveCaseInsensitive(root, room.modelPath());
        const std::filesystem::path textureFile = resolveCaseInsensitive(root, room.texturePath());
        std::filesystem::path animationFile;
        if (const std::string anim = room.animationPath(); !anim.empty()) {
            animationFile = resolveCaseInsensitive(root, anim);
            if (!std::filesystem::exists(animationFile)) {
                animationFile.clear();
            }
        }
        return Model::loadRoom(modelFile, textureFile, animationFile);
    }();
    // SceneSetup.GetNodeLayer and GetMultiplayerEntityLayer: by player count.
    const int nodeLayerMask = room.multiplayer ? multiplayerNodeLayerMask(players, mode == GameMode::Capture)
                                               : (room.nodeLayer > 0 ? ((1 << room.nodeLayer) & 0xFF) << 6 : 0);
    model.filterNodes(nodeLayerMask);
    std::vector<Entity> entities;
    if (!room.entityPath().empty()) {
        entities = loadEntities(resolveCaseInsensitive(root, room.entityPath()), room.multiplayer ? multiplayerEntityLayer(mode, players) : 0);
    }
    std::unique_ptr<RoomCollision> collision;
    if (const std::string colPath = room.collisionPath(); !colPath.empty()) {
        collision = std::make_unique<RoomCollision>(RoomCollision::load(resolveCaseInsensitive(root, colPath), nodeLayerMask));
    }
    MatchRoom out;
    const std::vector<PlayerSpawn> spawns = playerSpawns(entities);
    if (!spawns.empty()) {
        out.spawn = spawns.front();
    }
    out.scene = buildScene(root, room, std::move(model), entities);
    if (networked) {
        NetGame::preloadModels(*out.scene, root);
    }
    out.world = std::make_unique<World>(*out.scene, root, std::move(collision), entities, hunter);
    out.world->setNetworked(networked);
    out.world->setMode(mode);
    if (const std::string nodePath = matchNodePath(room, mode); !nodePath.empty()) {
        if (const std::filesystem::path file = resolveCaseInsensitive(root, nodePath); std::filesystem::exists(file)) {
            out.world->loadNodeData(file);
        }
    }
    if (out.spawn) {
        out.world->spawnPlayer(out.spawn->position, out.spawn->facing);
    }
    return out;
} catch (const std::exception& e) {
    std::fprintf(stderr, "Cannot load %s: %s\n", room.name, e.what());
    return std::nullopt;
}

} // namespace fp
