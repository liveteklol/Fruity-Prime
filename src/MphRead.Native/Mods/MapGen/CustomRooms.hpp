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

        // The map folder's rooms the two lists above did not have when they were
        // made: appended, each with the next id, ids and rooms together.
        static void AppendMissingRooms(
            std::vector<std::string>& ids,
            std::vector<std::shared_ptr<MphRead::RoomMetadata>>& rooms);
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
        // Quake's contents bits for the liquids that hurt, and how deep a
        // hunter standing at `feet` is in one (Quake's waterlevel: 0 dry, 1
        // to the ankles, 2 to the waist, 3 under) and which; 0 in any other
        // room.
        static constexpr std::int32_t Q3ContentsLava = 0x8;
        static constexpr std::int32_t Q3ContentsSlime = 0x10;
        [[nodiscard]] static std::int32_t Q3LiquidLevel(
            std::int32_t roomId, OpenTK::Mathematics::Vector3 feet, std::int32_t& contents);
        // The worst trigger_hurt the hunter's box touches, as Quake's hurt_touch
        // has it: the damage a hit does (0 for none) and whether the trigger
        // is the SLOW kind, hurting once a second rather than ten times.
        [[nodiscard]] static std::int32_t Q3HurtAt(
            std::int32_t roomId, OpenTK::Mathematics::Vector3 feet, bool& slow);
        // The Quake 3 level a translated room came from, read once and kept
        // while the room is (Q3Open's); nothing for any other room.
        [[nodiscard]] static std::shared_ptr<Q3Bsp> Q3LevelBsp(std::int32_t roomId);
        // The archives a translated room's level reads from: its own, then
        // the texture paks it names.
        [[nodiscard]] static std::vector<std::string> Q3LevelArchives(std::int32_t roomId);
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
            // Fetching the server's maps first (MapCatalog), not building.
            bool Downloading = false;
        };
        [[nodiscard]] static GenerationProgress Progress();
        // Maps the last StartGenerating downloaded, given once: the caller
        // then lists them (Rescan) and builds them (StartGenerating again).
        [[nodiscard]] static std::int32_t TakeDownloaded();
        // A pack fetched outside StartGenerating (the Download maps page):
        // TakeDownloaded gives it as the others.
        static void NoteDownloaded(std::int32_t count);
        // Counts the times the list of maps changed while the job ran: the launcher refreshes its rooms when it
        // moves, with no restart.
        [[nodiscard]] static std::int32_t ListVersion();
        // Lists the map folder again, for maps that arrived since. Not while
        // anything else walks the list: the launcher's thread, between jobs.
        static void Rescan();
        // A room about to be loaded: if it is one of these maps and not built
        // yet, waits for the build running, or builds it now.
        static void EnsureReady(const std::string& roomName);

        [[nodiscard]] static std::optional<std::string> WhyUnplayable(
            const std::string& roomName);

        // What a build of this map depends on (translator, archive, art):
        // anything made from the build is stale once it changes.
        [[nodiscard]] static std::string TranslatorStamp(MapDefinition* def);

        CustomRooms() = delete;
        CustomRooms(const CustomRooms&) = delete;
        CustomRooms& operator=(const CustomRooms&) = delete;

    private:
        [[nodiscard]] static std::shared_ptr<DefinitionList> LoadDefinitions();
        [[nodiscard]] static std::shared_ptr<MphRead::RoomMetadata> MakeMetadata(
            MapDefinition* def, std::int32_t id);
        [[nodiscard]] static bool NeedsGenerating(MapDefinition* def);
        static void AddTranslated(DefinitionList& results);
        [[nodiscard]] static std::string TranslatorStampPath(MapDefinition* def);
        [[nodiscard]] static std::string FailedStampPath(MapDefinition* def);
    };
}
