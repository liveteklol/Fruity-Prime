#include "Q3Convert.hpp"
#include "Q3Import.hpp"
#include "NativeRuntime/System/Charconv.hpp"

#include "../../Formats/Enums.hpp"
#include "CustomRooms.hpp"
#include "MapBuilder.hpp"
#include "MapDefinition.hpp"
#include "MapTextureBake.hpp"
#include "Q3Bsp.hpp"
#include "../../Formats/Types.hpp"
#include "../../NativeRuntime/System/Encoding.hpp"
#include "../../NativeRuntime/System/Globalization.hpp"
#include "../../NativeRuntime/System/IO.hpp"
#include "../../NativeRuntime/System/Managed.hpp"
#include "../../NativeRuntime/System/Console.hpp"
#include "../../NativeRuntime/System/ExceptionText.hpp"
#include "NativeRuntime/System/Globalization.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <cwctype>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <memory>
#include <numbers>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#endif

using ::MphRead::NativeRuntime::AppendUtf8;
using ::MphRead::NativeRuntime::DecodeUtf8Scalar;
using ::MphRead::NativeRuntime::DirectoryCreateDirectory;
using ::MphRead::NativeRuntime::FileExists;
using ::MphRead::NativeRuntime::ManagedAt;
using ::MphRead::NativeRuntime::ManagedListAt;
using ::MphRead::NativeRuntime::MathMax;
using ::MphRead::NativeRuntime::MathMin;
using ::MphRead::NativeRuntime::PathCombine;
using ::MphRead::NativeRuntime::PathFromUtf8;
using ::MphRead::NativeRuntime::PathGetFileName;
using ::MphRead::NativeRuntime::PathGetFullPath;
using ::MphRead::NativeRuntime::PathToUtf8;
using ::MphRead::NativeRuntime::RequireReference;
using ::MphRead::NativeRuntime::RoundToEven;
using ::MphRead::NativeRuntime::StringEqualsOrdinalIgnoreCase;
using ::MphRead::NativeRuntime::UncheckedAdd;
using ::MphRead::NativeRuntime::Utf16ToUtf8;
using ::MphRead::NativeRuntime::Utf8Scalar;
using ::MphRead::NativeRuntime::WideToUtf8;

namespace
{
    using MphRead::ItemType;
    using MphRead::Mods::MapGen::MapDefinition;
    using MphRead::Mods::MapGen::Q3Entity;
    using MphRead::Mods::MapGen::Q3StringEqual;

    void AppendConsoleValue(std::string& output, std::string_view value)
    {
        output.append(value);
    }

    template <typename T>
        requires std::is_integral_v<T> && (!std::is_same_v<T, bool>)
    void AppendConsoleValue(std::string& output, T value)
    {
        output += ::MphRead::NativeRuntime::ToString(value);
    }

    template <typename... T>
    void WriteLine(const T&... values)
    {
        std::string line;
        (AppendConsoleValue(line, values), ...);
        ::MphRead::NativeRuntime::ConsoleWriteLine(line);
    }

    void ValidatePathText(const std::string& path)
    {
        if (path.find('\0') != std::string::npos)
        {
            throw System::ArgumentException();
        }
    }

    [[nodiscard]] bool WindowsEffectivelyEmpty(
        const std::string& path) noexcept
    {
#if defined(_WIN32)
        return !path.empty()
            && std::all_of(
                path.begin(), path.end(),
                [](char ch) noexcept { return ch == ' '; });
#else
        (void)path;
        return false;
#endif
    }

    void CopyFile(
        const std::string& source,
        const std::string& destination)
    {
        (void)std::filesystem::copy_file(
            PathFromUtf8(source),
            PathFromUtf8(destination),
            std::filesystem::copy_options::overwrite_existing);
    }

    [[nodiscard]] const std::string* EntityValue(
        const Q3Entity* entity,
        std::string_view key)
    {
        if (entity == nullptr)
        {
            throw System::NullReferenceException();
        }
        for (const auto& pair : *entity)
        {
            if (::MphRead::NativeRuntime::StringEqualsOrdinalIgnoreCase(pair.first, key))
            {
                return std::addressof(pair.second);
            }
        }
        return nullptr;
    }

    [[nodiscard]] double AverageAxis(
        const std::vector<std::shared_ptr<std::vector<float>>>& values,
        std::size_t axis)
    {
        if (values.empty())
        {
            throw System::InvalidOperationException("Sequence contains no elements");
        }

        double sum = static_cast<double>(
            ManagedAt(&RequireReference(values.front()), axis));
        std::int64_t count = 1;
        for (std::size_t i = 1; i < values.size(); ++i)
        {
            sum += static_cast<double>(
                ManagedAt(&RequireReference(values[i]), axis));
            count = std::bit_cast<std::int64_t>(
                static_cast<std::uint64_t>(count) + 1U);
        }
        return sum / static_cast<double>(count);
    }

    [[nodiscard]] std::string JoinMultiplayerItems(
        const MphRead::Mods::MapGen::ItemTypeHashSet& values)
    {
        std::string result;
        bool first = true;
        for (const ItemType value : values)
        {
            if (!first)
            {
                result += ", ";
            }
            first = false;
            result += ::MphRead::ToString(value);
        }
        return result;
    }

    [[nodiscard]] std::string JoinStrings(
        const std::vector<std::string>& values,
        std::size_t count)
    {
        std::string result;
        const std::size_t limit = std::min(count, values.size());
        for (std::size_t i = 0; i < limit; ++i)
        {
            if (i != 0)
            {
                result += ", ";
            }
            result += values[i];
        }
        return result;
    }
}

namespace MphRead::Mods::MapGen
{
    std::int32_t Q3Convert::Run(
        const std::string& source,
        const std::optional<std::string>& mapName,
        const std::optional<std::string>& roomName,
        const std::optional<std::string>& outputDir,
        bool dropClip,
        bool dropItems,
        const std::optional<float>& forcedScale,
        std::int32_t textureSize,
        const std::vector<std::string>& texturePaks)
    {
        if (!FileExists(source))
        {
            WriteLine("No such file: ", source);
            return 1;
        }

        const std::string& sourceValue = source;
        std::shared_ptr<Q3Bsp> bsp;
        try
        {
            bsp = Q3Bsp::Load(sourceValue, mapName);
        }
        catch (...)
        {
            WriteLine(::MphRead::NativeRuntime::ExceptionMessage(std::current_exception()));
            return 1;
        }

        std::optional<std::string> selectedMapName = mapName;
        if (!selectedMapName.has_value())
        {
            const std::vector<std::string> maps
                = Q3Bsp::ListMaps(sourceValue);
            if (!maps.empty())
            {
                selectedMapName = maps.front();
            }
        }

        const std::string roomValue = roomName.has_value()
            ? *roomName
            : selectedMapName.has_value()
                ? *selectedMapName
                : "CUSTOM";
        const std::string room = ::MphRead::NativeRuntime::ToUpperInvariant(roomValue);
        const std::string prefix = ::MphRead::NativeRuntime::ToLowerInvariant(room);
        const std::string directory = outputDir.has_value()
            ? *outputDir
            : PathCombine(CustomRooms::MapDirectory(), prefix);
        DirectoryCreateDirectory(directory);

        std::shared_ptr<std::vector<float>> min;
        std::shared_ptr<std::vector<float>> max;
        Bounds(&RequireReference(bsp), min, max, false);
        if (ManagedAt(min.get(), 0) > ManagedAt(max.get(), 0))
        {
            WriteLine(selectedMapName.value_or(std::string()), " has no drawn surfaces.");
            return 1;
        }

        const float xExtent
            = ManagedAt(max.get(), 0) - ManagedAt(min.get(), 0);
        const float yExtent
            = ManagedAt(max.get(), 1) - ManagedAt(min.get(), 1);
        const float zExtent
            = ManagedAt(max.get(), 2) - ManagedAt(min.get(), 2);
        const float widest = MathMax(
            xExtent,
            MathMax(yExtent, zExtent));
        // The scale is for the part of the level anybody plays in. A level
        // may stand in a vista many times its arena's size -- acid3dm12's
        // floor and water run 16384 units across round an arena of 1500 --
        // and measured on that it came out at 126 units each, a room a
        // hunter crosses in two steps. Where the level places enough things
        // to say where play happens, their extent (and a margin) is what is
        // measured; the vista is still drawn.
        float measured = widest;
        {
            std::vector<std::shared_ptr<std::vector<float>>> placed;
            for (const std::shared_ptr<Q3Entity>& entityRef : RequireReference(bsp).Entities())
            {
                const Q3Entity* entity = &RequireReference(entityRef);
                const std::string* classname = EntityValue(entity, "classname");
                const std::string* origin = EntityValue(entity, "origin");
                if (classname == nullptr || origin == nullptr)
                {
                    continue;
                }
                using ::MphRead::NativeRuntime::StringStartsWithOrdinalIgnoreCase;
                if (StringStartsWithOrdinalIgnoreCase(*classname, "info_player_deathmatch")
                    || StringStartsWithOrdinalIgnoreCase(*classname, "item_")
                    || StringStartsWithOrdinalIgnoreCase(*classname, "weapon_")
                    || StringStartsWithOrdinalIgnoreCase(*classname, "ammo_"))
                {
                    placed.push_back(ParseVector(*origin));
                }
            }
            if (placed.size() >= 8)
            {
                float playWidest = 0.0F;
                for (std::size_t axis = 0; axis < 3; ++axis)
                {
                    float low = std::numeric_limits<float>::max();
                    float high = std::numeric_limits<float>::lowest();
                    for (const auto& position : placed)
                    {
                        low = MathMin(low, ManagedAt(position.get(), axis));
                        high = MathMax(high, ManagedAt(position.get(), axis));
                    }
                    playWidest = MathMax(playWidest, high - low);
                }
                measured = MathMin(widest, playWidest + 1024.0F);
            }
        }
        const float unit = forcedScale.has_value()
            ? *forcedScale
            : AutoScale(measured);

        std::shared_ptr<std::vector<float>> reachMin;
        std::shared_ptr<std::vector<float>> reachMax;
        Bounds(&RequireReference(bsp), reachMin, reachMax, true);

        const std::string levelName = PathGetFileName(sourceValue);
        const std::string beside
            = PathCombine(directory, levelName);
        const std::string besideFullPath = PathGetFullPath(beside);
        const std::string sourceFullPath = PathGetFullPath(sourceValue);
        if (besideFullPath != sourceFullPath)
        {
            CopyFile(sourceValue, beside);
        }

        const std::string texturePath
            = PathCombine(directory, prefix + ".tex");
        auto archivePaths = std::make_shared<
            std::vector<std::optional<std::string>>>();
        archivePaths->push_back(sourceValue);
        for (const std::string& pak : texturePaks)
        {
            if (!FileExists(pak))
            {
                WriteLine("  no texture pak at ", pak, "; going on without it");
                continue;
            }
            archivePaths->push_back(PathGetFullPath(pak));
        }
        const std::shared_ptr<MapTextureBake::Result> baked
            = MapTextureBake::Bake(
                bsp,
                archivePaths,
                std::optional<std::string>(texturePath),
                textureSize);
        MapTextureBake::Result* bakedValue = &RequireReference(baked);

        WriteLine("  ", ::MphRead::NativeRuntime::ToString(bakedValue->Baked),
            " textures at ", ::MphRead::NativeRuntime::ToString(textureSize), "x",
            ::MphRead::NativeRuntime::ToString(textureSize), " -> ",
            ::MphRead::NativeRuntime::ToString(bakedValue->Bytes, "N0"),
            " B  ", PathGetFileName(texturePath));
        for (const std::string& line : RequireReference(bakedValue->Substituted))
        {
            WriteLine("  stand-in: ", line);
        }
        if (!RequireReference(bakedValue->Missing).empty())
        {
            WriteLine("  no image for ",
                ::MphRead::NativeRuntime::ToString(RequireReference(bakedValue->Missing).size()),
                ": ", JoinStrings(RequireReference(bakedValue->Missing), 6),
                RequireReference(bakedValue->Missing).size() > 6 ? " ..." : "");
            WriteLine("  those surfaces are dropped rather than painted with somebody else's"
                " texture; pass another .pk3 in the same folder if it has them");
        }

        auto definition = std::make_shared<MapDefinition>();
        definition->Name(room);
        definition->InGameName(
            roomName.has_value()
                ? roomName
                : selectedMapName.has_value()
                    ? selectedMapName
                    : std::optional<std::string>(room));
        definition->ScaleFactor(
            ScaleFactor(reachMin.get(), reachMax.get(), unit));
        definition->KillHeight(
            RoundToEven(ManagedAt(min.get(), 2) / unit)
            - 5.0F);
        definition->FarClip(
            RoundToEven(MathMin(
                400.0F,
                widest / unit * 1.2F)));
        // A converted level is drawn with its own sky and no fog: the
        // cartridge's fog colours belong to the cartridge's rooms.
        definition->FogEnabled(false);

        auto import = std::make_shared<MapImport>();
        import->Source(levelName);
        import->MapName(selectedMapName);
        import->UnitsPerUnit(unit);
        import->Textures(PathGetFileName(texturePath));
        import->KeepSky(true);
        import->KeepClip(!dropClip);
        import->KeepSpawns(true);
        {
            std::vector<std::string> paks;
            for (std::size_t i = 1; i < archivePaths->size(); ++i)
            {
                paks.push_back(*(*archivePaths)[i]);
            }
            import->TexturePaks(std::move(paks));
        }
        definition->Import(import);

        std::int32_t clipBrushes = 0;
        for (const std::shared_ptr<Q3Brush>& brushRef
            : RequireReference(bsp).Brushes())
        {
            Q3Brush* brush = &RequireReference(brushRef);
            Q3Texture* solidTexture = &RequireReference(
                ManagedListAt(
                    RequireReference(bsp).Textures(),
                    brush->Texture()));
            if ((solidTexture->Contents()
                    & Q3Bsp::ContentsSolid) != 0)
            {
                continue;
            }
            Q3Texture* clipTexture = &RequireReference(
                ManagedListAt(
                    RequireReference(bsp).Textures(),
                    brush->Texture()));
            if ((clipTexture->Contents()
                    & Q3Bsp::ContentsPlayerClip) != 0)
            {
                if (clipBrushes
                    == std::numeric_limits<std::int32_t>::max())
                {
                    throw System::OverflowException();
                }
                ++clipBrushes;
            }
        }

        AddSpawns(
            &RequireReference(definition),
            &RequireReference(bsp),
            unit);
        AddItems(&RequireReference(definition), &RequireReference(bsp), unit, dropItems);

        const std::string path
            = PathCombine(directory, prefix + ".json");
        definition->Save(path);

        MapDefinition::SpawnList* outputSpawns
            = definition->Spawns();
        if (outputSpawns == nullptr)
        {
            throw System::NullReferenceException();
        }

        WriteLine("  ", ::MphRead::NativeRuntime::ToString(outputSpawns->size()),
            " spawn points, ", ::MphRead::NativeRuntime::ToString(unit, "0.#"),
            " Quake units per unit -> ",
            ::MphRead::NativeRuntime::ToString((ManagedAt(max.get(), 0)
                - ManagedAt(min.get(), 0)) / unit, "0"),
            " x ",
            ::MphRead::NativeRuntime::ToString((ManagedAt(max.get(), 2)
                - ManagedAt(min.get(), 2)) / unit, "0"),
            " x ",
            ::MphRead::NativeRuntime::ToString((ManagedAt(max.get(), 1)
                - ManagedAt(min.get(), 1)) / unit, "0"), " units");
        WriteLine("  wrote ", path);

        // With keepSpawns on, the level's own starts are read at every
        // generation and the recipe lists none, so the recipe is not the
        // count.
        if (RequireReference(definition->Import()).KeepSpawns())
        {
            WriteLine("  the level's own deathmatch starts are its spawns (\"keepSpawns\")");
        }
        else if (outputSpawns->size() < 4)
        {
            WriteLine("  only ", ::MphRead::NativeRuntime::ToString(outputSpawns->size()),
                " places to appear: this level was not"
                " built for a deathmatch. Add spawns to the map file before playing it with a full house.");
        }
        if (clipBrushes > 0 && !dropClip)
        {
            WriteLine("  ", clipBrushes,
                " player-clip brushes kept. They are the level's invisible"
                " walls; on a race map they fence the route. -noclip converts without them.");
        }
        const std::vector<Q3Import::Q3Pickup> pickups = Q3Import::Pickups(bsp.get(), unit);
        const std::size_t itemCount = RequireReference(definition->Items()).size();
        if (dropItems)
        {
            WriteLine("  -noitems: the level's ",
                ::MphRead::NativeRuntime::ToString(pickups.size()),
                " pickups were left out, and \"keepItems\" turned off so they stay out. Add your own under \"items\", from:");
        }
        else if (itemCount > 0)
        {
            WriteLine("  ", ::MphRead::NativeRuntime::ToString(itemCount),
                " of the level's own pickups written under \"items\", and \"keepItems\" turned off so the recipe is the only place they"
                " live. Move them, drop them, or change what they are, from:");
        }
        else
        {
            WriteLine("  no pickups: this level holds none this game has an answer for."
                " Where weapons and powerups go decides how the map plays, so none were"
                " invented. Add them under \"items\", from:");
        }
        WriteLine("  ", JoinMultiplayerItems(MapBuilder::MultiplayerItems));
        const auto scripted = dropItems ? 0 : std::count_if(pickups.begin(), pickups.end(),
            [](const Q3Import::Q3Pickup& p) { return p.TargetName.has_value(); });
        if (scripted > 0)
        {
            WriteLine("  ", ::MphRead::NativeRuntime::ToString(scripted),
                " of them are handed out by the level's own scripts"
                " rather than walked over, and are usually stood in a closet nobody can reach."
                " FruityPrime -mapitems \"", room, "\" says which.");
        }
        WriteLine("  then: FruityPrime -mapgen \"", room, "\"");
        return 0;
    }

    float Q3Convert::AutoScale(float widestExtent)
    {
        return RoundToEven(MathMax(35.0F, widestExtent / TargetExtent));
    }

    float Q3Convert::WidestExtent(Q3Bsp* bsp)
    {
        std::shared_ptr<std::vector<float>> min;
        std::shared_ptr<std::vector<float>> max;
        Bounds(bsp, min, max, false);
        if (ManagedAt(min.get(), 0) > ManagedAt(max.get(), 0))
        {
            return 0;
        }
        return MathMax(ManagedAt(max.get(), 0) - ManagedAt(min.get(), 0),
            MathMax(ManagedAt(max.get(), 1) - ManagedAt(min.get(), 1), ManagedAt(max.get(), 2) - ManagedAt(min.get(), 2)));
    }

    // The level's own pickups, written into the recipe; either way the recipe
    // is now the only source, so keepItems goes off.
    void Q3Convert::AddItems(MapDefinition* definition, Q3Bsp* bsp, float unit, bool dropItems)
    {
        RequireReference(definition->Import()).KeepItems(false);
        if (dropItems)
        {
            return;
        }
        for (const Q3Import::Q3Pickup& pickup : Q3Import::Pickups(bsp, unit))
        {
            auto item = std::make_shared<MapItem>();
            item->Position(std::make_shared<std::vector<float>>(std::initializer_list<float>{
                Round(pickup.Position.X), Round(pickup.Position.Y), Round(pickup.Position.Z)}));
            item->Type(::MphRead::ToString(pickup.Type));
            RequireReference(definition->Items()).push_back(std::move(item));
        }
    }

    void Q3Convert::Bounds(
        Q3Bsp* bsp,
        std::shared_ptr<std::vector<float>>& min,
        std::shared_ptr<std::vector<float>>& max,
        bool sky)
    {
        if (bsp == nullptr)
        {
            throw System::NullReferenceException();
        }

        min = std::make_shared<std::vector<float>>(
            std::initializer_list<float>{
                std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max()});
        max = std::make_shared<std::vector<float>>(
            std::initializer_list<float>{
                std::numeric_limits<float>::lowest(),
                std::numeric_limits<float>::lowest(),
                std::numeric_limits<float>::lowest()});

        for (const std::shared_ptr<Q3Face>& faceRef
            : bsp->Faces())
        {
            Q3Face* face = &RequireReference(faceRef);
            if (face->Type() != 1
                && face->Type() != 2
                && face->Type() != 3)
            {
                continue;
            }

            Q3Texture* texture = &RequireReference(
                ManagedListAt(bsp->Textures(), face->Texture()));
            if ((texture->Flags()
                    & (Q3Bsp::SurfaceNoDraw
                        | Q3Bsp::SurfaceHint
                        | Q3Bsp::SurfaceSkip)) != 0
                || (!sky
                    && (texture->Flags()
                        & Q3Bsp::SurfaceSky) != 0))
            {
                continue;
            }

            for (std::int32_t i = face->Vertex();
                i < UncheckedAdd(
                    face->Vertex(),
                    face->VertexCount());
                i = UncheckedAdd(i, 1))
            {
                Q3Vertex* vertex = &RequireReference(
                    ManagedListAt(bsp->Vertices(), i));
                const std::vector<float>* position
                    = &RequireReference(vertex->Position());
                for (std::size_t axis = 0; axis < 3; ++axis)
                {
                    ManagedAt(min.get(), axis) = MathMin(
                        ManagedAt(min.get(), axis),
                        ManagedAt(position, axis));
                    ManagedAt(max.get(), axis) = MathMax(
                        ManagedAt(max.get(), axis),
                        ManagedAt(position, axis));
                }
            }
        }
    }

    std::int32_t Q3Convert::ScaleFactor(
        const std::vector<float>* min,
        const std::vector<float>* max,
        float unit)
    {
        float reach = 0.0F;
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            reach = MathMax(
                reach,
                MathMax(
                    std::fabs(ManagedAt(min, axis)),
                    std::fabs(ManagedAt(max, axis)))
                    / unit);
        }

        std::int32_t factor = 0;
        while (8.0F
                * std::pow(
                    2.0F,
                    static_cast<float>(factor))
                < reach
            && factor < 10)
        {
            factor = UncheckedAdd(factor, 1);
        }
        return factor;
    }

    void Q3Convert::AddSpawns(
        MapDefinition* definition,
        Q3Bsp* bsp,
        float unit)
    {
        if (definition == nullptr || bsp == nullptr)
        {
            throw System::NullReferenceException();
        }

        std::vector<std::shared_ptr<std::vector<float>>> starts;
        std::vector<std::shared_ptr<std::vector<float>>> fallbacks;

        for (const std::shared_ptr<Q3Entity>& entityRef
            : bsp->Entities())
        {
            const Q3Entity* entity = &RequireReference(entityRef);
            const std::string* classname
                = EntityValue(entity, "classname");
            if (classname == nullptr)
            {
                continue;
            }
            const std::string* origin
                = EntityValue(entity, "origin");
            if (origin == nullptr)
            {
                continue;
            }

            std::shared_ptr<std::vector<float>> position
                = ParseVector(*origin);
            if (::MphRead::NativeRuntime::StringStartsWithOrdinalIgnoreCase(*classname, "info_player_deathmatch")
                || StringEqualsOrdinalIgnoreCase(
                    *classname,
                    "info_player_start"))
            {
                starts.push_back(std::move(position));
            }
            else if (::MphRead::NativeRuntime::StringStartsWithOrdinalIgnoreCase(*classname, "target_")
                || StringEqualsOrdinalIgnoreCase(
                    *classname,
                    "misc_teleporter_dest")
                || StringEqualsOrdinalIgnoreCase(
                    *classname,
                    "info_player_intermission"))
            {
                // Only over ground, and stood on it: a jump pad's target is
                // the top of its arc and an intermission is a camera, and
                // either may hang over the void.
                const std::optional<float> floor = Q3Import::Floor(bsp,
                    ManagedAt(position.get(), 0), ManagedAt(position.get(), 1),
                    ManagedAt(position.get(), 2));
                if (!floor.has_value() || ManagedAt(position.get(), 2) - *floor > 96.0F)
                {
                    continue;
                }
                ManagedAt(position.get(), 2) = *floor + 24.0F;
                fallbacks.push_back(std::move(position));
            }
        }

        std::vector<std::shared_ptr<std::vector<float>>> concatenated;
        const std::vector<std::shared_ptr<std::vector<float>>>* chosen;
        if (starts.size() >= 4)
        {
            chosen = &starts;
        }
        else
        {
            if (starts.size()
                    > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())
                || fallbacks.size()
                    > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())
                        - starts.size())
            {
                throw System::OverflowException();
            }
            const std::size_t count = starts.size() + fallbacks.size();
            concatenated.reserve(count);
            concatenated.insert(
                concatenated.end(),
                starts.begin(),
                starts.end());
            concatenated.insert(
                concatenated.end(),
                fallbacks.begin(),
                fallbacks.end());
            chosen = &concatenated;
        }

        MapImport* import = definition->Import();
        if (import == nullptr)
        {
            throw System::NullReferenceException();
        }
        import->KeepSpawns(starts.size() >= 4);

        import = definition->Import();
        if (import == nullptr)
        {
            throw System::NullReferenceException();
        }
        if (import->KeepSpawns())
        {
            return;
        }

        auto centre = std::make_shared<std::vector<float>>(
            std::initializer_list<float>{
                chosen->empty()
                    ? 0.0F
                    : static_cast<float>(
                        AverageAxis(*chosen, 0)),
                chosen->empty()
                    ? 0.0F
                    : static_cast<float>(
                        AverageAxis(*chosen, 1))});

        for (const std::shared_ptr<std::vector<float>>& positionRef
            : *chosen)
        {
            const std::vector<float>* position
                = &RequireReference(positionRef);
            const float x = ManagedAt(position, 0) / unit;
            const float y = ManagedAt(position, 2) / unit
                - 24.0F / unit;
            const float z = -ManagedAt(position, 1) / unit;
            const float toCentre = std::atan2(
                ManagedAt(centre.get(), 0) / unit - x,
                -ManagedAt(centre.get(), 1) / unit - z);

            auto spawn = std::make_shared<MapSpawn>();
            spawn->Position(
                std::make_shared<std::vector<float>>(
                    std::initializer_list<float>{
                        Round(x),
                        Round(y),
                        Round(z)}));
            spawn->Yaw(
                Round(
                    toCentre
                    * 180.0F
                    / std::numbers::pi_v<float>));

            MapDefinition::SpawnList* spawns
                = definition->Spawns();
            if (spawns == nullptr)
            {
                throw System::NullReferenceException();
            }
            spawns->push_back(std::move(spawn));
        }
    }

    float Q3Convert::Round(float value) noexcept
    {
        constexpr float Power = 100.0F;
        constexpr float RoundLimit = 1.0e8F;
        if (std::fabs(value) < RoundLimit)
        {
            return RoundToEven(value * Power) / Power;
        }
        return value;
    }

    std::shared_ptr<std::vector<float>> Q3Convert::ParseVector(
        const std::string& value)
    {
        auto result = std::make_shared<std::vector<float>>(
            3, 0.0F);

        std::vector<std::string_view> parts;
        std::size_t position = 0;
        while (position < value.size())
        {
            while (position < value.size()
                && value[position] == ' ')
            {
                ++position;
            }
            if (position >= value.size())
            {
                break;
            }
            const std::size_t start = position;
            while (position < value.size()
                && value[position] != ' ')
            {
                ++position;
            }
            parts.emplace_back(
                value.data() + start,
                position - start);
        }

        const std::size_t count
            = std::min<std::size_t>(3, parts.size());
        for (std::size_t i = 0; i < count; ++i)
        {
            float parsed = 0.0F;
            (void)::MphRead::NativeRuntime::SingleTryParseInvariant(parts[i], parsed);
            ManagedAt(result.get(), i) = parsed;
        }
        return result;
    }
}
