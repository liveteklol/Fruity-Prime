#include "Q3Import.hpp"
#include "NativeRuntime/System/Charconv.hpp"

#include "../../Formats/Formats.hpp"
#include "../../Program.hpp"
#include "../../Read.hpp"
#include "BuiltMap.hpp"
#include "CustomRooms.hpp"
#include "MapBuilder.hpp"
#include "MapDefinition.hpp"
#include "MapTextureBake.hpp"
#include "MapTexturePack.hpp"
#include "Q3Bsp.hpp"
#include "../../Formats/Types.hpp"
#include "../../NativeRuntime/System/Console.hpp"
#include "../../NativeRuntime/System/Encoding.hpp"
#include "../../NativeRuntime/System/ExceptionText.hpp"
#include "../../NativeRuntime/System/Globalization.hpp"
#include "../../NativeRuntime/System/IO.hpp"
#include "../../NativeRuntime/System/Managed.hpp"
#include "../../NativeRuntime/OpenTK/Mathematics.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

using ::MphRead::NativeRuntime::ConvertToInt32Net9;
using ::MphRead::NativeRuntime::Int32TryParseCurrentCulture;
using ::MphRead::NativeRuntime::ManagedAt;
using ::MphRead::NativeRuntime::ManagedListAt;
using ::MphRead::NativeRuntime::MathMax;
using ::MphRead::NativeRuntime::PathCombine;
using ::MphRead::NativeRuntime::PathFromUtf8;
using ::MphRead::NativeRuntime::PathGetFileName;
using ::MphRead::NativeRuntime::PathToUtf8;
using ::MphRead::NativeRuntime::RequireReference;
using ::MphRead::NativeRuntime::StringEqualsOrdinalIgnoreCase;
using ::MphRead::NativeRuntime::UncheckedAdd;
using ::MphRead::NativeRuntime::UncheckedMultiply;
using ::OpenTK::Mathematics::Add;
using ::OpenTK::Mathematics::ComponentMax;
using ::OpenTK::Mathematics::ComponentMin;
using ::OpenTK::Mathematics::Divide;
using ::OpenTK::Mathematics::LengthSquared;
using ::OpenTK::Mathematics::Multiply;
using ::OpenTK::Mathematics::Subtract;

namespace
{
    namespace Runtime = ::MphRead::NativeRuntime;

    using MphRead::ItemType;
    using MphRead::ManagedArray;
    using MphRead::Mods::MapGen::BuiltFace;
    using MphRead::Mods::MapGen::Q3Entity;
    using MphRead::Mods::MapGen::Q3StringEqual;
    using OpenTK::Mathematics::Vector2;
    using OpenTK::Mathematics::Vector3;
    using OpenTK::Mathematics::Vector4;

    void WriteLine(std::string_view value)
    {
        Runtime::ConsoleWriteLine(value);
    }

    void WriteLine()
    {
        Runtime::ConsoleWriteLine();
    }

    [[noreturn]] void ArrayBounds()
    {
        throw System::IndexOutOfRangeException();
    }

    [[nodiscard]] ManagedArray<Vector3>* FacePoints(BuiltFace* face)
    {
        if (face == nullptr)
        {
            throw System::NullReferenceException();
        }
        auto* points = face->Points();
        if (points == nullptr)
        {
            throw System::NullReferenceException();
        }
        return points;
    }

    [[nodiscard]] ManagedArray<Vector2>* FaceTexcoords(BuiltFace* face)
    {
        if (face == nullptr)
        {
            throw System::NullReferenceException();
        }
        auto* texcoords = face->Texcoords();
        if (texcoords == nullptr)
        {
            throw System::NullReferenceException();
        }
        return texcoords;
    }

    [[nodiscard]] const std::string* EntityValue(
        const Q3Entity* entity, const std::string& key)
    {
        if (entity == nullptr)
        {
            throw System::NullReferenceException();
        }
        const auto iterator = entity->find(key);
        if (iterator == entity->end())
        {
            return nullptr;
        }
        return std::addressof(iterator->second);
    }

    [[nodiscard]] std::size_t RectIndex(
        std::int32_t dimension, std::int32_t a, std::int32_t b)
    {
        if (dimension < 0 || a < 0 || b < 0
            || a >= dimension || b >= dimension)
        {
            ArrayBounds();
        }
        return static_cast<std::size_t>(a)
            * static_cast<std::size_t>(dimension)
            + static_cast<std::size_t>(b);
    }

    // The highest top of a solid world brush straight below (x, y, z), in
    // Quake units -- where something falling there lands. Only the floors:
    // a wall crossed on the way down is not looked for.
    [[nodiscard]] std::optional<float> Q3Floor(
        MphRead::Mods::MapGen::Q3Bsp* bsp, float x, float y, float z)
    {
        using MphRead::Mods::MapGen::Q3Bsp;
        const auto& models = bsp->Models();
        if (models.empty())
        {
            return std::nullopt;
        }
        const auto& world = *models.front();
        std::optional<float> best;
        for (std::int32_t b = world.Brush(); b < world.Brush() + world.BrushCount(); ++b)
        {
            const auto& brush = *bsp->Brushes()[static_cast<std::size_t>(b)];
            const auto& texture = *bsp->Textures()[static_cast<std::size_t>(brush.Texture())];
            if ((texture.Contents() & Q3Bsp::ContentsSolid) == 0)
            {
                continue;
            }
            float low = -1.0e9F;
            float high = 1.0e9F;
            bool inside = true;
            for (std::int32_t s = brush.FirstSide(); s < brush.FirstSide() + brush.SideCount(); ++s)
            {
                const auto& side = *bsp->BrushSides()[static_cast<std::size_t>(s)];
                const auto& plane = *bsp->Planes()[static_cast<std::size_t>(side.Plane())];
                const float across = plane.X() * x + plane.Y() * y;
                if (std::fabs(plane.Z()) < 1.0e-6F)
                {
                    if (across > plane.Distance() + 0.01F)
                    {
                        inside = false;
                        break;
                    }
                }
                else if (plane.Z() > 0.0F)
                {
                    high = std::min(high, (plane.Distance() - across) / plane.Z());
                }
                else
                {
                    low = std::max(low, (plane.Distance() - across) / plane.Z());
                }
            }
            if (inside && low <= high && high <= z + 1.0F && (!best || high > *best))
            {
                best = high;
            }
        }
        return best;
    }
}

namespace MphRead::Mods::MapGen
{
    bool Q3Import::CellKey::operator==(const CellKey& other) const noexcept
    {
        return X == other.X && Y == other.Y && Z == other.Z;
    }

    std::size_t Q3Import::CellKeyHash::operator()(const CellKey& value) const noexcept
    {
        std::size_t hash = std::hash<std::int32_t>{}(value.X);
        hash ^= std::hash<std::int32_t>{}(value.Y)
            + static_cast<std::size_t>(0x9e3779b9U) + (hash << 6U) + (hash >> 2U);
        hash ^= std::hash<std::int32_t>{}(value.Z)
            + static_cast<std::size_t>(0x9e3779b9U) + (hash << 6U) + (hash >> 2U);
        return hash;
    }

    std::shared_ptr<BuiltMap> Q3Import::Build(MapDefinition* def, bool verbose)
    {
        if (def == nullptr)
        {
            throw System::NullReferenceException();
        }

        MapImport* import = def->Import();
        if (import == nullptr)
        {
            throw ProgramException("Map " + def->Name() + " has no import settings.");
        }

        std::optional<std::string> resolved = import->Resolve();
        const std::string source = resolved.has_value()
            ? *resolved
            : import->Source();
        const std::optional<std::string> mapName = import->MapName();
        std::shared_ptr<Q3Bsp> bsp = Q3Bsp::Load(source, mapName);

        auto map = std::make_shared<BuiltMap>(def);
        const float unit = import->UnitsPerUnit();

        std::shared_ptr<MapTexturePack> packOwner = import->LoadTexturePack();
        std::unique_ptr<MapTexturePack> bakedPack;
        MapTexturePack* pack = packOwner.get();
        if (pack == nullptr)
        {
            const std::optional<std::string> baked = BakeTextures(bsp, import, verbose);
            if (baked.has_value())
            {
                bakedPack.reset(new MapTexturePack(MapTexturePack::Load(*baked)));
                pack = bakedPack.get();
            }
        }

        std::vector<std::pair<std::int32_t, std::int32_t>> textureSizes;
        if (pack == nullptr)
        {
            textureSizes = GetTextureSizes(def);
        }
        else
        {
            textureSizes.reserve(pack->Entries().size());
            for (const MapTexturePack::Entry& entry : pack->Entries())
            {
                textureSizes.emplace_back(
                    static_cast<std::int32_t>(entry.Width()),
                    static_cast<std::int32_t>(entry.Height()));
            }
        }

        if (textureSizes.empty())
        {
            const std::optional<std::string>& textures = import->Textures();
            if (!textures.has_value() || textures->empty())
            {
                throw ProgramException(
                    def->Name()
                    + " names no texture pack and maps no shaders onto a shipped "
                    + "room's materials, so it has no materials at all. A bundle cooked "
                    + "without its baked textures is the usual cause.");
            }
            throw ProgramException(
                def->Name() + " has no textures: " + *textures
                + " is not beside its recipe, not in its bundle, and could not be baked from the level.");
        }

        std::int32_t unpainted = 0;

        float skySpan = 100.0F;
        if (!bsp)
        {
            throw System::NullReferenceException();
        }
        if (!bsp->Models().empty())
        {
            Q3Model* model = &RequireReference(bsp->Models().front());
            const std::vector<float>* mins = &RequireReference(model->Mins());
            const std::vector<float>* maxs = &RequireReference(model->Maxs());
            const float spanX = ManagedAt(maxs, 0) - ManagedAt(mins, 0);
            const float spanY = ManagedAt(maxs, 1) - ManagedAt(mins, 1);
            skySpan = MathMax(spanX, spanY) / unit;
        }

        // A sky box baked as six sides (MapTextureBake) is drawn the way
        // Quake draws one -- a box of six images round the level -- instead
        // of the level's sky surfaces with one image tiled over them, which
        // is neither what the level looked like nor sharp enough to look at.
        std::array<std::int32_t, 6> skyMaterials{};
        bool skyBox = pack != nullptr && import->KeepSky();
        for (std::size_t side = 0; skyBox && side < skyMaterials.size(); ++side)
        {
            skyBox = pack->BySourceIndex().TryGetValue(
                MapTextureBake::SkyBoxSourceIndex + static_cast<std::int32_t>(side),
                skyMaterials[side]);
        }
        Vector3 skyMin(
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max());
        Vector3 skyMax(
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest());

        std::int32_t skipped = 0;
        std::int32_t patches = 0;
        std::int32_t patchTriangles = 0;
        Vector3 drawnMin(
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max());
        Vector3 drawnMax(
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest());

        for (const std::shared_ptr<Q3Face>& faceValue : bsp->Faces())
        {
            Q3Face* face = &RequireReference(faceValue);
            if (face->Type() != 1 && face->Type() != 2 && face->Type() != 3)
            {
                skipped = UncheckedAdd(skipped, 1);
                continue;
            }

            Q3Texture* texture = &RequireReference(ManagedListAt(bsp->Textures(), face->Texture()));
            if ((texture->Flags()
                & (Q3Bsp::SurfaceNoDraw | Q3Bsp::SurfaceHint | Q3Bsp::SurfaceSkip)) != 0)
            {
                skipped = UncheckedAdd(skipped, 1);
                continue;
            }

            const bool sky = (texture->Flags() & Q3Bsp::SurfaceSky) != 0;
            if (sky && !import->KeepSky())
            {
                skipped = UncheckedAdd(skipped, 1);
                continue;
            }
            if (sky && skyBox)
            {
                // Only where the box goes: the extent of the level's sky.
                for (std::int32_t i = face->Vertex();
                    i < UncheckedAdd(face->Vertex(), face->VertexCount());
                    i = UncheckedAdd(i, 1))
                {
                    const std::vector<float>& position = RequireReference(
                        RequireReference(ManagedListAt(bsp->Vertices(), i)).Position());
                    const Vector3 point(position[0], position[1], position[2]);
                    skyMin = ComponentMin(skyMin, point);
                    skyMax = ComponentMax(skyMax, point);
                }
                continue;
            }

            std::int32_t material = 0;
            if (pack == nullptr)
            {
                material = MatchMaterial(import, texture->Name().Value());
            }
            else if (!pack->BySourceIndex().TryGetValue(face->Texture(), material))
            {
                unpainted = UncheckedAdd(unpainted, 1);
                continue;
            }

            if (material < 0
                || static_cast<std::size_t>(material) >= textureSizes.size())
            {
                throw System::ArgumentOutOfRangeException();
            }
            const auto [width, height]
                = textureSizes[static_cast<std::size_t>(material)];

            const bool patch = face->Type() == 2;
            if (patch)
            {
                patches = UncheckedAdd(patches, 1);
            }

            MphRead::Enumerable<BuiltFace*> builtFaces = patch
                ? Tessellate(
                    bsp.get(), face, unit, width, height,
                    material, sky, import->PatchLevel())
                : Triangles(
                    bsp.get(), face, unit, width, height,
                    material, sky);

            for (BuiltFace* built : builtFaces)
            {
                std::unique_ptr<BuiltFace> builtOwner(built);
                built = map->OwnFace(std::move(builtOwner));
                if (sky)
                {
                    ProjectSky(
                        built,
                        static_cast<float>(width) * SkyTiles
                            / MathMax(1.0F, skySpan));
                    built->Sky = true;
                }

                map->Faces().push_back(built);
                if (patch)
                {
                    patchTriangles = UncheckedAdd(patchTriangles, 1);
                }

                if (!sky)
                {
                    ManagedArray<Vector3>* points = FacePoints(built);
                    for (std::size_t i = 0; i < points->Length(); ++i)
                    {
                        drawnMin = ComponentMin(drawnMin, (*points)[i]);
                        drawnMax = ComponentMax(drawnMax, (*points)[i]);
                    }
                }

                if (patch
                    && (texture->Contents() & Q3Bsp::ContentsSolid) != 0)
                {
                    map->Solid().push_back(built);
                }
            }
        }

        if (skyBox && skyMin.X <= skyMax.X)
        {
            AddSkyBox(map.get(), skyMin, skyMax, unit, skyMaterials, textureSizes);
        }

        const Vector3 margin(6.0F, 6.0F, 6.0F);
        const Vector3 keepMin = Subtract(drawnMin, margin);
        const Vector3 keepMax = Add(drawnMax, margin);
        std::int32_t solidBrushes = 0;
        std::int32_t shellBrushes = 0;
        std::int32_t clipBrushes = 0;
        std::vector<std::vector<Vector4>> brushPlanes;
        std::vector<BrushBounds> brushBounds;
        std::vector<BrushSideEntry> sides;

        std::int32_t firstBrush = 0;
        std::int32_t brushCount = static_cast<std::int32_t>(bsp->Brushes().size());
        if (!bsp->Models().empty())
        {
            Q3Model* levelModel = &RequireReference(bsp->Models().front());
            firstBrush = levelModel->Brush();
            brushCount = levelModel->BrushCount();
        }

        const std::int32_t brushEnd = UncheckedAdd(firstBrush, brushCount);
        for (std::int32_t brushIndex = firstBrush;
            brushIndex < brushEnd;
            brushIndex = UncheckedAdd(brushIndex, 1))
        {
            Q3Brush* brush = &RequireReference(ManagedListAt(bsp->Brushes(), brushIndex));
            Q3Texture* texture = &RequireReference(ManagedListAt(bsp->Textures(), brush->Texture()));

            const bool solid = (texture->Contents() & Q3Bsp::ContentsSolid) != 0;
            const bool clip = (texture->Contents() & Q3Bsp::ContentsPlayerClip) != 0;
            if (!solid && clip && !import->KeepClip())
            {
                clipBrushes = UncheckedAdd(clipBrushes, 1);
                continue;
            }
            if (!solid && !clip)
            {
                continue;
            }
            if (IsSky(bsp.get(), brush))
            {
                shellBrushes = UncheckedAdd(shellBrushes, 1);
                continue;
            }

            std::vector<BrushPolygon> polygons = BrushSides(bsp.get(), brush);
            if (polygons.empty())
            {
                continue;
            }

            bool allOutside = true;
            for (const BrushPolygon& polygon : polygons)
            {
                for (const Vector3 point : polygon.Points)
                {
                    if (Inside(ToWorld(point, unit), keepMin, keepMax))
                    {
                        allOutside = false;
                        break;
                    }
                }
                if (!allOutside)
                {
                    break;
                }
            }
            if (allOutside)
            {
                shellBrushes = UncheckedAdd(shellBrushes, 1);
                continue;
            }

            solidBrushes = UncheckedAdd(solidBrushes, 1);
            if (brush->SideCount() < 0)
            {
                throw System::OverflowException();
            }
            std::vector<Vector4> planes(
                static_cast<std::size_t>(brush->SideCount()));
            for (std::int32_t i = 0; i < brush->SideCount(); ++i)
            {
                const std::int32_t sideIndex = UncheckedAdd(brush->FirstSide(), i);
                Q3BrushSide* side = &RequireReference(ManagedListAt(bsp->BrushSides(), sideIndex));
                Q3Plane* plane = &RequireReference(ManagedListAt(bsp->Planes(), side->Plane()));
                planes[static_cast<std::size_t>(i)] = Vector4(
                    plane->X(), plane->Y(), plane->Z(), plane->Distance());
            }

            Vector3 brushMin(
                std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max());
            Vector3 brushMax(
                std::numeric_limits<float>::lowest(),
                std::numeric_limits<float>::lowest(),
                std::numeric_limits<float>::lowest());
            for (const BrushPolygon& polygon : polygons)
            {
                for (const Vector3 point : polygon.Points)
                {
                    brushMin = ComponentMin(brushMin, point);
                    brushMax = ComponentMax(brushMax, point);
                }
            }

            const std::int32_t index
                = static_cast<std::int32_t>(brushPlanes.size());
            brushPlanes.push_back(std::move(planes));
            brushBounds.push_back(BrushBounds{brushMin, brushMax});
            for (const BrushPolygon& polygon : polygons)
            {
                sides.push_back(
                    BrushSideEntry{index, polygon.Points, polygon.Normal});
            }
        }

        const BrushLookup lookup = BuildBrushLookup(brushBounds);
        std::int32_t buried = 0;
        for (const BrushSideEntry& side : sides)
        {
            if (IsBuried(
                side.Points, side.Normal, side.Brush,
                brushPlanes, brushBounds, lookup))
            {
                buried = UncheckedAdd(buried, 1);
                continue;
            }

            auto world = std::make_unique<ManagedArray<Vector3>>(side.Points.size());
            auto texcoords = std::make_unique<ManagedArray<Vector2>>(side.Points.size());
            for (std::size_t i = 0; i < side.Points.size(); ++i)
            {
                (*world)[i] = ToWorld(side.Points[i], unit);
            }
            const std::vector<float> direction{
                side.Normal.X, side.Normal.Y, side.Normal.Z};
            auto face = std::make_unique<BuiltFace>(
                std::move(world),
                std::move(texcoords),
                ToDirection(std::addressof(direction)),
                0,
                1.0F);
            map->Solid().push_back(map->OwnFace(std::move(face)));
        }

        AddEntities(map.get(), def, bsp.get(), import, verbose);

        if (def->Preview() == nullptr)
        {
            // Over where play happens, looking across it from inside: the
            // drawn bounds take in the sky box, and a camera outside that
            // photographs the lid of the sky.
            Vector3 low(
                std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max());
            Vector3 high(
                std::numeric_limits<float>::lowest(),
                std::numeric_limits<float>::lowest(),
                std::numeric_limits<float>::lowest());
            std::int32_t placed = 0;
            const auto take = [&](const std::vector<float>* position)
            {
                if (position != nullptr && position->size() >= 3)
                {
                    const Vector3 point((*position)[0], (*position)[1], (*position)[2]);
                    low = ComponentMin(low, point);
                    high = ComponentMax(high, point);
                    ++placed;
                }
            };
            for (const auto& spawn : RequireReference(def->Spawns()))
            {
                take(RequireReference(spawn).Position());
            }
            for (const auto& item : RequireReference(def->Items()))
            {
                take(RequireReference(item).Position());
            }
            for (const auto& pad : RequireReference(def->JumpPads()))
            {
                take(RequireReference(pad).Position());
            }
            if (placed < 4)
            {
                Bounds(map.get(), low, high);
            }
            const Vector3 centre = Divide(Add(low, high), 2.0F);
            const Vector3 size = Subtract(high, low);
            const float across = MathMax(size.X, size.Z);

            auto preview = std::make_shared<MapPreview>();
            preview->Position(std::make_shared<std::vector<float>>(
                std::initializer_list<float>{
                    centre.X + size.X * 0.55F,
                    high.Y + across * 0.25F,
                    centre.Z + size.Z * 0.55F}));
            preview->Target(std::make_shared<std::vector<float>>(
                std::initializer_list<float>{
                    centre.X, low.Y, centre.Z}));
            def->Preview(preview);

            if (verbose)
            {
                WriteLine("  suggested preview (paste into the map file to keep it):");
                const std::vector<float>* position = preview->Position();
                const std::vector<float>* target = preview->Target();
                WriteLine("  \"preview\": { \"position\": ["
                    + Runtime::ToString(ManagedAt(position, 0), "0.#") + ", "
                    + Runtime::ToString(ManagedAt(position, 1), "0.#") + ", "
                    + Runtime::ToString(ManagedAt(position, 2), "0.#")
                    + "], \"target\": ["
                    + Runtime::ToString(ManagedAt(target, 0), "0.#") + ", "
                    + Runtime::ToString(ManagedAt(target, 1), "0.#") + ", "
                    + Runtime::ToString(ManagedAt(target, 2), "0.#")
                    + "] },");
            }
        }

        if (verbose)
        {
            if (pack != nullptr)
            {
                std::string line = "  "
                    + Runtime::ToString(static_cast<std::int32_t>(pack->Entries().size()))
                    + " baked textures";
                if (unpainted > 0)
                {
                    line += ", " + Runtime::ToString(unpainted)
                        + " surfaces dropped for want of one";
                }
                WriteLine(line);
            }

            WriteLine("  imported "
                + Runtime::ToString(static_cast<std::int32_t>(bsp->Faces().size()))
                + " surfaces -> "
                + Runtime::ToString(static_cast<std::int32_t>(map->Faces().size()))
                + " triangles (" + Runtime::ToString(patches)
                + " patches tessellated to " + Runtime::ToString(patchTriangles)
                + ", " + Runtime::ToString(skipped)
                + " non-drawing surfaces skipped)");

            std::string line = "  " + Runtime::ToString(solidBrushes)
                + " solid brushes -> "
                + Runtime::ToString(static_cast<std::int32_t>(map->Solid().size()))
                + " collision faces (" + Runtime::ToString(shellBrushes)
                + " shell brushes outside the level left out, "
                + Runtime::ToString(buried) + " sides buried inside other brushes";
            if (clipBrushes > 0)
            {
                line += ", " + Runtime::ToString(clipBrushes)
                    + " invisible-wall brushes dropped";
            }
            line += ')';
            WriteLine(line);

            Vector3 min;
            Vector3 max;
            Bounds(map.get(), min, max);
            WriteLine("  extent " + Runtime::ToString(max.X - min.X, "0.0")
                + " x " + Runtime::ToString(max.Y - min.Y, "0.0")
                + " x " + Runtime::ToString(max.Z - min.Z, "0.0")
                + " units at " + Runtime::ToString(unit, "0.#")
                + " Quake units each");
        }

        return map;
    }

    MphRead::Enumerable<BuiltFace*> Q3Import::Triangles(
        Q3Bsp* bsp,
        Q3Face* face,
        float unit,
        std::int32_t width,
        std::int32_t height,
        std::int32_t material,
        bool sky)
    {
        return MphRead::Enumerable<BuiltFace*>(
            [bsp, face, unit, width, height, material, sky]()
                -> MphRead::NativeRuntime::CoroutineSequence<BuiltFace*>
            {
                if (face == nullptr || bsp == nullptr)
                {
                    throw System::NullReferenceException();
                }

                for (std::int32_t i = 0;
                    UncheckedAdd(i, 2) < face->MeshVertCount();
                    i = UncheckedAdd(i, 3))
                {
                    std::vector<Vector3> points(3);
                    std::vector<Vector2> uvs(3);
                    Vector3 vertexNormal(0.0F, 0.0F, 0.0F);
                    float shade = 0.0F;

                    for (std::int32_t j = 0; j < 3; ++j)
                    {
                        const std::int32_t meshIndex = UncheckedAdd(
                            UncheckedAdd(face->MeshVert(), i), j);
                        const std::int32_t vertexOffset
                            = ManagedListAt(bsp->MeshVerts(), meshIndex);
                        const std::int32_t vertexIndex
                            = UncheckedAdd(face->Vertex(), vertexOffset);
                        Q3Vertex* vertex
                            = &RequireReference(ManagedListAt(bsp->Vertices(), vertexIndex));

                        points[static_cast<std::size_t>(j)]
                            = ToWorld(&RequireReference(vertex->Position()), unit);
                        const std::vector<float>* surface
                            = &RequireReference(vertex->Surface());
                        uvs[static_cast<std::size_t>(j)] = Vector2(
                            ManagedAt(surface, 0) * static_cast<float>(width),
                            ManagedAt(surface, 1) * static_cast<float>(height));

                        vertexNormal = Add(
                            vertexNormal,
                            ToDirection(&RequireReference(vertex->Normal())));

                        const std::vector<std::uint8_t>* color
                            = &RequireReference(vertex->Color());
                        const std::int32_t colorSum
                            = static_cast<std::int32_t>(ManagedAt(color, 0))
                            + static_cast<std::int32_t>(ManagedAt(color, 1))
                            + static_cast<std::int32_t>(ManagedAt(color, 2));
                        shade += static_cast<float>(colorSum)
                            / (3.0F * 255.0F);
                    }

                    const Vector3 normal = TriangleNormal(points, vertexNormal,
                        ToDirection(&RequireReference(face->Normal())));
                    co_yield MakeFace(
                        std::move(points),
                        std::move(uvs),
                        normal,
                        material,
                        Shade(shade / 3.0F, sky));
                }
            });
    }

    MphRead::Enumerable<BuiltFace*> Q3Import::Tessellate(
        Q3Bsp* bsp,
        Q3Face* face,
        float unit,
        std::int32_t width,
        std::int32_t height,
        std::int32_t material,
        bool sky,
        std::int32_t level)
    {
        return MphRead::Enumerable<BuiltFace*>(
            [bsp, face, unit, width, height, material, sky, level]()
                -> MphRead::NativeRuntime::CoroutineSequence<BuiltFace*>
            {
                if (face == nullptr || bsp == nullptr)
                {
                    throw System::NullReferenceException();
                }

                const std::vector<std::int32_t>* size = &RequireReference(face->Size());
                const std::int32_t w = ManagedAt(size, 0);
                const std::int32_t h = ManagedAt(size, 1);
                if (w < 3 || h < 3 || w % 2 == 0 || h % 2 == 0)
                {
                    co_return;
                }

                const std::int32_t tessLevel = std::clamp(level, 1, 8);
                const std::int32_t dimension = tessLevel + 1;
                const std::size_t valueCount
                    = static_cast<std::size_t>(dimension)
                    * static_cast<std::size_t>(dimension);

                for (std::int32_t py = 0; UncheckedAdd(py, 2) < h; py = UncheckedAdd(py, 2))
                {
                    for (std::int32_t px = 0; UncheckedAdd(px, 2) < w; px = UncheckedAdd(px, 2))
                    {
                        std::vector<Vector3> points(valueCount);
                        std::vector<Vector2> uvs(valueCount);
                        std::vector<Vector3> normals(valueCount);
                        std::vector<float> shades(valueCount);

                        for (std::int32_t a = 0; a <= tessLevel; ++a)
                        {
                            const float v
                                = static_cast<float>(a)
                                / static_cast<float>(tessLevel);
                            const auto [v0, v1, v2] = Weights(v);

                            for (std::int32_t b = 0; b <= tessLevel; ++b)
                            {
                                const float u
                                    = static_cast<float>(b)
                                    / static_cast<float>(tessLevel);
                                const auto [u0, u1, u2] = Weights(u);

                                Vector3 position(0.0F, 0.0F, 0.0F);
                                Vector2 uv(0.0F, 0.0F);
                                Vector3 normal(0.0F, 0.0F, 0.0F);
                                float shade = 0.0F;

                                for (std::int32_t r = 0; r < 3; ++r)
                                {
                                    const float rw
                                        = r == 0 ? v0 : r == 1 ? v1 : v2;
                                    for (std::int32_t c = 0; c < 3; ++c)
                                    {
                                        const float cw
                                            = c == 0 ? u0 : c == 1 ? u1 : u2;
                                        const float weight = rw * cw;

                                        const std::int32_t row
                                            = UncheckedAdd(py, r);
                                        const std::int32_t rowOffset
                                            = UncheckedMultiply(row, w);
                                        const std::int32_t controlOffset
                                            = UncheckedAdd(
                                                UncheckedAdd(rowOffset, px), c);
                                        const std::int32_t vertexIndex
                                            = UncheckedAdd(
                                                face->Vertex(),
                                                controlOffset);
                                        Q3Vertex* vertex = &RequireReference(
                                            ManagedListAt(
                                                bsp->Vertices(),
                                                vertexIndex));

                                        position = Add(
                                            position,
                                            Multiply(
                                                ToWorld(
                                                    &RequireReference(vertex->Position()),
                                                    unit),
                                                weight));

                                        const std::vector<float>* surface
                                            = &RequireReference(vertex->Surface());
                                        uv = Add(
                                            uv,
                                            Multiply(
                                                Vector2(
                                                    ManagedAt(surface, 0)
                                                        * static_cast<float>(width),
                                                    ManagedAt(surface, 1)
                                                        * static_cast<float>(height)),
                                                weight));

                                        normal = Add(
                                            normal,
                                            Multiply(
                                                ToDirection(
                                                    &RequireReference(vertex->Normal())),
                                                weight));

                                        const std::vector<std::uint8_t>* color
                                            = &RequireReference(vertex->Color());
                                        const std::int32_t colorSum
                                            = static_cast<std::int32_t>(
                                                ManagedAt(color, 0))
                                            + static_cast<std::int32_t>(
                                                ManagedAt(color, 1))
                                            + static_cast<std::int32_t>(
                                                ManagedAt(color, 2));
                                        shade += (
                                            static_cast<float>(colorSum)
                                            / (3.0F * 255.0F))
                                            * weight;
                                    }
                                }

                                const std::size_t index
                                    = RectIndex(dimension, a, b);
                                points[index] = position;
                                uvs[index] = uv;
                                normals[index] = LengthSquared(normal) < 0.0001F
                                    ? ToDirection(&RequireReference(face->Normal()))
                                    : normal.Normalized();
                                shades[index] = shade;
                            }
                        }

                        for (std::int32_t a = 0; a < tessLevel; ++a)
                        {
                            for (std::int32_t b = 0; b < tessLevel; ++b)
                            {
                                co_yield Cell(
                                    points, uvs, normals, shades,
                                    dimension, material, sky,
                                    GridPoint{a, b},
                                    GridPoint{a, b + 1},
                                    GridPoint{a + 1, b + 1});
                                co_yield Cell(
                                    points, uvs, normals, shades,
                                    dimension, material, sky,
                                    GridPoint{a, b},
                                    GridPoint{a + 1, b + 1},
                                    GridPoint{a + 1, b});
                            }
                        }
                    }
                }
            });
    }

    std::tuple<float, float, float> Q3Import::Weights(float t) noexcept
    {
        const float inverse = 1.0F - t;
        return std::tuple<float, float, float>(
            inverse * inverse,
            2.0F * t * inverse,
            t * t);
    }

    // Which way a drawn triangle faces: its own plane, not the average of its
    // vertices' normals, which for a welded model mesh belong to no single
    // triangle. Quake winds a front face clockwise, so the cross product is
    // negated; the vertex normals break a tie the other way and are the
    // fallback for a degenerate triangle, then the lump's own face normal.
    Vector3 Q3Import::TriangleNormal(const std::vector<Vector3>& points, Vector3 vertexNormal, Vector3 faceNormal)
    {
        Vector3 geometric = Vector3::Cross(Subtract(points.at(1), points.at(0)), Subtract(points.at(2), points.at(0)));
        if (LengthSquared(geometric) >= 1e-10F)
        {
            geometric = Negate(geometric.Normalized());
            if (LengthSquared(vertexNormal) >= 0.0001F
                && Vector3::Dot(geometric, vertexNormal.Normalized()) < -0.2F)
            {
                geometric = Negate(geometric);
            }
            return geometric;
        }
        if (LengthSquared(vertexNormal) >= 0.0001F)
        {
            return vertexNormal.Normalized();
        }
        return LengthSquared(faceNormal) >= 0.0001F ? faceNormal.Normalized() : Vector3(0.0F, 1.0F, 0.0F);
    }

    BuiltFace* Q3Import::Cell(
        const std::vector<Vector3>& points,
        const std::vector<Vector2>& uvs,
        const std::vector<Vector3>& normals,
        const std::vector<float>& shades,
        std::int32_t dimension,
        std::int32_t material,
        bool sky,
        GridPoint p0,
        GridPoint p1,
        GridPoint p2)
    {
        const std::size_t i0 = RectIndex(dimension, p0.A, p0.B);
        const std::size_t i1 = RectIndex(dimension, p1.A, p1.B);
        const std::size_t i2 = RectIndex(dimension, p2.A, p2.B);

        std::vector<Vector3> corners{
            points.at(i0), points.at(i1), points.at(i2)};
        std::vector<Vector2> texcoords{
            uvs.at(i0), uvs.at(i1), uvs.at(i2)};

        Vector3 normal = Add(
            Add(normals.at(i0), normals.at(i1)),
            normals.at(i2));
        normal = LengthSquared(normal) < 0.0001F
            ? Vector3(0.0F, 1.0F, 0.0F)
            : normal.Normalized();
        // The cell's own plane, pointed the way the interpolated normals say:
        // a tessellated patch is collision as well as geometry.
        Vector3 plane = Vector3::Cross(Subtract(corners.at(1), corners.at(0)), Subtract(corners.at(2), corners.at(0)));
        if (LengthSquared(plane) >= 1e-10F)
        {
            plane = plane.Normalized();
            normal = Vector3::Dot(plane, normal) < 0 ? Negate(plane) : plane;
        }

        const float shade
            = (shades.at(i0) + shades.at(i1) + shades.at(i2))
            / 3.0F;
        return MakeFace(
            std::move(corners),
            std::move(texcoords),
            normal,
            material,
            Shade(shade, sky));
    }

    float Q3Import::Shade(float baked, bool sky) noexcept
    {
        return sky
            ? 1.0F
            : std::clamp(0.62F + baked * 0.75F, 0.55F, 1.0F);
    }

    BuiltFace* Q3Import::MakeFace(
        std::vector<Vector3> points,
        std::vector<Vector2> uvs,
        Vector3 normal,
        std::int32_t material,
        float shade)
    {
        const Vector3 point1 = ManagedAt(&points, 1);
        const Vector3 point0a = ManagedAt(&points, 0);
        const Vector3 point2 = ManagedAt(&points, 2);
        const Vector3 point0b = ManagedAt(&points, 0);
        const Vector3 wound = Vector3::Cross(
            Subtract(point1, point0a),
            Subtract(point2, point0b));
        if (Vector3::Dot(wound, normal) < 0.0F)
        {
            (void)ManagedAt(&points, 1);
            (void)ManagedAt(&points, 2);
            (void)ManagedAt(&uvs, 1);
            (void)ManagedAt(&uvs, 2);
            std::swap(points[1], points[2]);
            std::swap(uvs[1], uvs[2]);
        }

        auto pointArray = std::make_unique<ManagedArray<Vector3>>(points.size());
        auto uvArray = std::make_unique<ManagedArray<Vector2>>(uvs.size());
        for (std::size_t i = 0; i < points.size(); ++i)
        {
            (*pointArray)[i] = points[i];
        }
        for (std::size_t i = 0; i < uvs.size(); ++i)
        {
            (*uvArray)[i] = uvs[i];
        }

        return new BuiltFace(
            std::move(pointArray),
            std::move(uvArray),
            normal,
            material,
            shade);
    }

    void Q3Import::AddSkyBox(
        BuiltMap* map,
        Vector3 min,
        Vector3 max,
        float unit,
        const std::array<std::int32_t, 6>& materials,
        const std::vector<std::pair<std::int32_t, std::int32_t>>& textureSizes)
    {
        // tr_sky.c's st_to_vec: for each side, where each of the three
        // axes takes its value from -- 1 is s, 2 is t, 3 the side's own
        // depth, negative for the opposite sign. Its texture coordinates are
        // ((s + 1) / 2, 1 - (t + 1) / 2).
        static constexpr std::array<std::array<std::int32_t, 3>, 6> StToVec{{
            {{3, -1, 2}}, {{-3, 1, 2}}, {{1, 3, 2}}, {{-1, -3, 2}}, {{-2, -1, 3}}, {{2, -1, -3}}
        }};
        static constexpr std::array<std::array<float, 2>, 4> Corners{{
            {{-1.0F, -1.0F}}, {{1.0F, -1.0F}}, {{1.0F, 1.0F}}, {{-1.0F, 1.0F}}
        }};
        const Vector3 centre = Divide(Add(min, max), 2.0F);
        const Vector3 half = Divide(Subtract(max, min), 2.0F);
        const Vector3 centreWorld = ToWorld(centre, unit);
        for (std::size_t side = 0; side < StToVec.size(); ++side)
        {
            const std::int32_t material = materials[side];
            if (material < 0 || static_cast<std::size_t>(material) >= textureSizes.size())
            {
                throw System::ArgumentOutOfRangeException();
            }
            const auto [width, height] = textureSizes[static_cast<std::size_t>(material)];
            std::vector<Vector3> points;
            std::vector<Vector2> texcoords;
            for (const auto& [s, t] : Corners)
            {
                const float source[3] = {s, t, 1.0F};
                float axis[3]{};
                for (std::size_t j = 0; j < 3; ++j)
                {
                    const std::int32_t k = StToVec[side][j];
                    axis[j] = k < 0 ? -source[-k - 1] : source[k - 1];
                }
                const Vector3 q3(
                    centre.X + axis[0] * half.X,
                    centre.Y + axis[1] * half.Y,
                    centre.Z + axis[2] * half.Z);
                points.push_back(ToWorld(q3, unit));
                texcoords.emplace_back(
                    (s + 1.0F) * 0.5F * static_cast<float>(width),
                    (1.0F - (t + 1.0F) * 0.5F) * static_cast<float>(height));
            }
            // Seen from inside: wound counter-clockwise towards the centre.
            const Vector3 faceCentre = Divide(
                Add(Add(points[0], points[1]), Add(points[2], points[3])), 4.0F);
            const Vector3 inward = Subtract(centreWorld, faceCentre).Normalized();
            const Vector3 winding = Vector3::Cross(
                Subtract(points[1], points[0]), Subtract(points[2], points[0]));
            if (Vector3::Dot(winding, inward) < 0.0F)
            {
                std::reverse(points.begin(), points.end());
                std::reverse(texcoords.begin(), texcoords.end());
            }
            auto pointArray = std::make_unique<ManagedArray<Vector3>>(points.size());
            auto texcoordArray = std::make_unique<ManagedArray<Vector2>>(texcoords.size());
            for (std::size_t i = 0; i < points.size(); ++i)
            {
                (*pointArray)[i] = points[i];
                (*texcoordArray)[i] = texcoords[i];
            }
            auto face = std::make_unique<BuiltFace>(
                std::move(pointArray), std::move(texcoordArray), inward, material, Shade(1.0F, true));
            face->Sky = true;
            map->Faces().push_back(map->OwnFace(std::move(face)));
        }
    }

    void Q3Import::ProjectSky(BuiltFace* face, float texelsPerUnit)
    {
        ManagedArray<Vector3>* points = FacePoints(face);
        ManagedArray<Vector2>* texcoords = FaceTexcoords(face);
        const Vector3 origin = (*points)[0];
        const Vector3 normal = face->Normal();
        const float ax = std::fabs(normal.X);
        const float ay = std::fabs(normal.Y);
        const float az = std::fabs(normal.Z);

        for (std::size_t i = 0; i < points->Length(); ++i)
        {
            const Vector3 offset = Multiply(
                Subtract((*points)[i], origin),
                texelsPerUnit);
            (*texcoords)[i] = ay > ax && ay >= az
                ? Vector2(offset.X, offset.Z)
                : ax >= az
                    ? Vector2(offset.Z, -offset.Y)
                    : Vector2(offset.X, -offset.Y);
        }
    }

    std::optional<std::string> Q3Import::BakeTextures(
        const std::shared_ptr<Q3Bsp>& bsp,
        MapImport* import,
        bool verbose)
    {
        if (import == nullptr)
        {
            throw System::NullReferenceException();
        }

        const std::optional<std::string> level = import->Resolve();
        const std::optional<std::string>& textures = import->Textures();
        if (!textures.has_value() || textures->empty() || !level.has_value())
        {
            return std::nullopt;
        }

        const std::optional<std::string>& baseDirectoryValue
            = import->BaseDirectory();
        const std::string& baseDirectory = baseDirectoryValue.has_value()
            ? *baseDirectoryValue
            : CustomRooms::MapDirectory();
        const std::string target = PathCombine(baseDirectory, *textures);

        try
        {
            auto archivePaths = std::make_shared<
                std::vector<std::optional<std::string>>>();
            archivePaths->push_back(*level);
            for (std::string& pak : import->ResolveTexturePaks())
            {
                archivePaths->push_back(std::move(pak));
            }

            std::shared_ptr<MapTextureBake::Result> result
                = MapTextureBake::Bake(
                    bsp,
                    archivePaths,
                    std::optional<std::string>(target),
                    MapTextureBake::DefaultSize,
                    true,
                    &import->TextureAliases());
            if (!result)
            {
                throw System::NullReferenceException();
            }

            if (result->Baked == 0)
            {
                std::filesystem::remove(PathFromUtf8(target));
                return std::nullopt;
            }

            if (verbose)
            {
                WriteLine("  baked " + Runtime::ToString(result->Baked)
                    + " textures from " + PathGetFileName(*level)
                    + " -> " + PathGetFileName(target));
            }
            return target;
        }
        catch (...)
        {
            WriteLine("[mapgen] could not bake textures from "
                + PathGetFileName(*level) + ": "
                + Runtime::ExceptionMessage(std::current_exception()));
            return std::nullopt;
        }
    }

    bool Q3Import::Inside(
        Vector3 point, Vector3 min, Vector3 max) noexcept
    {
        return point.X >= min.X && point.X <= max.X
            && point.Y >= min.Y && point.Y <= max.Y
            && point.Z >= min.Z && point.Z <= max.Z;
    }

    bool Q3Import::IsSky(Q3Bsp* bsp, Q3Brush* brush)
    {
        if (bsp == nullptr || brush == nullptr)
        {
            throw System::NullReferenceException();
        }

        for (std::int32_t i = 0; i < brush->SideCount(); ++i)
        {
            const std::int32_t sideIndex
                = UncheckedAdd(brush->FirstSide(), i);
            Q3BrushSide* side
                = &RequireReference(ManagedListAt(bsp->BrushSides(), sideIndex));
            Q3Texture* texture
                = &RequireReference(ManagedListAt(bsp->Textures(), side->Texture()));
            if ((texture->Flags() & Q3Bsp::SurfaceSky) == 0)
            {
                return false;
            }
        }
        return brush->SideCount() > 0;
    }

    void Q3Import::Bounds(BuiltMap* map, Vector3& min, Vector3& max)
    {
        if (map == nullptr)
        {
            throw System::NullReferenceException();
        }

        min = Vector3(
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max());
        max = Vector3(
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest());

        const auto accumulate = [&min, &max](const std::vector<BuiltFace*>& faces)
        {
            for (BuiltFace* face : faces)
            {
                ManagedArray<Vector3>* points = FacePoints(face);
                for (std::size_t i = 0; i < points->Length(); ++i)
                {
                    min = ComponentMin(min, (*points)[i]);
                    max = ComponentMax(max, (*points)[i]);
                }
            }
        };
        accumulate(map->Faces());
        accumulate(map->Solid());
    }

    std::vector<Vector2> Q3Import::Rebase(const std::vector<Vector2>& uvs)
    {
        if (uvs.empty())
        {
            throw System::InvalidOperationException("Sequence contains no elements");
        }

        float minU = uvs.front().X;
        for (std::size_t i = std::isnan(minU) ? uvs.size() : 1;
            i < uvs.size(); ++i)
        {
            if (std::isnan(uvs[i].X))
            {
                minU = uvs[i].X;
                break;
            }
            if (uvs[i].X < minU)
            {
                minU = uvs[i].X;
            }
        }

        float minV = uvs.front().Y;
        for (std::size_t i = std::isnan(minV) ? uvs.size() : 1;
            i < uvs.size(); ++i)
        {
            if (std::isnan(uvs[i].Y))
            {
                minV = uvs[i].Y;
                break;
            }
            if (uvs[i].Y < minV)
            {
                minV = uvs[i].Y;
            }
        }

        const Vector2 offset(std::floor(minU), std::floor(minV));
        std::vector<Vector2> result;
        result.reserve(uvs.size());
        for (const Vector2 uv : uvs)
        {
            result.push_back(Subtract(uv, offset));
        }
        return result;
    }

    std::vector<std::pair<std::int32_t, std::int32_t>>
        Q3Import::GetTextureSizes(MapDefinition* def)
    {
        if (def == nullptr)
        {
            throw System::NullReferenceException();
        }

        std::shared_ptr<ModelInstance> instance
            = Read::GetRoomModelInstance(def->TextureSource());
        ModelInstance* requiredInstance = &RequireReference(instance);
        std::shared_ptr<Model> source = requiredInstance->Model();
        Model* requiredSource = &RequireReference(source);

        if (!requiredSource->Recolors)
        {
            throw System::NullReferenceException();
        }
        Recolor* recolor
            = &RequireReference(ManagedListAt(*requiredSource->Recolors, 0));

        if (!requiredSource->Materials)
        {
            throw System::NullReferenceException();
        }
        if (!recolor->Textures)
        {
            throw System::NullReferenceException();
        }

        MapDefinition::MaterialList* materials = def->Materials();
        if (materials == nullptr)
        {
            throw System::NullReferenceException();
        }

        std::vector<std::pair<std::int32_t, std::int32_t>> results;
        results.reserve(materials->size());
        for (const std::shared_ptr<MapMaterial>& materialValue : *materials)
        {
            MapMaterial* material = &RequireReference(materialValue);
            const std::int32_t sourceMaterialId = material->SourceMaterial();
            Material* sourceMaterial = &RequireReference(
                ManagedListAt(*requiredSource->Materials, sourceMaterialId));
            const Texture& texture
                = ManagedListAt(*recolor->Textures, sourceMaterial->TextureId);
            results.emplace_back(
                static_cast<std::int32_t>(texture.Width),
                static_cast<std::int32_t>(texture.Height));
        }
        return results;
    }

    std::int32_t Q3Import::MatchMaterial(
        MapImport* import, const std::string& shader)
    {
        if (import == nullptr)
        {
            throw System::NullReferenceException();
        }
        MapImport::ShaderMaterialDictionary* materials
            = import->ShaderMaterials();
        if (materials == nullptr)
        {
            throw System::NullReferenceException();
        }

        std::int32_t exact = 0;
        if (materials->TryGetValue(shader, exact))
        {
            return exact;
        }

        std::optional<std::string> best;
        for (const std::string& key : materials->Keys())
        {
            if (::MphRead::NativeRuntime::StringStartsWithOrdinalIgnoreCase(shader, key)
                && (!best.has_value()
                    || Runtime::Utf16Length(key) > Runtime::Utf16Length(*best)))
            {
                best = key;
            }
        }
        return best.has_value()
            ? (*materials)[*best]
            : import->DefaultMaterial();
    }

    std::vector<Q3Import::BrushPolygon> Q3Import::BrushSides(
        Q3Bsp* bsp, Q3Brush* brush)
    {
        if (bsp == nullptr || brush == nullptr)
        {
            throw System::NullReferenceException();
        }

        std::vector<BrushPolygon> result;
        for (std::int32_t i = 0; i < brush->SideCount(); ++i)
        {
            const std::int32_t firstIndex
                = UncheckedAdd(brush->FirstSide(), i);
            Q3BrushSide* firstSide
                = &RequireReference(ManagedListAt(bsp->BrushSides(), firstIndex));
            Q3Plane* plane
                = &RequireReference(ManagedListAt(bsp->Planes(), firstSide->Plane()));
            const Vector3 normal(
                plane->X(), plane->Y(), plane->Z());

            std::vector<Vector3> points
                = MakeSheet(normal, plane->Distance());
            for (std::int32_t j = 0;
                j < brush->SideCount() && points.size() >= 3;
                ++j)
            {
                if (j == i)
                {
                    continue;
                }

                const std::int32_t otherIndex
                    = UncheckedAdd(brush->FirstSide(), j);
                Q3BrushSide* otherSide
                    = &RequireReference(ManagedListAt(bsp->BrushSides(), otherIndex));
                Q3Plane* other
                    = &RequireReference(ManagedListAt(bsp->Planes(), otherSide->Plane()));
                points = Clip(
                    points,
                    Vector3(other->X(), other->Y(), other->Z()),
                    other->Distance());
            }

            if (points.size() < 3)
            {
                continue;
            }
            points = Weld(points);
            if (points.size() < 3)
            {
                continue;
            }
            result.push_back(
                BrushPolygon{std::move(points), normal});
        }
        return result;
    }

    Q3Import::BrushLookup Q3Import::BuildBrushLookup(
        const std::vector<BrushBounds>& bounds)
    {
        BrushLookup lookup;
        for (std::size_t i = 0; i < bounds.size(); ++i)
        {
            const BrushBounds& bound = bounds[i];
            const std::int32_t startX = Cell(bound.Min.X);
            const std::int32_t endX = Cell(bound.Max.X);
            for (std::int32_t x = startX; x <= endX; x = UncheckedAdd(x, 1))
            {
                const std::int32_t startY = Cell(bound.Min.Y);
                const std::int32_t endY = Cell(bound.Max.Y);
                for (std::int32_t y = startY; y <= endY; y = UncheckedAdd(y, 1))
                {
                    const std::int32_t startZ = Cell(bound.Min.Z);
                    const std::int32_t endZ = Cell(bound.Max.Z);
                    for (std::int32_t z = startZ; z <= endZ; z = UncheckedAdd(z, 1))
                    {
                        lookup[CellKey{x, y, z}].push_back(
                            static_cast<std::int32_t>(i));
                    }
                }
            }
        }
        return lookup;
    }

    std::int32_t Q3Import::Cell(float value) noexcept
    {
        return ConvertToInt32Net9(std::floor(value / BrushCellSize));
    }

    bool Q3Import::IsBuried(
        const std::vector<Vector3>& points,
        Vector3 normal,
        std::int32_t owner,
        const std::vector<std::vector<Vector4>>& brushes,
        const std::vector<BrushBounds>& bounds,
        const BrushLookup& lookup)
    {
        Vector3 centre(0.0F, 0.0F, 0.0F);
        for (const Vector3 point : points)
        {
            centre = Add(centre, point);
        }
        centre = Divide(
            centre, static_cast<float>(points.size()));
        const Vector3 offset = normal.Normalized();

        for (std::size_t i = 0; i < points.size(); ++i)
        {
            const Vector3 edge = Divide(
                Add(points[i], points[(i + 1) % points.size()]),
                2.0F);

            const Vector3 cornerSample = Add(
                Add(
                    points[i],
                    Multiply(
                        Subtract(centre, points[i]),
                        0.15F)),
                offset);
            const Vector3 edgeSample = Add(
                Add(
                    edge,
                    Multiply(
                        Subtract(centre, edge),
                        0.15F)),
                offset);

            if (!Covered(
                    cornerSample, owner,
                    brushes, bounds, lookup)
                || !Covered(
                    edgeSample, owner,
                    brushes, bounds, lookup))
            {
                return false;
            }
        }
        return Covered(
            Add(centre, offset),
            owner, brushes, bounds, lookup);
    }

    bool Q3Import::Covered(
        Vector3 point,
        std::int32_t owner,
        const std::vector<std::vector<Vector4>>& brushes,
        const std::vector<BrushBounds>& bounds,
        const BrushLookup& lookup)
    {
        const auto iterator = lookup.find(
            CellKey{Cell(point.X), Cell(point.Y), Cell(point.Z)});
        if (iterator == lookup.end())
        {
            return false;
        }

        for (const std::int32_t index : iterator->second)
        {
            if (index == owner)
            {
                continue;
            }

            const BrushBounds& bound = ManagedListAt(bounds, index);
            if (point.X < bound.Min.X - 1.0F
                || point.X > bound.Max.X + 1.0F
                || point.Y < bound.Min.Y - 1.0F
                || point.Y > bound.Max.Y + 1.0F
                || point.Z < bound.Min.Z - 1.0F
                || point.Z > bound.Max.Z + 1.0F)
            {
                continue;
            }

            bool inside = true;
            const std::vector<Vector4>& planes
                = ManagedListAt(brushes, index);
            for (const Vector4 plane : planes)
            {
                if (plane.X * point.X
                    + plane.Y * point.Y
                    + plane.Z * point.Z
                    - plane.W > -0.1F)
                {
                    inside = false;
                    break;
                }
            }
            if (inside)
            {
                return true;
            }
        }
        return false;
    }

    std::vector<Vector3> Q3Import::MakeSheet(
        Vector3 normal, float distance)
    {
        const Vector3 axis = std::fabs(normal.Z) < 0.9F
            ? Vector3(0.0F, 0.0F, 1.0F)
            : Vector3(1.0F, 0.0F, 0.0F);
        const Vector3 right
            = Vector3::Cross(axis, normal).Normalized();
        const Vector3 up
            = Vector3::Cross(normal, right).Normalized();
        const Vector3 centre = Multiply(normal, distance);
        constexpr float Extent = 65536.0F;

        return std::vector<Vector3>{
            Subtract(Subtract(centre, Multiply(right, Extent)), Multiply(up, Extent)),
            Subtract(Add(centre, Multiply(right, Extent)), Multiply(up, Extent)),
            Add(Add(centre, Multiply(right, Extent)), Multiply(up, Extent)),
            Add(Subtract(centre, Multiply(right, Extent)), Multiply(up, Extent))
        };
    }

    std::vector<Vector3> Q3Import::Clip(
        const std::vector<Vector3>& points,
        Vector3 normal,
        float distance)
    {
        constexpr float Epsilon = 0.01F;
        std::vector<Vector3> result;
        for (std::size_t i = 0; i < points.size(); ++i)
        {
            const Vector3 current = points[i];
            const Vector3 next
                = points[(i + 1) % points.size()];
            const float distCurrent
                = Vector3::Dot(normal, current) - distance;
            const float distNext
                = Vector3::Dot(normal, next) - distance;

            if (distCurrent <= Epsilon)
            {
                result.push_back(current);
            }
            if ((distCurrent > Epsilon) != (distNext > Epsilon)
                && std::fabs(distCurrent - distNext) > 1.0e-6F)
            {
                // Clamped: a pair straddling the epsilon gap solves to a t
                // outside 0..1, and unclamped that point lands beyond the far
                // end of the edge and the polygon reads as a bowtie.
                const float t = std::clamp(distCurrent / (distCurrent - distNext), 0.0F, 1.0F);
                result.push_back(Add(
                    current,
                    Multiply(
                        Subtract(next, current),
                        t)));
            }
        }
        return result;
    }

    std::vector<Vector3> Q3Import::Weld(
        const std::vector<Vector3>& points)
    {
        // The hair is a thousandth of the polygon's own extent, with the old
        // constant as the floor: clipping a 131,072-unit sheet leaves
        // accidental edges far longer than a fixed 0.02.
        Vector3 min(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
        Vector3 max(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest());
        for (const Vector3 point : points)
        {
            min = ComponentMin(min, point);
            max = ComponentMax(max, point);
        }
        const float tolerance = MathMax(
            0.02F,
            std::sqrt(LengthSquared(Subtract(max, min))) * 0.001F);
        const float square = tolerance * tolerance;
        std::vector<Vector3> result;
        for (const Vector3 point : points)
        {
            bool found = false;
            for (const Vector3 existing : result)
            {
                if (LengthSquared(
                        Subtract(existing, point)) < square)
                {
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                result.push_back(point);
            }
        }
        return result;
    }

    void Q3Import::AddEntities(
        BuiltMap* map,
        MapDefinition* def,
        Q3Bsp* bsp,
        MapImport* import,
        bool verbose)
    {
        if (map == nullptr || def == nullptr
            || bsp == nullptr || import == nullptr)
        {
            throw System::NullReferenceException();
        }

        std::unordered_map<
            std::string,
            Vector3,
            Q3StringHash,
            Q3StringEqual> targets;
        std::unordered_map<
            std::string,
            float,
            Q3StringHash,
            Q3StringEqual> targetAngles;
        // The same targets in Quake's own units and axes, for the pads.
        std::unordered_map<
            std::string,
            Vector3,
            Q3StringHash,
            Q3StringEqual> q3Targets;

        for (const std::shared_ptr<Q3Entity>& entityValue : bsp->Entities())
        {
            const Q3Entity* entity = &RequireReference(entityValue);
            const std::string* name
                = EntityValue(entity, "targetname");
            const std::string* origin
                = EntityValue(entity, "origin");
            if (name != nullptr && origin != nullptr)
            {
                const std::shared_ptr<std::vector<float>> raw = ParseVector(*origin);
                targets[*name] = ToWorld(raw.get(), import->UnitsPerUnit());
                q3Targets[*name] = Vector3(
                    ManagedAt(raw.get(), 0), ManagedAt(raw.get(), 1), ManagedAt(raw.get(), 2));
                float angle = 0.0F;
                const std::string* value = EntityValue(entity, "angle");
                if (value != nullptr)
                {
                    (void)::MphRead::NativeRuntime::SingleTryParseInvariant(*value, angle);
                }
                targetAngles[*name] = angle;
            }
        }

        std::int32_t pads = 0;
        std::int32_t teleporters = 0;
        std::int32_t items = 0;
        for (const std::shared_ptr<Q3Entity>& entityValue : bsp->Entities())
        {
            const Q3Entity* entity = &RequireReference(entityValue);
            const std::string* classname
                = EntityValue(entity, "classname");
            if (classname == nullptr)
            {
                continue;
            }

            if (StringEqualsOrdinalIgnoreCase(
                    *classname, "info_player_deathmatch")
                || StringEqualsOrdinalIgnoreCase(
                    *classname, "info_player_start"))
            {
                const std::string* origin = nullptr;
                if (!import->KeepSpawns()
                    || (origin = EntityValue(entity, "origin")) == nullptr)
                {
                    continue;
                }

                float angle = 0.0F;
                const std::string* value
                    = EntityValue(entity, "angle");
                float parsed = 0.0F;
                if (value != nullptr
                    && ::MphRead::NativeRuntime::SingleTryParseInvariant(*value, parsed))
                {
                    angle = parsed;
                }

                const Vector3 position = ToWorld(
                    ParseVector(*origin).get(),
                    import->UnitsPerUnit());

                MapDefinition::SpawnList* spawns = def->Spawns();
                if (spawns == nullptr)
                {
                    throw System::NullReferenceException();
                }
                auto spawn = std::make_shared<MapSpawn>();
                spawn->Position(std::make_shared<std::vector<float>>(
                    std::initializer_list<float>{
                        position.X,
                        position.Y - 24.0F / import->UnitsPerUnit(),
                        position.Z}));
                spawn->Yaw(90.0F + angle);
                spawns->push_back(std::move(spawn));
            }
            else if (StringEqualsOrdinalIgnoreCase(
                *classname, "trigger_push"))
            {
                const std::string* model
                    = EntityValue(entity, "model");
                const std::string* target
                    = EntityValue(entity, "target");
                if (model == nullptr
                    || !::MphRead::NativeRuntime::StringStartsWithCurrentCulture(*model, "*")
                    || target == nullptr)
                {
                    continue;
                }

                const auto targetIterator = targets.find(*target);
                if (targetIterator == targets.end())
                {
                    continue;
                }
                const Vector3 destination = targetIterator->second;

                std::int32_t modelIndex = 0;
                if (!Int32TryParseCurrentCulture(
                        std::string_view(*model).substr(1),
                        modelIndex)
                    || modelIndex < 0
                    || static_cast<std::size_t>(modelIndex)
                        >= bsp->Models().size())
                {
                    continue;
                }

                Q3Model* volume
                    = &RequireReference(ManagedListAt(bsp->Models(), modelIndex));
                const Vector3 min = ToWorld(
                    &RequireReference(volume->Mins()),
                    import->UnitsPerUnit());
                const Vector3 max = ToWorld(
                    &RequireReference(volume->Maxs()),
                    import->UnitsPerUnit());
                const Vector3 lower = ComponentMin(min, max);
                const Vector3 upper = ComponentMax(min, max);
                const Vector3 centre(
                    (lower.X + upper.X) / 2.0F,
                    lower.Y,
                    (lower.Z + upper.Z) / 2.0F);

                MapDefinition::JumpPadList* jumpPads
                    = def->JumpPads();
                if (jumpPads == nullptr)
                {
                    throw System::NullReferenceException();
                }
                auto pad = std::make_shared<MapJumpPad>();
                pad->Position(std::make_shared<std::vector<float>>(
                    std::initializer_list<float>{
                        centre.X, centre.Y, centre.Z}));
                // The level draws its own pad (launchpad_diamond and the
                // like, plain surfaces of the .bsp), so the cartridge's pad
                // and beam stay off: JumpPadEntity::HiddenModelId.
                pad->ModelId(0xFFFFFFFFU);

                // Quake aims a pad at the top of its arc, not at where it
                // lands (g_mover's AimAtTarget): the target is often over
                // the void, with the platform it throws you onto further
                // on. So the launch is Quake's own, from the volume's centre,
                // with its speed scaled by the root of the gravity ratio --
                // the same path under this game's gravity, only slower --
                // and the target kept is where that path comes down, for the
                // bots' routes.
                const std::vector<float>& q3Min = RequireReference(volume->Mins());
                const std::vector<float>& q3Max = RequireReference(volume->Maxs());
                const Vector3 q3Origin(
                    (q3Min[0] + q3Max[0]) / 2.0F,
                    (q3Min[1] + q3Max[1]) / 2.0F,
                    (q3Min[2] + q3Max[2]) / 2.0F);
                // A trigger often reaches into the floor; the pad stands on
                // the floor, where a bot has to walk to.
                if (const std::optional<float> padFloor
                        = Q3Floor(bsp, q3Origin.X, q3Origin.Y, q3Max[2]);
                    padFloor && *padFloor >= q3Min[2] - 1.0F)
                {
                    pad->Position(std::make_shared<std::vector<float>>(
                        std::initializer_list<float>{
                            centre.X, *padFloor / import->UnitsPerUnit(), centre.Z}));
                }
                const Vector3 apex = q3Targets[*target];
                constexpr float Q3Gravity = 800.0F;
                const float height = apex.Z - q3Origin.Z;
                Vector3 landing = destination;
                if (height > 0.0F)
                {
                    const float time = std::sqrt(height / (0.5F * Q3Gravity));
                    const float dx = apex.X - q3Origin.X;
                    const float dy = apex.Y - q3Origin.Y;
                    const Vector3 velocity(dx / time, dy / time, time * Q3Gravity);

                    const float unit = import->UnitsPerUnit();
                    constexpr float Gravity = 77.0F / 4096.0F;
                    const float q3GravityPerFrame = Q3Gravity / unit / 900.0F;
                    const float scale = std::sqrt(Gravity / q3GravityPerFrame);
                    const Vector3 world(velocity.X, velocity.Z, -velocity.Y);
                    const float length = std::sqrt(
                        world.X * world.X + world.Y * world.Y + world.Z * world.Z);
                    pad->Vector(std::make_shared<std::vector<float>>(
                        std::initializer_list<float>{
                            world.X / length, world.Y / length, world.Z / length}));
                    pad->Speed(length / unit / 30.0F * scale);

                    // Down the arc in sixtieths of a second until it passes
                    // below the floor under it.
                    for (std::int32_t step = 1; step < 60 * 12; ++step)
                    {
                        const float t = static_cast<float>(step) / 60.0F;
                        const Vector3 at(
                            q3Origin.X + velocity.X * t,
                            q3Origin.Y + velocity.Y * t,
                            q3Origin.Z + velocity.Z * t - 0.5F * Q3Gravity * t * t);
                        if (velocity.Z - Q3Gravity * t > 0.0F)
                        {
                            continue;
                        }
                        const std::optional<float> floor = Q3Floor(bsp, at.X, at.Y, at.Z + 32.0F);
                        if (floor && at.Z <= *floor)
                        {
                            landing = ToWorld(Vector3(at.X, at.Y, *floor), unit);
                            break;
                        }
                    }
                }
                pad->Target(std::make_shared<std::vector<float>>(
                    std::initializer_list<float>{
                        landing.X, landing.Y, landing.Z}));
                pad->Size(std::make_shared<std::vector<float>>(
                    std::initializer_list<float>{
                        MathMax(upper.X - lower.X, 0.8F),
                        MathMax(upper.Y - lower.Y, 0.8F),
                        MathMax(upper.Z - lower.Z, 0.8F)}));
                jumpPads->push_back(std::move(pad));
                pads = UncheckedAdd(pads, 1);
            }
            else if (StringEqualsOrdinalIgnoreCase(
                *classname, "trigger_teleport"))
            {
                // A brush volume in Quake; here the cartridge's teleporter
                // pad, stood on the floor of that volume at its centre, which
                // sends you to the destination's floor facing its angle.
                // Spawnflag 1 is a spectators-only teleporter.
                const std::string* model = EntityValue(entity, "model");
                const std::string* target = EntityValue(entity, "target");
                const std::string* spawnflags = EntityValue(entity, "spawnflags");
                std::int32_t flags = 0;
                if (spawnflags != nullptr)
                {
                    (void)Int32TryParseCurrentCulture(*spawnflags, flags);
                }
                if (model == nullptr
                    || !::MphRead::NativeRuntime::StringStartsWithCurrentCulture(*model, "*")
                    || target == nullptr
                    || (flags & 1) != 0)
                {
                    continue;
                }
                const auto targetIterator = targets.find(*target);
                std::int32_t modelIndex = 0;
                if (targetIterator == targets.end()
                    || !Int32TryParseCurrentCulture(std::string_view(*model).substr(1), modelIndex)
                    || modelIndex < 0
                    || static_cast<std::size_t>(modelIndex) >= bsp->Models().size())
                {
                    continue;
                }
                Q3Model* volume
                    = &RequireReference(ManagedListAt(bsp->Models(), modelIndex));
                const Vector3 min = ToWorld(
                    &RequireReference(volume->Mins()),
                    import->UnitsPerUnit());
                const Vector3 max = ToWorld(
                    &RequireReference(volume->Maxs()),
                    import->UnitsPerUnit());
                const Vector3 lower = ComponentMin(min, max);
                const Vector3 upper = ComponentMax(min, max);
                const Vector3 destination = targetIterator->second;

                MapDefinition::TeleporterList* list = def->Teleporters();
                if (list == nullptr)
                {
                    throw System::NullReferenceException();
                }
                const std::vector<float>& q3Min = RequireReference(volume->Mins());
                const std::vector<float>& q3Max = RequireReference(volume->Maxs());
                float floorY = lower.Y;
                if (const std::optional<float> floor = Q3Floor(bsp,
                        (q3Min[0] + q3Max[0]) / 2.0F, (q3Min[1] + q3Max[1]) / 2.0F, q3Max[2]);
                    floor && *floor >= q3Min[2] - 1.0F)
                {
                    floorY = *floor / import->UnitsPerUnit();
                }
                auto teleporter = std::make_shared<MapTeleporter>();
                teleporter->Position(std::make_shared<std::vector<float>>(
                    std::initializer_list<float>{
                        (lower.X + upper.X) / 2.0F,
                        floorY,
                        (lower.Z + upper.Z) / 2.0F}));
                teleporter->Target(std::make_shared<std::vector<float>>(
                    std::initializer_list<float>{
                        destination.X,
                        destination.Y - 24.0F / import->UnitsPerUnit(),
                        destination.Z}));
                teleporter->Yaw(90.0F + targetAngles[*target]);
                list->push_back(std::move(teleporter));
                teleporters = UncheckedAdd(teleporters, 1);
            }
        }

        // The level's own pickups, after the loop: whether they are wanted at
        // all is one question about the level.
        const std::vector<Q3Pickup> pickups = Pickups(bsp, import->UnitsPerUnit());
        MapDefinition::ItemList* mapItems = def->Items();
        if (mapItems == nullptr)
        {
            throw System::NullReferenceException();
        }
        if (import->KeepItems())
        {
            for (const Q3Pickup& pickup : pickups)
            {
                auto item = std::make_shared<MapItem>();
                item->Position(std::make_shared<std::vector<float>>(
                    std::initializer_list<float>{
                        pickup.Position.X, pickup.Position.Y, pickup.Position.Z}));
                item->Type(::MphRead::ToString(pickup.Type));
                mapItems->push_back(std::move(item));
                items = UncheckedAdd(items, 1);
            }
        }

        if (verbose)
        {
            MapDefinition::SpawnList* spawns = def->Spawns();
            if (spawns == nullptr)
            {
                throw System::NullReferenceException();
            }
            const std::string note = import->KeepItems()
                ? (items > 0
                    ? " (" + Runtime::ToString(items) + " of them the level's own)"
                    : std::string())
                : (!pickups.empty()
                    ? ", the level's "
                        + Runtime::ToString(static_cast<std::int32_t>(pickups.size()))
                        + " ignored"
                    : std::string());
            WriteLine("  "
                + Runtime::ToString(static_cast<std::int32_t>(spawns->size()))
                + " spawns, " + Runtime::ToString(pads)
                + " jump pads, " + Runtime::ToString(teleporters)
                + " teleporters, "
                + Runtime::ToString(static_cast<std::int32_t>(mapItems->size()))
                + " items" + note);
        }

        MapBuilder::AddEntities(map, def);
    }

    std::optional<float> Q3Import::Floor(Q3Bsp* bsp, float x, float y, float z)
    {
        return Q3Floor(&RequireReference(bsp), x, y, z);
    }

    // The level's pickups, in world units: one definition of what counts as
    // one and where it is, for the importer, the converter and -mapitems.
    std::vector<Q3Import::Q3Pickup> Q3Import::Pickups(Q3Bsp* bsp, float unitsPerUnit)
    {
        if (bsp == nullptr)
        {
            throw System::NullReferenceException();
        }
        std::vector<Q3Pickup> pickups;
        for (const std::shared_ptr<Q3Entity>& entityValue : bsp->Entities())
        {
            const Q3Entity* entity = entityValue.get();
            const std::string* classname = EntityValue(entity, "classname");
            const std::string* origin = EntityValue(entity, "origin");
            if (classname == nullptr || origin == nullptr)
            {
                continue;
            }
            const ItemType type = MapItemType(*classname);
            if (type == ItemType::None)
            {
                continue;
            }
            const std::string* targetName = EntityValue(entity, "targetname");
            pickups.push_back(Q3Pickup{*classname, type, ToWorld(ParseVector(*origin).get(), unitsPerUnit),
                targetName != nullptr ? std::optional<std::string>(*targetName) : std::nullopt});
        }
        return pickups;
    }

    ItemType Q3Import::MapItemType(
        const std::string& classname) noexcept
    {
        const auto equal = [&classname](const char* value) noexcept
        {
            return Q3StringEqual{}(classname, value);
        };

        if (equal("weapon_railgun")) return ItemType::Imperialist;
        if (equal("weapon_rocketlauncher")) return ItemType::Magmaul;
        if (equal("weapon_lightning")) return ItemType::ShockCoil;
        if (equal("weapon_plasmagun")) return ItemType::VoltDriver;
        if (equal("weapon_shotgun")) return ItemType::Battlehammer;
        if (equal("weapon_grenadelauncher")) return ItemType::Judicator;
        if (equal("weapon_bfg")) return ItemType::OmegaCannon;
        // item_quad and item_invis are left out like every other powerup:
        // custom maps carry none.
        if (equal("item_health")) return ItemType::HealthMedium;
        if (equal("item_health_small")) return ItemType::HealthSmall;
        if (equal("item_health_large")) return ItemType::HealthBig;
        if (equal("item_health_mega")) return ItemType::HealthBig;
        if (equal("item_armor_shard")) return ItemType::UASmall;
        if (equal("item_armor_combat")) return ItemType::UABig;
        if (equal("item_armor_body")) return ItemType::UABig;
        if (equal("ammo_rockets")) return ItemType::MissileBig;
        if (equal("ammo_slugs")) return ItemType::UABig;
        if (equal("ammo_cells")) return ItemType::UASmall;
        if (equal("ammo_shells")) return ItemType::UASmall;
        if (equal("ammo_bullets")) return ItemType::UASmall;
        if (equal("ammo_grenades")) return ItemType::MissileSmall;
        if (equal("ammo_lightning")) return ItemType::UASmall;
        return ItemType::None;
    }

    std::shared_ptr<std::vector<float>> Q3Import::ParseVector(
        const std::string& value)
    {
        auto result = std::make_shared<std::vector<float>>(3, 0.0F);

        std::size_t index = 0;
        std::int32_t partIndex = 0;
        while (index < value.size() && partIndex < 3)
        {
            while (index < value.size() && value[index] == ' ')
            {
                ++index;
            }
            if (index >= value.size())
            {
                break;
            }

            const std::size_t start = index;
            while (index < value.size() && value[index] != ' ')
            {
                ++index;
            }

            float parsed = 0.0F;
            (void)::MphRead::NativeRuntime::SingleTryParseInvariant(std::string_view(value).substr(start, index - start), parsed);
            (*result)[static_cast<std::size_t>(partIndex)] = parsed;
            ++partIndex;
        }
        return result;
    }

    Vector3 Q3Import::ToWorld(
        const std::vector<float>* position, float unit)
    {
        const float x = ManagedAt(position, 0);
        const float y = ManagedAt(position, 2);
        const float z = -ManagedAt(position, 1);
        return Vector3(x / unit, y / unit, z / unit);
    }

    Vector3 Q3Import::ToWorld(
        Vector3 position, float unit) noexcept
    {
        return Vector3(
            position.X / unit,
            position.Z / unit,
            -position.Y / unit);
    }

    Vector3 Q3Import::ToDirection(
        const std::vector<float>* direction)
    {
        const Vector3 result(
            ManagedAt(direction, 0),
            ManagedAt(direction, 2),
            -ManagedAt(direction, 1));
        return LengthSquared(result) < 0.0001F
            ? Vector3(0.0F, 1.0F, 0.0F)
            : result.Normalized();
    }
}
