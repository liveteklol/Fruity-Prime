#pragma once

#include "../../Formats/Types.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace MphRead
{
    class RoomMetadata;
}

namespace MphRead::Mods::MapGen
{
    class Q3Bsp;
}

namespace MphRead::Mods::MapGen
{
    class MapDefinition;

    class CustomRooms final
    {
    public:
        using DefinitionList = std::vector<std::shared_ptr<MapDefinition>>;

        [[nodiscard]] static const std::string& MapDirectory();
        static void MapDirectory(std::string value);

        [[nodiscard]] static const DefinitionList& Definitions();

        [[nodiscard]] static const std::vector<std::string>& AppendIds(
            std::vector<std::string>& ids);
        [[nodiscard]] static std::vector<std::string> AppendIds(
            std::vector<std::string>&& ids);
        [[nodiscard]] static const std::vector<std::shared_ptr<MphRead::RoomMetadata>>& AppendRooms(
            std::vector<std::shared_ptr<MphRead::RoomMetadata>>& rooms);
        [[nodiscard]] static std::vector<std::shared_ptr<MphRead::RoomMetadata>> AppendRooms(
            std::vector<std::shared_ptr<MphRead::RoomMetadata>>&& rooms);

        [[nodiscard]] static std::string ArchiveDirectory(MapDefinition* def);
        [[nodiscard]] static std::string EntityDirectory();
        [[nodiscard]] static std::string NodeDirectory();

        // Quake's STEPSIZE in this game's units for a room read from a Quake 3
        // level, 0 for any other: how high a hunter steps up there.
        [[nodiscard]] static float Q3StepHeight(std::int32_t roomId);
        // False where a room read from a Quake 3 level has nowhere to stand:
        // inside its walls or out in the void around it. Always true in any
        // other room.
        [[nodiscard]] static bool Q3Open(std::int32_t roomId, OpenTK::Mathematics::Vector3 position);
        // The Quake 3 level a translated room came from, read once and kept
        // while the room is (Q3Open's); nothing for any other room.
        [[nodiscard]] static std::shared_ptr<Q3Bsp> Q3LevelBsp(std::int32_t roomId);
        // The fog a point of such a room is in, as Quake's fogparms has it
        // (red, green, blue 0-1, and the distance it is opaque at in this
        // game's units); none outside every fog volume, and in any other room.
        [[nodiscard]] static std::optional<std::array<float, 4>> Q3FogAt(
            std::int32_t roomId, OpenTK::Mathematics::Vector3 position);
        // A translated room's fog volumes as boxes in this game's units and
        // axes -- low corner, high corner, colour, and the depth each is
        // opaque at -- nearest `eye` first; none in any other room.
        struct FogBox
        {
            OpenTK::Mathematics::Vector3 Min{};
            OpenTK::Mathematics::Vector3 Max{};
            OpenTK::Mathematics::Vector3 Color{};
            float Depth = 0.0F;
        };
        [[nodiscard]] static std::vector<FogBox> Q3FogBoxes(
            std::int32_t roomId, OpenTK::Mathematics::Vector3 eye, std::size_t most);
        // Where portal i of a translated level's room keeps the view from its
        // camera, and the rooms still without one (ThumbnailGenerator renders
        // them, then they are built again with them).
        [[nodiscard]] static std::string PortalImagePath(MapDefinition* def, std::size_t portal);
        [[nodiscard]] static std::vector<std::string> RoomsWithoutPortalViews();
        // Generates the room's files: MapPacker::Generate, and for a level
        // read from its archive the stamp NeedsGenerating compares.
        static void Build(MapDefinition* def, bool verbose, bool keepPortalViews = false);
        [[nodiscard]] static std::int32_t GenerateAll(
            bool force = false, bool verbose = true);
        static void GenerateMissing();
        // GenerateMissing on a thread of its own, so the launcher is up while
        // the levels added since the last start are read (once each: what
        // is built stays built until its .pk3 or the translator changes).
        // Nothing when it is already running.
        static void StartGenerating();
        struct GenerationProgress
        {
            std::int32_t Total = 0;
            std::int32_t Done = 0;
            std::string Current{};
            bool Running = false;
        };
        [[nodiscard]] static GenerationProgress Progress();
        // A room about to be loaded: if it is one of these maps and not built
        // yet, waits for the build running, or builds it now.
        static void EnsureReady(const std::string& roomName);

        [[nodiscard]] static std::optional<std::string> WhyUnplayable(
            const std::string& roomName);

        CustomRooms() = delete;
        CustomRooms(const CustomRooms&) = delete;
        CustomRooms& operator=(const CustomRooms&) = delete;

    private:
        [[nodiscard]] static std::shared_ptr<DefinitionList> LoadDefinitions();
        [[nodiscard]] static std::shared_ptr<MphRead::RoomMetadata> MakeMetadata(
            MapDefinition* def, std::int32_t id);
        [[nodiscard]] static bool NeedsGenerating(MapDefinition* def);
        static void AddTranslated(DefinitionList& results);
        [[nodiscard]] static std::string TranslatorStamp(MapDefinition* def);
        [[nodiscard]] static std::string TranslatorStampPath(MapDefinition* def);
        [[nodiscard]] static std::string FailedStampPath(MapDefinition* def);
    };
}
