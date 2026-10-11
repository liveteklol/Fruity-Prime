#include "ThumbnailGenerator.hpp"
#include "../NativeRuntime/System/Globalization.hpp"

#include "ThumbnailBatch.hpp"
#include "../Formats/Formats.hpp"
#include "Launcher/Portable/GameFiles.hpp"
#include "MapGen/CustomRooms.hpp"
#include "MapGen/MapDefinition.hpp"
#include "../Metadata/Metadata.hpp"
#include "../Metadata/Rooms.hpp"
#include "Network/NetLaunch.hpp"
#include "../Read.hpp"
#include "../NativeRuntime/System/Encoding.hpp"
#include "../NativeRuntime/System/IO.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include "../NativeRuntime/System/Sort.hpp"

using ::MphRead::NativeRuntime::PathFromUtf8;
using ::MphRead::NativeRuntime::PathToUtf8;
using ::MphRead::NativeRuntime::Utf16ToUtf8;
using ::MphRead::NativeRuntime::Utf8ToUtf16;

using ::MphRead::NativeRuntime::ManagedSort;
namespace
{
    using ::MphRead::NativeRuntime::OrdinalIgnoreCaseEqual;
    using ::MphRead::NativeRuntime::OrdinalIgnoreCaseHash;

    int CompareOrdinalIgnoreCase(std::string_view left, std::string_view right)
    {
        return ::MphRead::NativeRuntime::StringCompareOrdinalIgnoreCase(left, right);
    }

    using SpawnCache = std::unordered_map<
        std::string,
        bool,
        OrdinalIgnoreCaseHash,
        OrdinalIgnoreCaseEqual>;

    SpawnCache& GetSpawnCache()
    {
        static SpawnCache cache;
        return cache;
    }

}

namespace MphRead::Mods
{
    static bool HasPlayerSpawn(const std::string& roomKey, const RoomMetadata& meta);

    std::string ThumbnailGenerator::CacheDirectory()
    {
        return PathToUtf8(PathFromUtf8(Launcher::GameFiles::Root()) / "thumbnails");
    }

    std::string ThumbnailGenerator::PathFor(const std::string& roomKey)
    {
        const std::u16string source = Utf8ToUtf16(roomKey);
        std::u16string safe;
        safe.reserve(source.size());

        for (char16_t value : source)
        {
            safe.push_back(::MphRead::NativeRuntime::CharIsLetterOrDigit(value)
                ? static_cast<char16_t>(::MphRead::NativeRuntime::ToLowerInvariant(static_cast<char32_t>(value)))
                : u'_');
        }

        const std::string filename = Utf16ToUtf8(safe) + ".png";
        return PathToUtf8(PathFromUtf8(CacheDirectory()) / PathFromUtf8(filename));
    }

    bool ThumbnailGenerator::Exists(const std::string& roomKey)
    {
        const std::filesystem::path path = PathFromUtf8(PathFor(roomKey));
        std::error_code error;
        const bool exists = std::filesystem::is_regular_file(path, error);
        if (error || !exists)
        {
            return false;
        }
        return std::filesystem::file_size(path) > 0;
    }

    std::vector<std::string> ThumbnailGenerator::MultiplayerRooms()
    {
        const bool firstHuntAvailable = FirstHuntAvailable();
        std::vector<std::string> rooms;

        for (const auto& entry : Metadata::RoomMetadata)
        {
            const auto& roomKey = entry.first;
            const RoomMetadata& meta = *entry.second;

            if (!meta.Multiplayer)
            {
                continue;
            }
            if (meta.FirstHunt && !firstHuntAvailable)
            {
                continue;
            }
            if (!HasPlayerSpawn(roomKey, meta))
            {
                continue;
            }
            rooms.push_back(roomKey);
        }

        ManagedSort(rooms, [](const std::string& left, const std::string& right)
            {
                return CompareOrdinalIgnoreCase(left, right);
            });
        return rooms;
    }

    static bool HasPlayerSpawn(
        const std::string& roomKey,
        const RoomMetadata& meta)
    {
        SpawnCache& cache = GetSpawnCache();
        if (const auto iterator = cache.find(roomKey); iterator != cache.end())
        {
            return iterator->second;
        }

        bool found = false;
        try
        {
            if (meta.EntityPath.has_value())
            {
                const std::int32_t layerId = Metadata::GetMultiplayerEntityLayer(
                    GameMode::Battle, Network::NetLaunch::RoomPlayerCount());

                // The list has to be held: iterating over the dereferenced
                // return value frees it before the first step, and reading a
                // destroyed entity is what threw std::bad_cast below.
                const std::shared_ptr<const std::vector<std::shared_ptr<Entity>>>
                    entities = Read::GetEntities(
                        *meta.EntityPath, layerId, meta.FirstHunt);
                for (const auto& entity : *entities)
                {
                    if (entity->Type != EntityType::PlayerSpawn
                        && entity->Type != EntityType::FhPlayerSpawn)
                    {
                        continue;
                    }

                    const auto& spawn =
                        dynamic_cast<const EntityOf<PlayerSpawnEntityData>&>(*entity);
                    if (spawn.Data.Active != 0)
                    {
                        found = true;
                        break;
                    }
                }
            }
        }
        catch (const std::exception& ex)
        {
            std::cout << "[rooms] could not read the spawns in "
                      << roomKey << ": " << ex.what() << std::endl;
            found = true;
        }

        cache[roomKey] = found;
        return found;
    }

    bool ThumbnailGenerator::FirstHuntAvailable()
    {
        try
        {
            const std::string path = Paths::FhFileSystem();
            if (path.empty())
            {
                return false;
            }
            std::error_code error;
            const bool exists = std::filesystem::is_directory(PathFromUtf8(path), error);
            return !error && exists;
        }
        catch (...)
        {
            return false;
        }
    }

    std::vector<std::string> ThumbnailGenerator::MissingThumbnails()
    {
        std::vector<std::string> missing;
        for (const std::string& room : MultiplayerRooms())
        {
            if (!Exists(room))
            {
                missing.push_back(room);
            }
        }
        return missing;
    }

    void ThumbnailGenerator::EnsureCustomPreviews(std::function<void(const std::string&)> report)
    {
        if (!Launcher::GameFiles::Ready() || !ThumbnailBatch::CanRun())
        {
            return;
        }

        std::vector<std::string> missing;
        try
        {
            std::unordered_set<std::string> custom;
            for (const auto& definition : MapGen::CustomRooms::Definitions())
            {
                custom.insert(definition->Name());
            }

            for (std::string room : MissingThumbnails())
            {
                if (custom.contains(room))
                {
                    missing.push_back(std::move(room));
                }
            }
        }
        catch (...)
        {
            return;
        }

        if (missing.empty())
        {
            return;
        }

        if (!report)
        {
            report = [](const std::string& line)
            {
                std::cout << "  " << line << std::endl;
            };
        }

        report("Rendering " + std::to_string(missing.size()) + " custom map preview(s)...");

        try
        {
            ThumbnailBatch::Run(
                missing,
                ThumbnailBatch::DefaultParallelism(),
                ThumbnailWidth,
                ThumbnailHeight,
                report);
        }
        catch (const std::exception& ex)
        {
            report("could not render: " + std::string(ex.what()));
        }
    }

    void ThumbnailGenerator::EnsurePortalViews(std::function<void(const std::string&)> report)
    {
        if (!Launcher::GameFiles::Ready() || !ThumbnailBatch::CanRun())
        {
            return;
        }
        std::vector<std::string> rooms;
        try
        {
            rooms = MapGen::CustomRooms::RoomsWithoutPortalViews();
        }
        catch (...)
        {
            return;
        }
        if (rooms.empty())
        {
            return;
        }
        if (!report)
        {
            report = [](const std::string& line) { std::cout << "  " << line << std::endl; };
        }
        report("Rendering the portals of " + std::to_string(rooms.size()) + " map(s)...");
        std::vector<std::string> keys;
        for (const std::string& room : rooms)
        {
            keys.push_back(room + "#portals");
        }
        try
        {
            (void)ThumbnailBatch::Run(keys, 1, PortalViewSize, PortalViewSize, report);
        }
        catch (const std::exception& ex)
        {
            report("could not render portals: " + std::string(ex.what()));
            return;
        }
        for (const auto& definition : MapGen::CustomRooms::Definitions())
        {
            if (std::find(rooms.begin(), rooms.end(), definition->Name()) == rooms.end())
            {
                continue;
            }
            try
            {
                MapGen::CustomRooms::Build(definition.get(), false, true);
            }
            catch (const std::exception& ex)
            {
                report(definition->Name() + " could not be built with its portals: " + ex.what());
            }
        }
    }

    void ThumbnailGenerator::EnsureCacheDirectory()
    {
        std::filesystem::create_directories(PathFromUtf8(CacheDirectory()));
    }
}
