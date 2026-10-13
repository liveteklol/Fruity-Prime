#include "ThumbnailGenerator.hpp"
#include "../NativeRuntime/System/Globalization.hpp"

#include "ThumbnailBatch.hpp"
#include "../Formats/Formats.hpp"
#include "Launcher/Portable/GameFiles.hpp"
#include "MapGen/CustomRooms.hpp"
#include "MapGen/MapDefinition.hpp"
#include "../Metadata/Metadata.hpp"
#include "../Metadata/Rooms.hpp"
#include "Mods/Network/Integration/Launch/NetLaunch.hpp"
#include "../Read.hpp"
#include "../NativeRuntime/System/Encoding.hpp"
#include "../NativeRuntime/System/IO.hpp"
#include "../NativeRuntime/System/ZipArchive.hpp"
#include "../NativeRuntime/Stb/Image.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <fstream>
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

    std::atomic<std::int32_t> PreviewJobs{0};

    struct PreviewJob final
    {
        PreviewJob() { ++PreviewJobs; }
        ~PreviewJob() { --PreviewJobs; }
        PreviewJob(const PreviewJob&) = delete;
        PreviewJob& operator=(const PreviewJob&) = delete;
    };

    // levelshots/<map>.<jpg|tga|png> in the archive, any case.
    std::vector<std::uint8_t> ReadLevelshot(const std::string& archivePath, const std::string& map)
    {
        const std::shared_ptr<::MphRead::NativeRuntime::ZipArchive> archive
            = ::MphRead::NativeRuntime::ZipArchive::OpenRead(archivePath);
        for (const std::string_view extension : {".jpg", ".tga", ".png", ".jpeg"})
        {
            const std::string wanted = "levelshots/" + map + std::string(extension);
            for (const auto& entry : archive->Entries())
            {
                if (::MphRead::NativeRuntime::StringCompareOrdinalIgnoreCase(entry->FullName(), wanted) == 0)
                {
                    return entry->ReadAllBytes();
                }
            }
        }
        return {};
    }

    // The middle of the image at the launcher's 16:9, at its own resolution
    // (whatever reads the preview scales it), as an RGB PNG.
    bool WriteCropped(const ::MphRead::NativeRuntime::Image& image, const std::string& path)
    {
        if (image.Width <= 0 || image.Height <= 0
            || image.Pixels.size() < static_cast<std::size_t>(image.Width) * image.Height * 3U)
        {
            return false;
        }
        std::int32_t width = image.Width;
        std::int32_t height = width * 9 / 16;
        if (height > image.Height)
        {
            height = image.Height;
            width = std::min(image.Width, height * 16 / 9);
        }
        const std::int32_t left = (image.Width - width) / 2;
        const std::int32_t top = (image.Height - height) / 2;
        std::vector<std::uint8_t> rgb(static_cast<std::size_t>(width) * height * 3U);
        for (std::int32_t y = 0; y < height; y++)
        {
            const std::uint8_t* const from = image.Pixels.data()
                + (static_cast<std::size_t>(top + y) * image.Width + left) * 3U;
            std::copy(from, from + static_cast<std::size_t>(width) * 3U,
                rgb.data() + static_cast<std::size_t>(y) * width * 3U);
        }
        const std::string temporary = path + ".tmp";
        {
            std::ofstream stream(PathFromUtf8(temporary), std::ios::binary | std::ios::trunc);
            if (!stream)
            {
                return false;
            }
            ::stbi_write_png_to_func(
                [](void* context, void* data, int size)
                {
                    static_cast<std::ofstream*>(context)->write(static_cast<const char*>(data), size);
                },
                &stream, width, height, 3, rgb.data(), width * 3);
            if (!stream)
            {
                return false;
            }
        }
        std::error_code error;
        std::filesystem::rename(PathFromUtf8(temporary), PathFromUtf8(path), error);
        return !error;
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

    bool ThumbnailGenerator::RefreshingPreviews() noexcept
    {
        return PreviewJobs.load() > 0;
    }

    void ThumbnailGenerator::EnsureCustomPreviews(std::function<void(const std::string&)> report)
    {
        if (!Launcher::GameFiles::Ready())
        {
            return;
        }
        const PreviewJob job;
        if (!report)
        {
            report = [](const std::string& line)
            {
                std::cout << "  " << line << std::endl;
            };
        }

        // name -> (archive, map), for the levels read from a .pk3.
        std::vector<std::pair<std::shared_ptr<MapGen::MapDefinition>, std::string>> levels;
        std::vector<std::string> archives;
        try
        {
            for (const auto& definition : MapGen::CustomRooms::Definitions())
            {
                const MapGen::MapImport* import = definition->Import();
                if (!definition->Translated() || import == nullptr)
                {
                    continue;
                }
                const std::optional<std::string> archive = import->Resolve();
                if (!archive.has_value())
                {
                    continue;
                }
                levels.emplace_back(definition, *archive);
                if (std::find(archives.begin(), archives.end(), *archive) == archives.end())
                {
                    archives.push_back(*archive);
                }
            }
        }
        catch (...)
        {
            return;
        }

        try
        {
            EnsureCacheDirectory();
        }
        catch (...)
        {
            return;
        }

        // The levelshot, whether the level is built or not: a marker beside
        // the PNG says it came from there (a rendered one is replaced).
        std::vector<std::string> render;
        std::int32_t fromArchives = 0;
        for (const auto& [definition, archive] : levels)
        {
            const std::string room = definition->Name();
            const std::string png = PathFor(room);
            const std::string marker = png + ".levelshot";
            if (Exists(room) && ::MphRead::NativeRuntime::FileExists(marker))
            {
                continue;
            }
            const std::string map = definition->Import()->MapName().value_or(room);
            bool written = false;
            try
            {
                std::vector<std::uint8_t> bytes = ReadLevelshot(archive, map);
                // pak0's levels can have their shot in another pak.
                for (std::size_t i = 0; bytes.empty() && i < archives.size(); i++)
                {
                    if (archives[i] != archive)
                    {
                        bytes = ReadLevelshot(archives[i], map);
                    }
                }
                if (!bytes.empty())
                {
                    written = WriteCropped(::MphRead::NativeRuntime::LoadImage(bytes, 3), png);
                }
            }
            catch (const std::exception& ex)
            {
                report(room + ": levelshot unreadable: " + ex.what());
            }
            if (written)
            {
                ::MphRead::NativeRuntime::FileWriteAllText(marker, "");
                fromArchives++;
            }
            else if (!Exists(room))
            {
                render.push_back(room);
            }
        }
        if (fromArchives > 0)
        {
            report(std::to_string(fromArchives) + " preview(s) taken from the levels' own levelshots");
        }

        // The rest are drawn by the game, once built, and only once per build:
        // a level that comes out black is left with the placeholder.
        std::vector<std::string> missing;
        std::vector<std::pair<std::string, std::string>> stamps;
        if (ThumbnailBatch::CanRun())
        {
            for (const std::string& room : render)
            {
                const auto found = std::find_if(levels.begin(), levels.end(),
                    [&](const auto& level) { return level.first->Name() == room; });
                if (found == levels.end()
                    || MapGen::CustomRooms::WhyUnplayable(room).has_value())
                {
                    continue;
                }
                std::string stamp;
                try
                {
                    stamp = MapGen::CustomRooms::TranslatorStamp(found->first.get());
                }
                catch (...)
                {
                }
                const std::string failed = PathFor(room) + ".failed";
                if (::MphRead::NativeRuntime::FileExists(failed))
                {
                    try
                    {
                        if (::MphRead::NativeRuntime::FileReadAllText(failed) == stamp)
                        {
                            continue;
                        }
                    }
                    catch (...)
                    {
                    }
                }
                missing.push_back(room);
                stamps.emplace_back(failed, stamp);
            }
        }
        if (missing.empty())
        {
            return;
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
        for (std::size_t i = 0; i < missing.size(); i++)
        {
            if (!Exists(missing[i]))
            {
                try
                {
                    ::MphRead::NativeRuntime::FileWriteAllText(stamps[i].first, stamps[i].second);
                }
                catch (...)
                {
                }
            }
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
