#include "CustomRooms.hpp"
#include "../Platform/AppPaths.hpp"

#include "MapBundle.hpp"
#include <fstream>
#include "Q3Convert.hpp"
#include "../../Metadata/Metadata.hpp"
#include "Q3Bsp.hpp"
#include "Q3Import.hpp"
#include "OpenArenaTextures.hpp"
#include "MapDefinition.hpp"
#include "MapPacker.hpp"
#include "../../Formats/Types.hpp"
#include "../../Metadata/Rooms.hpp"
#include "../../Read.hpp"
#include "../../NativeRuntime/System/Encoding.hpp"
#include "../../NativeRuntime/System/Console.hpp"
#include "../../NativeRuntime/System/ExceptionText.hpp"
#include "../../NativeRuntime/System/IO.hpp"
#include "../../NativeRuntime/System/Managed.hpp"
#include "../../NativeRuntime/System/Globalization.hpp"

#include <algorithm>
#include <thread>
#include <cmath>
#include <array>
#include <bit>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

using ::MphRead::NativeRuntime::DirectoryExists;
using ::MphRead::NativeRuntime::FileExists;
using ::MphRead::NativeRuntime::PathCombine;
using ::MphRead::NativeRuntime::PathFromUtf8;
using ::MphRead::NativeRuntime::PathGetFileName;
using ::MphRead::NativeRuntime::PathGetFileNameWithoutExtension;
using ::MphRead::NativeRuntime::PathToUtf8;
using ::MphRead::NativeRuntime::UncheckedAdd;
using ::MphRead::NativeRuntime::Utf16ToUtf8;
using ::MphRead::NativeRuntime::Utf8ToUtf16;

namespace
{
    using MphRead::ColorRgb;
    using MphRead::Fixed;
    using MphRead::RoomSize;
    using MphRead::Mods::MapGen::CustomRooms;
    using MphRead::Mods::MapGen::MapDefinition;
    using OpenTK::Mathematics::Vector3;

    [[nodiscard]] std::filesystem::file_time_type GetLastWriteTimeUtc(
        const std::string& path)
    {
        const std::filesystem::path nativePath = PathFromUtf8(path);
        std::error_code error;
        const std::filesystem::file_time_type value =
            std::filesystem::last_write_time(nativePath, error);
        if (!error)
        {
            return value;
        }
        if (error == std::errc::no_such_file_or_directory
            || error == std::errc::not_a_directory)
        {
            // File.GetLastWriteTimeUtc returns its 1601 sentinel when the
            // file has disappeared. file_time_type::min() is an ordering-only
            // native sentinel with the same effect in the comparison below.
            return std::filesystem::file_time_type::min();
        }
        throw std::filesystem::filesystem_error(
            "last_write_time", nativePath, error);
    }

    [[nodiscard]] bool ExtensionMatches(
        const std::filesystem::path& path, std::string_view extension)
    {
        // Directory.EnumerateFiles(root, "*<extension>", ...) applies the
        // wildcard to the whole file name. In particular, "*" may match zero
        // characters, so a file named exactly ".fpmap" or ".json" matches.
        // std::filesystem::path::extension() treats such a dotfile as having
        // no extension, so compare the filename suffix instead.
        const std::string fileName = PathToUtf8(path.filename());
        if (fileName.size() < extension.size())
        {
            return false;
        }
        const std::string_view actual(
            fileName.data() + fileName.size() - extension.size(),
            extension.size());
#if defined(_WIN32) || defined(__APPLE__)
        return ::MphRead::NativeRuntime::StringCompareOrdinalIgnoreCase(actual, extension) == 0;
#else
        return actual == extension;
#endif
    }

    struct MapFileEnumeration final
    {
        std::vector<std::string> Results;
        std::unordered_set<
            std::string, ::MphRead::NativeRuntime::OrdinalIgnoreCaseHash, ::MphRead::NativeRuntime::OrdinalIgnoreCaseEqual> Names;
        std::filesystem::recursive_directory_iterator JsonIterator{};
        std::filesystem::recursive_directory_iterator End{};
    };

    [[nodiscard]] MapFileEnumeration MapFiles()
    {
        MapFileEnumeration files;
        if (!DirectoryExists(CustomRooms::MapDirectory()))
        {
            return files;
        }

        for (const std::filesystem::directory_entry& entry :
            std::filesystem::recursive_directory_iterator(
                PathFromUtf8(CustomRooms::MapDirectory()),
                std::filesystem::directory_options::follow_directory_symlink))
        {
            if (!entry.is_directory()
                && ExtensionMatches(
                    entry.path(), MphRead::Mods::MapGen::MapBundle::Extension))
            {
                files.Results.push_back(PathToUtf8(entry.path()));
            }
        }

        for (const std::string& path : files.Results)
        {
            files.Names.insert(PathGetFileNameWithoutExtension(path));
        }

        // Directory.EnumerateFiles creates its enumerable (and opens the root)
        // here. Walking its entries remains deferred until OrderBy enumerates
        // the Concat sequence in LoadDefinitions.
        files.JsonIterator = std::filesystem::recursive_directory_iterator(
            PathFromUtf8(CustomRooms::MapDirectory()),
            std::filesystem::directory_options::follow_directory_symlink);
        return files;
    }

    [[nodiscard]] std::vector<std::string> MaterializeMapFiles(
        MapFileEnumeration files)
    {
        while (files.JsonIterator != files.End)
        {
            const std::filesystem::directory_entry entry =
                *files.JsonIterator;
            if (!entry.is_directory()
                && ExtensionMatches(entry.path(), ".json"))
            {
                const std::string path = PathToUtf8(entry.path());
                if (!files.Names.contains(PathGetFileNameWithoutExtension(path)))
                {
                    files.Results.push_back(path);
                }
            }
            ++files.JsonIterator;
        }
        return std::move(files.Results);
    }

    struct CustomRoomsState final
    {
        std::shared_ptr<CustomRooms::DefinitionList> Definitions{};
        std::int32_t FirstId = -1;
        std::recursive_mutex Lock;
        std::string MapDirectory;

        CustomRoomsState()
            : MapDirectory(::MphRead::Mods::Platform::AppPaths::Maps())
        {
        }
    };

    [[nodiscard]] CustomRoomsState& State()
    {
        static CustomRoomsState state;
        return state;
    }

    [[nodiscard]] const std::vector<std::int32_t>& RequireIntArray(
        const std::vector<std::int32_t>* value)
    {
        if (value == nullptr)
        {
            throw System::NullReferenceException();
        }
        return *value;
    }

    [[nodiscard]] const std::vector<float>& RequireFloatArray(
        const std::vector<float>* value)
    {
        if (value == nullptr)
        {
            throw System::NullReferenceException();
        }
        return *value;
    }

    template <typename T>
    [[nodiscard]] const T& ArrayElement(
        const std::vector<T>& values, std::size_t index)
    {
        if (index >= values.size())
        {
            throw System::IndexOutOfRangeException();
        }
        return values[index];
    }

    [[nodiscard]] ColorRgb ToColor(const std::vector<std::int32_t>* values)
    {
        const std::vector<std::int32_t>& array = RequireIntArray(values);
        const std::uint8_t red =
            static_cast<std::uint8_t>(ArrayElement(array, 0));
        const std::uint8_t green =
            static_cast<std::uint8_t>(ArrayElement(array, 1));
        const std::uint8_t blue =
            static_cast<std::uint8_t>(ArrayElement(array, 2));
        return ColorRgb(red, green, blue);
    }

    [[nodiscard]] Vector3 ToVector(const std::vector<float>* values)
    {
        const std::vector<float>& array = RequireFloatArray(values);
        const float x = ArrayElement(array, 0);
        const float y = ArrayElement(array, 1);
        const float z = ArrayElement(array, 2);
        return Vector3(x, y, z);
    }
}

namespace MphRead::Mods::MapGen
{
    const std::string& CustomRooms::MapDirectory()
    {
        return State().MapDirectory;
    }

    void CustomRooms::MapDirectory(std::string value)
    {
        State().MapDirectory = std::move(value);
    }

    const CustomRooms::DefinitionList& CustomRooms::Definitions()
    {
        CustomRoomsState& state = State();
        std::lock_guard<std::recursive_mutex> guard(state.Lock);
        if (!state.Definitions)
        {
            state.Definitions = LoadDefinitions();
        }
        return *state.Definitions;
    }

    std::shared_ptr<CustomRooms::DefinitionList> CustomRooms::LoadDefinitions()
    {
        auto results = std::make_shared<DefinitionList>();
        if (!DirectoryExists(MapDirectory()))
        {
            return results;
        }

        std::vector<std::string> paths =
            MaterializeMapFiles(MapFiles());

        // Enumerable.OrderBy(string): stable, by the current culture.
        std::stable_sort(
            paths.begin(), paths.end(),
            [](const std::string& left, const std::string& right)
            {
                return ::MphRead::NativeRuntime::StringCompareCurrentCulture(left, right) < 0;
            });

        for (const std::string& path : paths)
        {
            try
            {
                std::shared_ptr<MapDefinition> definition =
                    MapDefinition::Load(path);
                definition->Name(
                    ::MphRead::NativeRuntime::ToUpperInvariant(definition->Name()));

                if (definition->Import() != nullptr)
                {
                    MapImport* resolveImport = definition->Import();
                    if (resolveImport == nullptr)
                    {
                        throw System::NullReferenceException();
                    }
                    if (!resolveImport->Resolve().has_value())
                    {
                        const std::string name = definition->Name();
                        MapImport* sourceImport = definition->Import();
                        if (sourceImport == nullptr)
                        {
                            throw System::NullReferenceException();
                        }
                        const std::string source = sourceImport->Source();
                        const std::optional<std::string> baseDirectory =
                            definition->BaseDirectory();
                        const std::string directory = baseDirectory.has_value()
                            ? *baseDirectory
                            : MapDirectory();

                        const std::string message =
                            "Leaving out map " + name
                            + ": its source level " + source
                            + " is not here. Put it in " + directory
                            + " to have this map.";
                        ::MphRead::NativeRuntime::ConsoleWriteLine(message);
                        continue;
                    }
                }
                results->push_back(std::move(definition));
            }
            catch (...)
            {
                const std::exception_ptr error = std::current_exception();
                const std::string fileName = PathGetFileName(path);
                const std::string message =
                    "Ignoring map " + fileName + ": "
                    + ::MphRead::NativeRuntime::ExceptionMessage(error);
                ::MphRead::NativeRuntime::ConsoleWriteLine(message);
            }
        }
        AddTranslated(*results);
        return results;
    }

    // Every level in every .pk3 or .zip in the map folder is a room, read
    // from the archive as it is (Q3Convert::Translate) -- except one a recipe
    // already imports, whose recipe says how it is to be played, and a name
    // already taken.
    void CustomRooms::AddTranslated(DefinitionList& results)
    {
        std::vector<std::string> archives;
        for (const std::filesystem::directory_entry& entry :
            std::filesystem::recursive_directory_iterator(
                PathFromUtf8(MapDirectory()),
                std::filesystem::directory_options::follow_directory_symlink
                    | std::filesystem::directory_options::skip_permission_denied))
        {
            if (!entry.is_directory()
                && (ExtensionMatches(entry.path(), ".pk3")
                    || ExtensionMatches(entry.path(), ".PK3")
                    || ExtensionMatches(entry.path(), ".zip")))
            {
                archives.push_back(PathToUtf8(entry.path()));
            }
        }
        std::stable_sort(
            archives.begin(), archives.end(),
            [](const std::string& left, const std::string& right)
            {
                return ::MphRead::NativeRuntime::StringCompareOrdinalIgnoreCase(left, right) < 0;
            });

        std::vector<std::pair<std::string, std::string>> imported;
        for (const std::shared_ptr<MapDefinition>& definition : results)
        {
            if (const MapImport* import = definition->Import())
            {
                if (const std::optional<std::string> source = import->Resolve())
                {
                    imported.emplace_back(
                        PathGetFileName(*source),
                        ::MphRead::NativeRuntime::ToLowerInvariant(import->MapName().value_or(std::string())));
                }
            }
        }

        for (const std::string& archive : archives)
        {
            std::vector<std::string> maps;
            try
            {
                maps = Q3Bsp::ListMaps(archive);
            }
            catch (...)
            {
                ::MphRead::NativeRuntime::ConsoleWriteLine("Ignoring " + PathGetFileName(archive) + ": "
                    + ::MphRead::NativeRuntime::ExceptionMessage(std::current_exception()));
                continue;
            }
            for (const std::string& map : maps)
            {
                const std::string lower = ::MphRead::NativeRuntime::ToLowerInvariant(map);
                const bool byRecipe = std::any_of(imported.begin(), imported.end(),
                    [&](const auto& pair)
                    {
                        return ::MphRead::NativeRuntime::StringCompareOrdinalIgnoreCase(
                                   pair.first, PathGetFileName(archive)) == 0
                            && (pair.second.empty() || pair.second == lower);
                    });
                const bool taken = std::any_of(results.begin(), results.end(),
                    [&](const std::shared_ptr<MapDefinition>& definition)
                    {
                        return ::MphRead::NativeRuntime::StringCompareOrdinalIgnoreCase(
                                   definition->Name(), map) == 0;
                    });
                if (byRecipe || taken)
                {
                    continue;
                }
                try
                {
                    results.push_back(Q3Convert::Translate(archive, map));
                }
                catch (...)
                {
                    ::MphRead::NativeRuntime::ConsoleWriteLine("Ignoring map " + map + " in "
                        + PathGetFileName(archive) + ": "
                        + ::MphRead::NativeRuntime::ExceptionMessage(std::current_exception()));
                }
            }
        }
    }

    std::string CustomRooms::PortalImagePath(MapDefinition* def, std::size_t portal)
    {
        return PathCombine(ArchiveDirectory(def), "portal_" + std::to_string(portal) + ".png");
    }

    std::vector<std::string> CustomRooms::RoomsWithoutPortalViews()
    {
        std::vector<std::string> rooms;
        for (const std::shared_ptr<MapDefinition>& definition : Definitions())
        {
            if (!definition->Translated() || NeedsGenerating(definition.get()))
            {
                continue;
            }
            for (std::size_t i = 0; i < definition->Portals().size(); ++i)
            {
                if (!FileExists(PortalImagePath(definition.get(), i)))
                {
                    rooms.push_back(definition->Name());
                    break;
                }
            }
        }
        return rooms;
    }

    float CustomRooms::Q3StepHeight(std::int32_t roomId)
    {
        static std::int32_t cachedRoom = -1;
        static float cachedHeight = 0.0F;
        if (roomId == cachedRoom)
        {
            return cachedHeight;
        }
        float height = 0.0F;
        if (const ::MphRead::RoomMetadata* meta = ::MphRead::Metadata::GetRoomById(roomId, true))
        {
            for (const std::shared_ptr<MapDefinition>& definition : Definitions())
            {
                if (definition->Translated()
                    && ::MphRead::NativeRuntime::StringCompareOrdinalIgnoreCase(definition->Name(), meta->Name) == 0)
                {
                    height = 18.0F / Q3Convert::UnitsPerUnit;
                    break;
                }
            }
        }
        cachedRoom = roomId;
        cachedHeight = height;
        return height;
    }

    namespace
    {
        thread_local std::optional<std::array<float, 4>>* fogQuery = nullptr;

        // The level a translated room was read from, kept while the room is
        // played: its tree for Q3Open, its fog volumes for Q3FogAt.
        struct Q3Level
        {
            std::int32_t Room = -1;
            std::shared_ptr<Q3Bsp> Bsp;
            Q3Import::Lighting Lighting;
        };
    }

    bool CustomRooms::Q3Open(std::int32_t roomId, OpenTK::Mathematics::Vector3 position)
    {
        static Q3Level level;
        std::int32_t& cachedRoom = level.Room;
        std::shared_ptr<Q3Bsp>& cachedLevel = level.Bsp;
        if (roomId != cachedRoom)
        {
            cachedRoom = roomId;
            cachedLevel.reset();
            level.Lighting = Q3Import::Lighting{};
            if (Q3StepHeight(roomId) > 0.0F)
            {
                const ::MphRead::RoomMetadata* meta = ::MphRead::Metadata::GetRoomById(roomId, true);
                for (const std::shared_ptr<MapDefinition>& definition : Definitions())
                {
                    if (meta != nullptr && definition->Translated() && definition->Import() != nullptr
                        && definition->SourcePath().has_value()
                        && ::MphRead::NativeRuntime::StringCompareOrdinalIgnoreCase(definition->Name(), meta->Name) == 0)
                    {
                        try
                        {
                            cachedLevel = Q3Bsp::Load(*definition->SourcePath(), definition->Import()->MapName());
                            level.Lighting = Q3Import::MakeLighting(cachedLevel.get(),
                                std::vector<std::string>{*definition->SourcePath()});
                        }
                        catch (...)
                        {
                            cachedLevel.reset();
                        }
                        break;
                    }
                }
            }
        }
        if (!cachedLevel)
        {
            return true;
        }
        const float unit = Q3Convert::UnitsPerUnit;
        if (fogQuery != nullptr)
        {
            const float x = position.X * unit;
            const float y = -position.Z * unit;
            const float z = position.Y * unit;
            for (const Q3Import::FogVolume& fog : level.Lighting.Fogs)
            {
                if (fog.Distance <= 0.0F || fog.Planes.empty())
                {
                    continue;
                }
                bool inside = true;
                for (const OpenTK::Mathematics::Vector4& plane : fog.Planes)
                {
                    if (plane.X * x + plane.Y * y + plane.Z * z - plane.W > 0.0F)
                    {
                        inside = false;
                        break;
                    }
                }
                if (inside)
                {
                    // Quake fogs only what a ray crosses inside the volume:
                    // in a layer thinner than the fog is deep, most rays
                    // leave it soon, and the room reads far clearer than its
                    // fogparms say. Thinned by how much thinner it is.
                    float low = -1.0e9F;
                    float high = 1.0e9F;
                    for (const OpenTK::Mathematics::Vector4& plane : fog.Planes)
                    {
                        if (plane.Z > 0.999F)
                        {
                            high = std::min(high, plane.W);
                        }
                        else if (plane.Z < -0.999F)
                        {
                            low = std::max(low, -plane.W);
                        }
                    }
                    const float thickness = high > low ? high - low : fog.Distance;
                    const float thin = std::sqrt(std::sqrt(std::max(1.0F, fog.Distance / std::max(thickness, 1.0F))));
                    *fogQuery = std::array<float, 4>{fog.Color.X, fog.Color.Y, fog.Color.Z, fog.Distance * thin / unit};
                    break;
                }
            }
            return true;
        }
        return cachedLevel->Open(position.X * unit, -position.Z * unit, position.Y * unit);
    }

    std::optional<std::array<float, 4>> CustomRooms::Q3FogAt(
        std::int32_t roomId, OpenTK::Mathematics::Vector3 position)
    {
        std::optional<std::array<float, 4>> fog;
        fogQuery = &fog;
        (void)Q3Open(roomId, position);
        fogQuery = nullptr;
        return fog;
    }

    std::string CustomRooms::TranslatorStamp(MapDefinition* def)
    {
        const std::string& source = ::MphRead::NativeRuntime::RequireReference(def).SourcePath().value();
        const auto time = GetLastWriteTimeUtc(source).time_since_epoch().count();
        return "translator " + ::MphRead::NativeRuntime::ToString(Q3Convert::TranslatorVersion)
            + " " + ::MphRead::NativeRuntime::ToString(static_cast<std::int64_t>(
                std::filesystem::file_size(PathFromUtf8(source))))
            + " " + ::MphRead::NativeRuntime::ToString(static_cast<std::int64_t>(time))
            + " " + OpenArenaTextures::Stamp();
    }

    std::string CustomRooms::TranslatorStampPath(MapDefinition* def)
    {
        return PathCombine(ArchiveDirectory(def), "translated.txt");
    }

    namespace
    {
        std::recursive_mutex& BuildLock();
    }

    std::string CustomRooms::FailedStampPath(MapDefinition* def)
    {
        return PathCombine(ArchiveDirectory(def), "failed.txt");
    }

    void CustomRooms::Build(MapDefinition* def, bool verbose, bool keepPortalViews)
    {
        const std::lock_guard<std::recursive_mutex> building(BuildLock());
        if (def == nullptr)
        {
            throw System::NullReferenceException();
        }
        const std::string archiveDirectory = ArchiveDirectory(def);
        if (def->Translated())
        {
            // The textures baked last time are part of what is built again.
            const std::string textures = PathCombine(archiveDirectory,
                ::MphRead::NativeRuntime::ToLowerInvariant(def->Name()) + ".tex");
            ::MphRead::NativeRuntime::RequireReference(def->Import()).Textures(textures);
            std::error_code ignored;
            std::filesystem::remove(PathFromUtf8(TranslatorStampPath(def)), ignored);
            std::filesystem::remove(PathFromUtf8(FailedStampPath(def)), ignored);
            std::filesystem::remove(PathFromUtf8(textures), ignored);
            std::filesystem::create_directories(PathFromUtf8(archiveDirectory), ignored);
            // The views through its portals belong to the level as it was:
            // rendered again unless this build is the one that uses them.
            if (!keepPortalViews)
            {
                for (std::size_t i = 0; i < def->Portals().size(); ++i)
                {
                    std::filesystem::remove(PathFromUtf8(PortalImagePath(def, i)), ignored);
                }
            }
        }
        try
        {
            MapPacker::Generate(def, archiveDirectory, EntityDirectory(), NodeDirectory(), verbose);
        }
        catch (...)
        {
            // Remembered, so the level is not tried again at every start
            // until it or the translator changes (NeedsGenerating).
            if (def->Translated())
            {
                std::ofstream failed(PathFromUtf8(FailedStampPath(def)), std::ios::trunc);
                failed << TranslatorStamp(def) << '\n' << ::MphRead::NativeRuntime::ExceptionMessage(std::current_exception()) << '\n';
            }
            throw;
        }
        if (def->Translated())
        {
            std::ofstream stamp(PathFromUtf8(TranslatorStampPath(def)), std::ios::trunc);
            stamp << TranslatorStamp(def);
        }
    }

    const std::vector<std::string>& CustomRooms::AppendIds(
        std::vector<std::string>& ids)
    {
        CustomRoomsState& state = State();
        state.FirstId = static_cast<std::int32_t>(ids.size());

        const DefinitionList& definitions = Definitions();
        for (const std::shared_ptr<MapDefinition>& definition : definitions)
        {
            ids.push_back(definition->Name());
        }
        return ids;
    }

    std::vector<std::string> CustomRooms::AppendIds(
        std::vector<std::string>&& ids)
    {
        (void)AppendIds(ids);
        return std::move(ids);
    }

    const std::vector<std::shared_ptr<MphRead::RoomMetadata>>&
        CustomRooms::AppendRooms(
            std::vector<std::shared_ptr<MphRead::RoomMetadata>>& rooms)
    {
        std::int32_t i = 0;
        for (;;)
        {
            const DefinitionList& countDefinitions = Definitions();
            if (static_cast<std::size_t>(i) >= countDefinitions.size())
            {
                break;
            }

            const DefinitionList& indexDefinitions = Definitions();
            MapDefinition* definition =
                indexDefinitions[static_cast<std::size_t>(i)].get();
            const std::int32_t id = UncheckedAdd(State().FirstId, i);
            rooms.push_back(MakeMetadata(definition, id));
            i = UncheckedAdd(i, 1);
        }
        return rooms;
    }

    std::vector<std::shared_ptr<MphRead::RoomMetadata>>
        CustomRooms::AppendRooms(
            std::vector<std::shared_ptr<MphRead::RoomMetadata>>&& rooms)
    {
        (void)AppendRooms(rooms);
        return std::move(rooms);
    }

    std::shared_ptr<MphRead::RoomMetadata> CustomRooms::MakeMetadata(
        MapDefinition* def, std::int32_t id)
    {
        if (def == nullptr)
        {
            throw System::NullReferenceException();
        }

        // Preserve the source expression order before entering the C++
        // constructor, whose argument evaluation order is otherwise unsuitable
        // for reproducing C#.
        const std::string prefix =
            ::MphRead::NativeRuntime::ToLowerInvariant(def->Name());
        const std::string name = def->Name();

        std::optional<std::string> inGameName = def->InGameName();
        if (!inGameName.has_value())
        {
            inGameName = def->Name();
        }

        const std::string archive = prefix;
        const std::string modelPath = prefix + "_Model.bin";
        const std::string animationPath = prefix + "_Anim.bin";
        const std::string collisionPath = prefix + "_Collision.bin";
        const std::optional<std::string> texturePath = std::nullopt;
        const std::string entityPath = prefix + "_Ent.bin";
        const std::string nodePath = prefix + "_Node.bin";
        const std::optional<std::string> roomNodeName = std::nullopt;
        const std::uint32_t battleTimeLimit = def->BattleTimeLimit();
        const std::uint32_t timeLimit = def->BattleTimeLimit();
        const std::int16_t pointLimit = def->PointLimit();
        const std::int16_t nodeLayer = 0;
        const bool fogEnabled = def->FogEnabled();
        const bool clearFog = false;
        const ColorRgb fogColor = ToColor(def->FogColor());
        const std::int32_t fogSlope = def->FogSlope();
        const std::uint16_t fogOffset =
            static_cast<std::uint16_t>(def->FogOffset());
        const ColorRgb light1Color = ToColor(def->Light1Color());
        const Vector3 light1Vector = ToVector(def->Light1Vector());
        const ColorRgb light2Color = ToColor(def->Light2Color());
        const Vector3 light2Vector = ToVector(def->Light2Vector());
        const std::int32_t farClip = Fixed::ToInt(def->FarClip());
        const std::int32_t killHeight = Fixed::ToInt(def->KillHeight());

        return std::make_shared<MphRead::RoomMetadata>(
            id,
            name,
            std::move(inGameName),
            archive,
            modelPath,
            animationPath,
            collisionPath,
            texturePath,
            entityPath,
            nodePath,
            roomNodeName,
            battleTimeLimit,
            timeLimit,
            pointLimit,
            nodeLayer,
            fogEnabled,
            clearFog,
            fogColor,
            fogSlope,
            fogOffset,
            light1Color,
            light1Vector,
            light2Color,
            light2Vector,
            farClip,
            killHeight,
            RoomSize::Large,
            Vector3{},
            Vector3{},
            Vector3{},
            Vector3{},
            true);
    }

    std::string CustomRooms::ArchiveDirectory(MapDefinition* def)
    {
        const std::string& fileSystem = Paths::FileSystem();
        if (def == nullptr)
        {
            throw System::NullReferenceException();
        }

        const std::string prefix =
            ::MphRead::NativeRuntime::ToLowerInvariant(def->Name());
        return Paths::Combine(fileSystem, "_archives", prefix);
    }

    std::string CustomRooms::EntityDirectory()
    {
        const std::string& fileSystem = Paths::FileSystem();
        return Paths::Combine(fileSystem, R"(levels\entities)");
    }

    std::string CustomRooms::NodeDirectory()
    {
        const std::string& fileSystem = Paths::FileSystem();
        return Paths::Combine(fileSystem, R"(levels\nodeData)");
    }

    std::int32_t CustomRooms::GenerateAll(bool force, bool verbose)
    {
        std::int32_t count = 0;
        for (const std::shared_ptr<MapDefinition>& def : Definitions())
        {
            if (force || NeedsGenerating(def.get()))
            {
                Build(def.get(), verbose);
                count = UncheckedAdd(count, 1);
            }
        }
        return count;
    }

    namespace
    {
        // One map built at a time, whoever asks: the background start, a
        // match about to load one, a portal view rendered.
        std::recursive_mutex& BuildLock()
        {
            static std::recursive_mutex lock;
            return lock;
        }

        struct GenerationState
        {
            std::mutex Lock;
            CustomRooms::GenerationProgress Progress;
        };

        GenerationState& Generation()
        {
            static GenerationState state;
            return state;
        }
    }

    void CustomRooms::StartGenerating()
    {
        try
        {
            // Listed here, before the thread reads the list.
            (void)Definitions();
        }
        catch (...)
        {
            return;
        }
        GenerationState& state = Generation();
        {
            const std::lock_guard<std::mutex> guard(state.Lock);
            if (state.Progress.Running)
            {
                return;
            }
            state.Progress = GenerationProgress{0, 0, {}, true};
        }
        std::thread([]()
        {
            try
            {
                GenerateMissing();
            }
            catch (...)
            {
            }
            GenerationState& done = Generation();
            const std::lock_guard<std::mutex> guard(done.Lock);
            done.Progress.Running = false;
            done.Progress.Current.clear();
        }).detach();
    }

    CustomRooms::GenerationProgress CustomRooms::Progress()
    {
        GenerationState& state = Generation();
        const std::lock_guard<std::mutex> guard(state.Lock);
        return state.Progress;
    }

    void CustomRooms::EnsureReady(const std::string& roomName)
    {
        const DefinitionList* definitions = nullptr;
        try
        {
            definitions = &Definitions();
        }
        catch (...)
        {
            return;
        }
        for (const std::shared_ptr<MapDefinition>& def : *definitions)
        {
            if (::MphRead::NativeRuntime::StringCompareOrdinalIgnoreCase(def->Name(), roomName) != 0)
            {
                continue;
            }
            const std::lock_guard<std::recursive_mutex> guard(BuildLock());
            try
            {
                if (NeedsGenerating(def.get()))
                {
                    ::MphRead::NativeRuntime::ConsoleWriteLine("[mapgen] building " + def->Name());
                    Build(def.get(), false);
                }
            }
            catch (...)
            {
                ::MphRead::NativeRuntime::ConsoleWriteLine("[mapgen] " + def->Name() + " could not be built: "
                    + ::MphRead::NativeRuntime::ExceptionMessage(std::current_exception()));
            }
            return;
        }
    }

    void CustomRooms::GenerateMissing()
    {
        const DefinitionList* definitions = nullptr;
        try
        {
            definitions = &Definitions();
        }
        catch (...)
        {
            return;
        }

        const std::lock_guard<std::recursive_mutex> building(BuildLock());
        std::vector<std::shared_ptr<MapDefinition>> missing;
        for (const std::shared_ptr<MapDefinition>& def : *definitions)
        {
            try
            {
                if (NeedsGenerating(def.get()))
                {
                    missing.push_back(def);
                }
            }
            catch (...)
            {
                missing.push_back(def);
            }
        }
        {
            GenerationState& state = Generation();
            const std::lock_guard<std::mutex> guard(state.Lock);
            state.Progress.Total = static_cast<std::int32_t>(missing.size());
            state.Progress.Done = 0;
        }

        for (const std::shared_ptr<MapDefinition>& def : missing)
        {
            {
                GenerationState& state = Generation();
                const std::lock_guard<std::mutex> guard(state.Lock);
                state.Progress.Current = def->InGameName().value_or(def->Name());
            }
            try
            {
                ::MphRead::NativeRuntime::ConsoleWriteLine(
                    "[mapgen] building " + def->Name());

                Build(def.get(), false);
            }
            catch (...)
            {
                const std::string name = def->Name();
                const std::exception_ptr error = std::current_exception();
                const std::string failureMessage =
                    "[mapgen] " + name + " could not be built: "
                    + ::MphRead::NativeRuntime::ExceptionMessage(error);
                ::MphRead::NativeRuntime::ConsoleWriteLine(failureMessage);
            }
            GenerationState& state = Generation();
            const std::lock_guard<std::mutex> guard(state.Lock);
            ++state.Progress.Done;
        }
    }

    std::optional<std::string> CustomRooms::WhyUnplayable(
        const std::string& roomName)
    {
        MapDefinition* def = nullptr;
        for (const std::shared_ptr<MapDefinition>& candidate : Definitions())
        {
            if (::MphRead::NativeRuntime::StringCompareOrdinalIgnoreCase(
                    candidate->Name(), roomName) == 0)
            {
                def = candidate.get();
                break;
            }
        }

        if (def == nullptr || !NeedsGenerating(def))
        {
            return std::nullopt;
        }

        std::string file = "its map file";
        const std::optional<std::string>& source = def->SourcePath();
        if (source.has_value())
        {
            file = PathGetFileName(*source);
        }

        return def->Name()
            + " could not be built from " + file
            + ", so there is no room to load. "
              "The [mapgen] line above says what went wrong with it.";
    }

    bool CustomRooms::NeedsGenerating(MapDefinition* def)
    {
        if (def == nullptr)
        {
            throw System::NullReferenceException();
        }

        // A level this translator could not build stays unbuilt until the
        // level or the translator changes, rather than costing every start.
        if (def->Translated())
        {
            std::ifstream failedFile(PathFromUtf8(FailedStampPath(def)));
            std::string failed;
            if (std::getline(failedFile, failed) && failed == TranslatorStamp(def))
            {
                return false;
            }
        }

        const std::string prefix =
            ::MphRead::NativeRuntime::ToLowerInvariant(def->Name());

        const std::string archiveDirectory = ArchiveDirectory(def);
        const std::string modelName = prefix + "_Model.bin";
        const std::string model = PathCombine(archiveDirectory, modelName);
        if (!FileExists(model))
        {
            return true;
        }

        const std::string entityDirectory = EntityDirectory();
        const std::string entityName = prefix + "_Ent.bin";
        const std::string entity = PathCombine(entityDirectory, entityName);
        if (!FileExists(entity))
        {
            return true;
        }

        const std::string nodeDirectory = NodeDirectory();
        const std::string nodeName = prefix + "_Node.bin";
        const std::string node = PathCombine(nodeDirectory, nodeName);
        if (!FileExists(node))
        {
            return true;
        }

        const std::optional<std::string>& source = def->SourcePath();
        if (!source.has_value() || !FileExists(*source))
        {
            return false;
        }

        if (def->Translated())
        {
            std::ifstream stampFile(PathFromUtf8(TranslatorStampPath(def)));
            std::string stamp;
            std::getline(stampFile, stamp);
            return stamp != TranslatorStamp(def);
        }

        const std::filesystem::file_time_type sourceTime =
            GetLastWriteTimeUtc(*source);
        const std::filesystem::file_time_type modelTime =
            GetLastWriteTimeUtc(model);
        return sourceTime > modelTime;
    }
}
