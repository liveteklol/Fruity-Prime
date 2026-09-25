#include "Rooms.h"

#include "Enums.h"

#include <algorithm>
#include <cctype>

namespace fp {

namespace {

const RoomMetadata kRooms[] = {
#include "RoomTable.inc"
};

bool equalsIgnoreCase(std::string_view a, std::string_view b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
    });
}

} // namespace

std::string RoomMetadata::modelPath() const
{
    if (firstHunt || hybrid) {
        return std::string("levels/models/") + modelFile;
    }
    return std::string("_archives/") + archive + "/" + modelFile;
}

std::string RoomMetadata::texturePath() const
{
    if (textureFile == nullptr) {
        return modelPath();
    }
    return std::string("levels/textures/") + textureFile;
}

std::string RoomMetadata::animationPath() const
{
    if (animationFile == nullptr) {
        return {};
    }
    const std::string dir = firstHunt || hybrid ? std::string("levels/models/") : std::string("_archives/") + archive + "/";
    return dir + animationFile;
}

std::string RoomMetadata::collisionPath() const
{
    if (collisionFile == nullptr) {
        return {};
    }
    if (firstHunt || hybrid) {
        return std::string("levels/collision/") + collisionFile;
    }
    return std::string("_archives/") + archive + "/" + collisionFile;
}

std::string RoomMetadata::entityPath() const
{
    return entityFile == nullptr ? std::string() : std::string("levels/entities/") + entityFile;
}

std::string RoomMetadata::nodePath() const
{
    return nodeFile == nullptr ? std::string() : std::string("levels/nodeData/") + nodeFile;
}

std::span<const RoomMetadata> allRooms() { return kRooms; }

const RoomMetadata* findRoom(std::string_view nameOrInGameName)
{
    for (const RoomMetadata& room : kRooms) {
        if (equalsIgnoreCase(room.name, nameOrInGameName)) {
            return &room;
        }
    }
    // In-game names repeat (Combat Hall is UNIT1_RM4 and MP3): prefer the arena.
    const RoomMetadata* fallback = nullptr;
    for (const RoomMetadata& room : kRooms) {
        if (room.inGameName != nullptr && equalsIgnoreCase(room.inGameName, nameOrInGameName)) {
            if (room.multiplayer) {
                return &room;
            }
            fallback = fallback != nullptr ? fallback : &room;
        }
    }
    return fallback;
}

int multiplayerNodeLayerMask(int playerCount, bool captureTheFlag)
{
    int mask = NodeLayer::MultiplayerU;
    mask |= playerCount <= 2 ? NodeLayer::MultiplayerLod0 : NodeLayer::MultiplayerLod1;
    if (captureTheFlag) {
        mask |= NodeLayer::CaptureTheFlag;
    }
    return mask;
}

int multiplayerEntityLayer(GameMode mode, int playerCount)
{
    const int bySize = playerCount >= 4 ? 2 : playerCount == 3 ? 1 : 0;
    switch (mode) {
    case GameMode::Battle:
    case GameMode::PrimeHunter:
        return bySize;
    case GameMode::BattleTeams:
        return 3;
    case GameMode::Survival:
    case GameMode::SurvivalTeams:
        return 15;
    case GameMode::Capture:
        return 12;
    case GameMode::Bounty:
        return 8 + bySize;
    case GameMode::BountyTeams:
        return 11;
    case GameMode::Nodes:
        return 4 + bySize;
    case GameMode::NodesTeams:
        return 7;
    case GameMode::Defender:
    case GameMode::DefenderTeams:
        return 14;
    }
    return 0;
}

} // namespace fp
