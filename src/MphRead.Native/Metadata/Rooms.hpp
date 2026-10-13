#pragma once

#include "../Formats/Types.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace MphRead
{
    enum class RoomSize : std::uint32_t
    {
        None = 0,
        Small = 2,
        Medium = 3,
        Large = 4,
        SinglePlayer = 6
    };

    class RoomMetadata
    {
    public:
        const std::int32_t Id;
        const std::string Name;
        const std::optional<std::string> InGameName;
        const std::string Archive;
        const std::string ModelPath;
        const std::string AnimationPath;
        const std::string CollisionPath;
        const std::optional<std::string> TexturePath;
        const std::optional<std::string> EntityPath;
        const std::optional<std::string> EntityFilename;
        const std::optional<std::string> NodePath;
        const std::optional<std::string> RoomNodeName;
        const std::uint32_t BattleTimeLimit;
        const std::uint32_t TimeLimit;
        const std::int16_t PointLimit;
        const std::int16_t NodeLayer;
        const bool FogEnabled;
        const bool ClearFog;
        const ColorRgb FogColor;
        const std::int32_t FogSlope;
        const std::int32_t FogOffset;
        const float FarClip;
        const std::int32_t FarClipInt;
        const ColorRgb Light1Color;
        const OpenTK::Mathematics::Vector3 Light1Vector;
        const ColorRgb Light2Color;
        const OpenTK::Mathematics::Vector3 Light2Vector;
        const float KillHeight;
        const RoomSize Size;
        const bool Multiplayer;
        const bool FirstHunt;
        const bool Hybrid;
        const OpenTK::Mathematics::Vector3 CameraMin;
        const OpenTK::Mathematics::Vector3 CameraMax;
        const OpenTK::Mathematics::Vector3 PlayerMin;
        const OpenTK::Mathematics::Vector3 PlayerMax;
        const bool HasLimits;

        RoomMetadata(
            std::int32_t id,
            std::string name,
            std::optional<std::string> inGameName,
            std::string archive,
            std::string modelPath,
            std::string animationPath,
            std::string collisionPath,
            std::optional<std::string> texturePath,
            std::optional<std::string> entityPath,
            std::optional<std::string> nodePath,
            std::optional<std::string> roomNodeName,
            std::uint32_t battleTimeLimit,
            std::uint32_t timeLimit,
            std::int16_t pointLimit,
            std::int16_t nodeLayer,
            bool fogEnabled,
            bool clearFog,
            ColorRgb fogColor,
            std::int32_t fogSlope,
            std::uint16_t fogOffset,
            ColorRgb light1Color,
            OpenTK::Mathematics::Vector3 light1Vector,
            ColorRgb light2Color,
            OpenTK::Mathematics::Vector3 light2Vector,
            std::int32_t farClip,
            std::int32_t killHeight,
            RoomSize size,
            OpenTK::Mathematics::Vector3 cameraMin = {},
            OpenTK::Mathematics::Vector3 cameraMax = {},
            OpenTK::Mathematics::Vector3 playerMin = {},
            OpenTK::Mathematics::Vector3 playerMax = {},
            bool multiplayer = false,
            bool firstHunt = false,
            bool hybrid = false);
    };

    namespace Metadata
    {
        // The cartridge's rooms, then the map folder's. Appended to while the
        // game runs (AppendLateRooms) and nowhere else.
        extern std::vector<std::string> _roomIds;
        extern std::vector<std::shared_ptr<RoomMetadata>> RoomList;
        extern std::unordered_map<std::string, std::shared_ptr<RoomMetadata>> RoomMetadata;
        // Rooms the map folder holds now that these lists lacked when the game
        // started (a pack downloaded since): added with the next ids, the
        // ones already given never move. How many were added. On the thread
        // that loads rooms; nothing reads the lists meanwhile.
        std::int32_t AppendLateRooms();
        extern const std::unordered_map<std::int32_t, std::string> EncounterNodeDataOverrides;
        extern const std::unordered_map<std::int32_t, std::string> CtfNodeDataOverrides;
    }
}
