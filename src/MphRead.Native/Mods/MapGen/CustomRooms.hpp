#pragma once

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
    };
}
