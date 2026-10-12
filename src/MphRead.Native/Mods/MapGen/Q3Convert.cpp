#include "Q3Convert.hpp"
#include "Q3Import.hpp"
#include "../../Program.hpp"
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

}

namespace MphRead::Mods::MapGen
{
    std::shared_ptr<MapDefinition> Q3Convert::Translate(
        const std::string& archive,
        const std::string& mapName)
    {
        const std::shared_ptr<Q3Bsp> bsp = Q3Bsp::Load(archive, std::optional<std::string>(mapName));

        std::shared_ptr<std::vector<float>> min;
        std::shared_ptr<std::vector<float>> max;
        Bounds(&RequireReference(bsp), min, max, false);
        if (ManagedAt(min.get(), 0) > ManagedAt(max.get(), 0))
        {
            throw MphRead::ProgramException(mapName + " has no drawn surfaces.");
        }

        const float widest = MathMax(
            ManagedAt(max.get(), 0) - ManagedAt(min.get(), 0),
            MathMax(
                ManagedAt(max.get(), 1) - ManagedAt(min.get(), 1),
                ManagedAt(max.get(), 2) - ManagedAt(min.get(), 2)));
        std::optional<std::string> title;
        for (const std::shared_ptr<Q3Entity>& entityRef : RequireReference(bsp).Entities())
        {
            const Q3Entity* entity = &RequireReference(entityRef);
            const std::string* classname = EntityValue(entity, "classname");
            if (classname != nullptr && StringEqualsOrdinalIgnoreCase(*classname, "worldspawn"))
            {
                if (const std::string* message = EntityValue(entity, "message"))
                {
                    title = Title(*message);
                }
            }
        }
        const float unit = UnitsPerUnit;

        std::shared_ptr<std::vector<float>> reachMin;
        std::shared_ptr<std::vector<float>> reachMax;
        Bounds(&RequireReference(bsp), reachMin, reachMax, true);

        const std::string room = ::MphRead::NativeRuntime::ToUpperInvariant(mapName);
        const std::string prefix = ::MphRead::NativeRuntime::ToLowerInvariant(room);

        auto definition = std::make_shared<MapDefinition>();
        definition->Name(room);
        definition->InGameName(title.has_value() && !title->empty() ? title : std::optional<std::string>(room));
        definition->SourcePath(PathGetFullPath(archive));
        definition->Translated(true);
        definition->ScaleFactor(ScaleFactor(reachMin.get(), reachMax.get(), unit));
        definition->KillHeight(RoundToEven(ManagedAt(min.get(), 2) / unit) - 5.0F);
        definition->FarClip(RoundToEven(MathMin(400.0F, widest / unit * 1.2F)));
        // A level is drawn with its own sky, and fogged only by its own fog:
        // the cartridge's fog colours belong to the cartridge's rooms.
        definition->FogEnabled(false);

        auto import = std::make_shared<MapImport>();
        import->Source(PathGetFullPath(archive));
        import->MapName(mapName);
        import->UnitsPerUnit(unit);
        // The textures are baked beside the room they build, a cache like the
        // rest of it; CustomRooms::Build names the file, the game's folders
        // not being known yet when the maps are first listed.
        import->KeepSky(true);
        import->KeepClip(true);
        import->KeepSpawns(true);
        import->KeepItems(true);
        definition->Import(import);

        AddSpawns(&RequireReference(definition), &RequireReference(bsp), unit);

        std::vector<MapDefinition::Portal> portals;
        for (const Q3Import::Q3Portal& portal : Q3Import::Portals(bsp.get()))
        {
            const OpenTK::Mathematics::Vector3 camera = Q3Import::WorldPoint(portal.Camera, unit);
            const OpenTK::Mathematics::Vector3 aim = Q3Import::WorldPoint(portal.Aim, unit);
            const OpenTK::Mathematics::Vector3 up = Q3Import::WorldPoint(portal.Up, 1.0F);
            portals.push_back(MapDefinition::Portal{
                {camera.X, camera.Y, camera.Z}, {aim.X, aim.Y, aim.Z}, portal.Roll, {up.X, up.Y, up.Z}});
        }
        definition->Portals(std::move(portals));
        return definition;
    }

    // A worldspawn message as a title: Quake's colour codes (^ and a
    // character) and line breaks out.
    std::string Q3Convert::Title(const std::string& message)
    {
        std::string result;
        for (std::size_t i = 0; i < message.size(); ++i)
        {
            const char ch = message[i];
            if (ch == '^' && i + 1 < message.size())
            {
                ++i;
                continue;
            }
            if (ch == '\n' || ch == '\r' || ch == '\\')
            {
                if (!result.empty() && result.back() != ' ')
                {
                    result.push_back(' ');
                }
                if (ch == '\\' && i + 1 < message.size() && message[i + 1] == 'n')
                {
                    ++i;
                }
                continue;
            }
            result.push_back(ch);
        }
        while (!result.empty() && result.back() == ' ')
        {
            result.pop_back();
        }
        return result;
    }

    float Q3Convert::AutoScale(float widestExtent)
    {
        (void)widestExtent;
        return UnitsPerUnit;
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
        if (!starts.empty())
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
        import->KeepSpawns(!starts.empty());

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
