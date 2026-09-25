#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace fp {

struct Rgb5 {
    uint8_t r, g, b;
    std::array<float, 3> toFloat() const { return {r / 31.0f, g / 31.0f, b / 31.0f}; }
};

struct RoomMetadata {
    int id;
    const char* name;
    const char* inGameName;
    const char* archive;
    const char* modelFile;
    const char* animationFile;
    const char* collisionFile;
    const char* textureFile;
    const char* entityFile;
    int nodeLayer;
    bool fogEnabled;
    Rgb5 fogColor;
    int fogSlope;
    int fogOffset;
    Rgb5 light1Color;
    std::array<float, 3> light1Vector;
    Rgb5 light2Color;
    std::array<float, 3> light2Vector;
    bool multiplayer;
    bool firstHunt;
    bool hybrid;
    float killHeight;
    std::array<float, 3> playerMin, playerMax; // multiplayer bounds, when hasLimits
    bool hasLimits;
    const char* nodeFile; // levels/nodeData, the bots' paths; nullptr when the room has none

    // Relative to the game's file system root, with forward slashes.
    std::string modelPath() const;
    std::string texturePath() const;
    std::string animationPath() const;
    std::string collisionPath() const;
    std::string entityPath() const; // empty when the room has none
    std::string nodePath() const;   // empty when the room has none
};

std::span<const RoomMetadata> allRooms();
const RoomMetadata* findRoom(std::string_view nameOrInGameName);

// The node layer mask a match uses. Ported from SceneSetup.GetNodeLayer.
int multiplayerNodeLayerMask(int playerCount, bool captureTheFlag);

enum class GameMode { Battle, BattleTeams, Survival, SurvivalTeams, Capture, Bounty, BountyTeams, Nodes, NodesTeams, Defender, DefenderTeams, PrimeHunter };

// The entity layer a match loads. Ported from Metadata.GetMultiplayerEntityLayer.
int multiplayerEntityLayer(GameMode mode, int playerCount);

} // namespace fp
