#include "Q3Import.hpp"
#include "OpenArenaTextures.hpp"
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
#include "ShaderAnim.hpp"
#include "Q3Movers.hpp"
#include "Q3Md3.hpp"
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
#include <functional>
#include <unordered_map>
#include <unordered_set>
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
        const std::vector<Q3Portal> portals = Portals(bsp.get());
        Lighting lighting = MakeLighting(bsp.get(), std::vector<std::string>{source});
        if (pack != nullptr && !bsp->Lightmaps().empty())
        {
            LightLayer(lighting, bsp.get());
            if (!lighting.Pages.empty())
            {
                lighting.LayerMaterial = static_cast<std::int32_t>(pack->Entries().size());
                map->LightPages = lighting.Pages;
            }
        }
        std::int32_t portalFaces = 0;

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

        // A moving shader's layers (MapTextureBake, ShaderAnim), each laid
        // over its faces as a decal, in order; and the shaders drawn by
        // brightness alone -- a flame -- which no light falls on.
        struct LayerUse
        {
            std::int32_t Order = 0;
            std::int32_t Material = 0;
            bool Lit = true;
        };
        std::unordered_map<std::int32_t, std::vector<LayerUse>> layersOf;
        std::unordered_set<std::int32_t> glowing;
        if (pack != nullptr)
        {
            for (std::size_t e = 0; e < pack->Entries().size(); ++e)
            {
                const std::string& text = pack->Entries()[e].Meta();
                if (text.empty())
                {
                    continue;
                }
                const ShaderAnim::EntryMeta meta = ShaderAnim::Decode(text);
                if (meta.Role == "layer")
                {
                    layersOf[meta.Of].push_back(LayerUse{meta.Order, static_cast<std::int32_t>(e), meta.Lit});
                }
                else if (meta.Role == "base" && meta.Blend == "add")
                {
                    glowing.insert(meta.Of);
                }
            }
            for (auto& [texture, uses] : layersOf)
            {
                std::sort(uses.begin(), uses.end(),
                    [](const LayerUse& left, const LayerUse& right) { return left.Order < right.Order; });
            }
        }
        std::int32_t layerFaces = 0;
        // A cloud sky's base and layers, and its dome's height.
        std::vector<std::int32_t> cloudMaterials;
        float cloudHeight = 0.0F;

        // The brush models that move (Q3Movers::IsMover), by face.
        std::vector<std::int32_t> faceMover(bsp->Faces().size(), -1);
        for (std::size_t m = 1; m < bsp->Models().size(); ++m)
        {
            if (!Q3Movers::IsMover(bsp.get(), static_cast<std::int32_t>(m)))
            {
                continue;
            }
            const Q3Model& model = RequireReference(bsp->Models()[m]);
            for (std::int32_t f = model.Face(); f < model.Face() + model.FaceCount(); ++f)
            {
                if (f >= 0 && static_cast<std::size_t>(f) < faceMover.size())
                {
                    faceMover[static_cast<std::size_t>(f)] = static_cast<std::int32_t>(m);
                }
            }
        }
        std::size_t faceIndex = 0;

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
            const std::int32_t mover = faceMover[faceIndex++];
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
            // A fog volume's own surface is never drawn: Quake fogs what is
            // seen through it (Scene::UpdateUniforms does), and its shader's
            // editor picture would be an opaque sheet over a pit.
            if ((texture->Contents() & Q3Bsp::ContentsFog) != 0)
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

            // A portal: the view from its camera, one material each
            // (MapTextureBake::PortalSourceIndex), on the face nearest the
            // portal's entity.
            if (pack != nullptr && !portals.empty() && IsPortalShader(texture->Name().Value()))
            {
                Vector3 centre(0.0F, 0.0F, 0.0F);
                for (std::int32_t i = 0; i < face->VertexCount(); ++i)
                {
                    const std::vector<float>& at = RequireReference(RequireReference(
                        ManagedListAt(bsp->Vertices(), UncheckedAdd(face->Vertex(), i))).Position());
                    centre = Add(centre, Vector3(at[0], at[1], at[2]));
                }
                centre = Divide(centre, static_cast<float>(std::max(face->VertexCount(), 1)));
                std::optional<std::size_t> nearest;
                float best = 256.0F * 256.0F;
                for (std::size_t p = 0; p < portals.size(); ++p)
                {
                    const float distance = LengthSquared(Subtract(portals[p].Surface, centre));
                    if (distance < best)
                    {
                        best = distance;
                        nearest = p;
                    }
                }
                std::int32_t portalMaterial = 0;
                if (nearest.has_value() && pack->BySourceIndex().TryGetValue(
                        MapTextureBake::PortalSourceIndex + static_cast<std::int32_t>(*nearest), portalMaterial))
                {
                    const auto [portalWidth, portalHeight] = textureSizes.at(static_cast<std::size_t>(portalMaterial));
                    AddPortalFace(map.get(), bsp.get(), face, unit, portalMaterial, portalWidth, portalHeight);
                    portalFaces = UncheckedAdd(portalFaces, 1);
                }
                else
                {
                    unpainted = UncheckedAdd(unpainted, 1);
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

            // A cloud sky: only where it goes; the dome is built after.
            if (sky && pack != nullptr && !skyBox)
            {
                const ShaderAnim::EntryMeta meta = ShaderAnim::Decode(
                    pack->Entries()[static_cast<std::size_t>(material)].Meta());
                if (meta.CloudHeight > 0.0F)
                {
                    if (cloudMaterials.empty())
                    {
                        cloudHeight = meta.CloudHeight;
                        cloudMaterials.push_back(material);
                        if (const auto layered = layersOf.find(face->Texture()); layered != layersOf.end())
                        {
                            for (const LayerUse& use : layered->second)
                            {
                                cloudMaterials.push_back(use.Material);
                            }
                        }
                    }
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
            }

            const bool patch = face->Type() == 2;
            if (patch)
            {
                patches = UncheckedAdd(patches, 1);
            }

            MphRead::Enumerable<BuiltFace*> builtFaces = patch
                ? Tessellate(
                    bsp.get(), face, unit, width, height,
                    material, sky, import->PatchLevel(), &lighting)
                : Triangles(
                    bsp.get(), face, unit, width, height,
                    material, sky, &lighting);

            std::vector<BuiltFace*> madeFaces;
            for (BuiltFace* built : builtFaces)
            {
                std::unique_ptr<BuiltFace> builtOwner(built);
                built = map->OwnFace(std::move(builtOwner));
                built->Mover = mover;
                madeFaces.push_back(built);
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

                if (patch && !built->Overlay && mover < 0
                    && (texture->Contents() & Q3Bsp::ContentsSolid) != 0)
                {
                    map->Solid().push_back(built);
                }
            }

            if (!sky && glowing.contains(face->Texture()))
            {
                for (BuiltFace* built : madeFaces)
                {
                    built->Colors.assign(FacePoints(built)->Length(), Vector3(1.0F, 1.0F, 1.0F));
                }
            }
            const auto layered = sky ? layersOf.end() : layersOf.find(face->Texture());
            if (layered != layersOf.end())
            {
                for (const LayerUse& use : layered->second)
                {
                    const auto [layerWidth, layerHeight] = textureSizes.at(static_cast<std::size_t>(use.Material));
                    const float scaleS = static_cast<float>(layerWidth) / static_cast<float>(std::max(width, 1));
                    const float scaleT = static_cast<float>(layerHeight) / static_cast<float>(std::max(height, 1));
                    for (BuiltFace* under : madeFaces)
                    {
                        ManagedArray<Vector3>* points = FacePoints(under);
                        ManagedArray<Vector2>* uvs = FaceTexcoords(under);
                        auto pointArray = std::make_unique<ManagedArray<Vector3>>(points->Length());
                        auto uvArray = std::make_unique<ManagedArray<Vector2>>(uvs->Length());
                        for (std::size_t i = 0; i < points->Length(); ++i)
                        {
                            (*pointArray)[i] = (*points)[i];
                        }
                        for (std::size_t i = 0; i < uvs->Length(); ++i)
                        {
                            (*uvArray)[i] = Vector2((*uvs)[i].X * scaleS, (*uvs)[i].Y * scaleT);
                        }
                        auto over = std::make_unique<BuiltFace>(
                            std::move(pointArray), std::move(uvArray), under->Normal(), use.Material, under->Shade());
                        over->Overlay = true;
                        over->Mover = under->Mover;
                        if (use.Lit)
                        {
                            over->Colors = under->Colors;
                        }
                        else
                        {
                            over->Colors.assign(points->Length(), Vector3(1.0F, 1.0F, 1.0F));
                        }
                        map->Faces().push_back(map->OwnFace(std::move(over)));
                        layerFaces = UncheckedAdd(layerFaces, 1);
                    }
                }
            }
        }
        // The models drawn on movers (Q3Md3), in their movers' nodes, lit as
        // the texture is painted.
        if (pack != nullptr)
        {
            std::vector<std::string> archives{source};
            for (std::string& pak : import->ResolveTexturePaks())
            {
                archives.push_back(std::move(pak));
            }
            const std::vector<Q3Md3::Placed> placed = Q3Md3::Load(bsp.get(), archives);
            const std::vector<std::string> shaders = Q3Md3::Shaders(placed);
            std::int32_t modelFaces = 0;
            for (const Q3Md3::Placed& one : placed)
            {
                for (const Q3Md3::Surface& surface : one.Surfaces)
                {
                    const auto at = std::find(shaders.begin(), shaders.end(), surface.Shader);
                    std::int32_t material = 0;
                    if (!pack->BySourceIndex().TryGetValue(
                            MapTextureBake::ModelSourceIndex + static_cast<std::int32_t>(at - shaders.begin()), material))
                    {
                        continue;
                    }
                    const auto [modelWidth, modelHeight] = textureSizes.at(static_cast<std::size_t>(material));
                    for (std::size_t t = 0; t < surface.Triangles.size(); ++t)
                    {
                        auto points = std::make_unique<ManagedArray<Vector3>>(3U);
                        auto uvs = std::make_unique<ManagedArray<Vector2>>(3U);
                        for (std::size_t k = 0; k < 3; ++k)
                        {
                            const std::vector<float> position{
                                surface.Triangles[t][k].X, surface.Triangles[t][k].Y, surface.Triangles[t][k].Z};
                            (*points)[k] = ToWorld(&position, unit);
                            (*uvs)[k] = Vector2(surface.Texcoords[t][k].X * static_cast<float>(modelWidth),
                                surface.Texcoords[t][k].Y * static_cast<float>(modelHeight));
                        }
                        const Vector3 normal = Vector3::Cross(
                            Subtract((*points)[1], (*points)[0]), Subtract((*points)[2], (*points)[0]));
                        if (LengthSquared(normal) < 1.0e-12F)
                        {
                            continue;
                        }
                        auto face = std::make_unique<BuiltFace>(
                            std::move(points), std::move(uvs), normal.Normalized(), material, 1.0F);
                        face->Mover = one.Model;
                        face->Colors.assign(3U, Vector3(0.5F, 0.5F, 0.5F));
                        map->Faces().push_back(map->OwnFace(std::move(face)));
                        modelFaces = UncheckedAdd(modelFaces, 1);
                    }
                }
            }
            if (verbose && modelFaces > 0)
            {
                WriteLine("  " + Runtime::ToString(modelFaces) + " triangles of models drawn on movers");
            }
        }
        if (verbose && layerFaces > 0)
        {
            WriteLine("  " + Runtime::ToString(layerFaces) + " faces of moving shaders' layers laid over the rest");
        }

        if ((skyBox || !cloudMaterials.empty()) && skyMin.X <= skyMax.X && !bsp->Models().empty())
        {
            // Sky surfaces cover only part of a level (Q3DM6 has none under
            // its platforms), so a box fitted to them cuts through the
            // middle of the level. Quake's sky is at infinity: the box takes
            // in the whole level, with room to spare.
            const Q3Model& world = RequireReference(bsp->Models().front());
            const std::vector<float>& wMin = RequireReference(world.Mins());
            const std::vector<float>& wMax = RequireReference(world.Maxs());
            const Vector3 levelMin(wMin[0], wMin[1], wMin[2]);
            const Vector3 levelMax(wMax[0], wMax[1], wMax[2]);
            skyMin = ComponentMin(skyMin, levelMin);
            skyMax = ComponentMax(skyMax, levelMax);
            // As much room as the vertices' range at this scale allows.
            const float reach = 8.0F * std::pow(2.0F, static_cast<float>(def->ScaleFactor())) * 0.995F;
            const auto fits = [&](Vector3 low, Vector3 high)
            {
                for (std::int32_t corner = 0; corner < 8; ++corner)
                {
                    const Vector3 world = ToWorld(Vector3(
                        (corner & 1) != 0 ? high.X : low.X,
                        (corner & 2) != 0 ? high.Y : low.Y,
                        (corner & 4) != 0 ? high.Z : low.Z), unit);
                    if (std::fabs(world.X) > reach || std::fabs(world.Y) > reach || std::fabs(world.Z) > reach)
                    {
                        return false;
                    }
                }
                return true;
            };
            for (const float share : {8.0F, 16.0F, 32.0F, 0.0F})
            {
                const Vector3 margin = share > 0.0F ? Divide(Subtract(skyMax, skyMin), share) : Vector3(0.0F, 0.0F, 0.0F);
                if (share == 0.0F || fits(Subtract(skyMin, margin), Add(skyMax, margin)))
                {
                    skyMin = Subtract(skyMin, margin);
                    skyMax = Add(skyMax, margin);
                    break;
                }
            }
        }
        if (skyBox && skyMin.X <= skyMax.X)
        {
            AddSkyBox(map.get(), skyMin, skyMax, unit, skyMaterials, textureSizes);
        }
        else if (!cloudMaterials.empty() && skyMin.X <= skyMax.X)
        {
            AddCloudBox(map.get(), skyMin, skyMax, unit, cloudHeight, cloudMaterials, textureSizes);
        }
        if (verbose && portalFaces > 0)
        {
            WriteLine("  " + Runtime::ToString(portalFaces) + " portal faces showing "
                + Runtime::ToString(static_cast<std::int32_t>(portals.size())) + " portals' views");
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

        // A mover's own brushes, where its brush model keeps them (about its
        // origin, for one that has one): the collision Q3MoverEntity moves.
        for (std::size_t m = 1; m < bsp->Models().size(); ++m)
        {
            if (!Q3Movers::IsMover(bsp.get(), static_cast<std::int32_t>(m)))
            {
                continue;
            }
            const Q3Model& model = RequireReference(bsp->Models()[m]);
            std::vector<BuiltFace*> moverSolid;
            for (std::int32_t b = model.Brush(); b < model.Brush() + model.BrushCount(); ++b)
            {
                Q3Brush* brush = &RequireReference(ManagedListAt(bsp->Brushes(), b));
                Q3Texture* texture = &RequireReference(ManagedListAt(bsp->Textures(), brush->Texture()));
                if ((texture->Contents() & (Q3Bsp::ContentsSolid | Q3Bsp::ContentsPlayerClip)) == 0)
                {
                    continue;
                }
                for (const BrushPolygon& polygon : BrushSides(bsp.get(), brush))
                {
                    auto world = std::make_unique<ManagedArray<Vector3>>(polygon.Points.size());
                    auto texcoords = std::make_unique<ManagedArray<Vector2>>(polygon.Points.size());
                    for (std::size_t i = 0; i < polygon.Points.size(); ++i)
                    {
                        (*world)[i] = ToWorld(polygon.Points[i], unit);
                    }
                    const std::vector<float> direction{polygon.Normal.X, polygon.Normal.Y, polygon.Normal.Z};
                    auto face = std::make_unique<BuiltFace>(
                        std::move(world), std::move(texcoords), ToDirection(std::addressof(direction)), 0, 1.0F);
                    moverSolid.push_back(map->OwnFace(std::move(face)));
                }
            }
            if (!moverSolid.empty())
            {
                map->MoverSolid.emplace(static_cast<std::int32_t>(m), std::move(moverSolid));
            }
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

    namespace
    {
        // Quake 3's own fog shaders, from tools/q3-texture-match.py.
        struct BaseFog
        {
            const char* Shader;
            float Red;
            float Green;
            float Blue;
            float Distance;
        };
        constexpr BaseFog BaseFogs[] = {
#include "Q3Fogs.inc"
            {nullptr, 0.0F, 0.0F, 0.0F, 0.0F}};
    }

    Q3Import::Lighting Q3Import::MakeLighting(Q3Bsp* bsp, const std::vector<std::string>& archives)
    {
        Lighting lighting;
        if (bsp == nullptr)
        {
            return lighting;
        }
        // The level's own scripts, read only for a level with fog.
        const auto own = bsp->Fogs().empty()
            ? std::unordered_map<std::string, std::array<float, 4>>{}
            : MapTextureBake::FogParms(archives);
        for (const Q3Bsp::Fog& fog : bsp->Fogs())
        {
            FogVolume volume;
            std::string lower = fog.Shader;
            for (char& ch : lower)
            {
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
            if (const auto found = own.find(lower); found != own.end())
            {
                volume.Color = Vector3(found->second[0], found->second[1], found->second[2]);
                volume.Distance = found->second[3];
            }
            else
            {
                for (const BaseFog* base = BaseFogs; base->Shader != nullptr; ++base)
                {
                    if (lower == base->Shader)
                    {
                        volume.Color = Vector3(base->Red, base->Green, base->Blue);
                        volume.Distance = base->Distance;
                        break;
                    }
                }
            }
            if (fog.Brush >= 0 && static_cast<std::size_t>(fog.Brush) < bsp->Brushes().size())
            {
                Q3Brush* brush = &RequireReference(ManagedListAt(bsp->Brushes(), fog.Brush));
                for (std::int32_t i = 0; i < brush->SideCount(); ++i)
                {
                    Q3BrushSide* side = &RequireReference(ManagedListAt(bsp->BrushSides(), UncheckedAdd(brush->FirstSide(), i)));
                    Q3Plane* plane = &RequireReference(ManagedListAt(bsp->Planes(), side->Plane()));
                    const Vector4 value(plane->X(), plane->Y(), plane->Z(), plane->Distance());
                    volume.Planes.push_back(value);
                    if (i == fog.VisibleSide)
                    {
                        volume.Surface = value;
                    }
                }
            }
            lighting.Fogs.push_back(std::move(volume));
        }
        // The fog whose brush holds most of the level's box is the air of
        // the room (q3dm4r's grey haze), not a layer over a pool.
        if (!bsp->Models().empty() && bsp->Models().front())
        {
            const Q3Model& world = *bsp->Models().front();
            const std::vector<float>& mins = RequireReference(world.Mins().get());
            const std::vector<float>& maxs = RequireReference(world.Maxs().get());
            float level = 1.0F;
            for (std::size_t k = 0; k < 3; ++k)
            {
                level *= std::max(1.0F, maxs.at(k) - mins.at(k));
            }
            for (FogVolume& volume : lighting.Fogs)
            {
                std::array<float, 3> low{mins.at(0), mins.at(1), mins.at(2)};
                std::array<float, 3> high{maxs.at(0), maxs.at(1), maxs.at(2)};
                for (const Vector4& plane : volume.Planes)
                {
                    const std::array<float, 3> normal{plane.X, plane.Y, plane.Z};
                    for (std::size_t k = 0; k < 3; ++k)
                    {
                        if (normal[k] > 0.999F)
                        {
                            high[k] = std::min(high[k], plane.W);
                        }
                        else if (normal[k] < -0.999F)
                        {
                            low[k] = std::max(low[k], -plane.W);
                        }
                    }
                }
                float inside = 1.0F;
                for (std::size_t k = 0; k < 3; ++k)
                {
                    inside *= std::max(0.0F, high[k] - low[k]);
                }
                volume.Room = volume.Distance > 0.0F && inside >= level * 0.4F;
            }
        }
        return lighting;
    }

    void Q3Import::LightLayer(Lighting& lighting, Q3Bsp* bsp)
    {
        constexpr std::int32_t Size = Q3Bsp::LightmapSize;
        constexpr std::size_t PageBytes = static_cast<std::size_t>(Size) * Size * 3;
        const std::vector<std::uint8_t>& raw = bsp->Lightmaps();
        const std::size_t pages = raw.size() / PageBytes;
        if (pages == 0)
        {
            return;
        }
        const auto light = [&](std::size_t page, std::int32_t x, std::int32_t y)
        {
            const std::size_t at = page * PageBytes + (static_cast<std::size_t>(y) * Size + x) * 3;
            const std::vector<std::uint8_t> rgb{raw[at], raw[at + 1], raw[at + 2]};
            return Light(&rgb);
        };
        // How bright a luxel is as stored: Quake's overbright makes most of a
        // lit floor brighter than white, so the light as drawn (Light) is the
        // same in the sun and out of it; the stored values still differ.
        const auto stored = [&](std::size_t page, std::int32_t x, std::int32_t y)
        {
            const std::size_t at = page * PageBytes + (static_cast<std::size_t>(y) * Size + x) * 3;
            return 0.299F * raw[at] + 0.587F * raw[at + 1] + 0.114F * raw[at + 2];
        };
        // How bright, as its strongest channel: a face's vertices carry the
        // hue at that brightness (LitFaces), so the layer's share of it gives
        // the light back in colour as well as in strength.
        const auto luminance = [](Vector3 c) { return std::max(c.X, std::max(c.Y, c.Z)); };
        lighting.Pages.assign(pages, std::vector<std::uint8_t>(static_cast<std::size_t>(Size) * Size, 0));
        for (const std::shared_ptr<Q3Face>& faceValue : bsp->Faces())
        {
            Q3Face& face = *faceValue;
            // A level compiled for Quake Live leaves the block out: it is
            // where the face's vertices' lightmap coordinates fall.
            if (face.LightmapIndex >= 0 && (face.LightmapRect[2] <= 0 || face.LightmapRect[3] <= 0))
            {
                float lowS = 1.0F, lowT = 1.0F, highS = 0.0F, highT = 0.0F;
                for (std::int32_t i = 0; i < face.VertexCount(); ++i)
                {
                    const Q3Vertex& vertex = RequireReference(ManagedListAt(bsp->Vertices(), UncheckedAdd(face.Vertex(), i)));
                    lowS = std::min(lowS, vertex.Lightmap[0]);
                    lowT = std::min(lowT, vertex.Lightmap[1]);
                    highS = std::max(highS, vertex.Lightmap[0]);
                    highT = std::max(highT, vertex.Lightmap[1]);
                }
                if (highS >= lowS && highT >= lowT)
                {
                    const std::int32_t x = std::clamp(static_cast<std::int32_t>(std::floor(lowS * Size)), 0, Size - 1);
                    const std::int32_t y = std::clamp(static_cast<std::int32_t>(std::floor(lowT * Size)), 0, Size - 1);
                    const std::int32_t right = std::clamp(static_cast<std::int32_t>(std::ceil(highS * Size)), x + 1, Size);
                    const std::int32_t bottom = std::clamp(static_cast<std::int32_t>(std::ceil(highT * Size)), y + 1, Size);
                    face.LightmapRect = {x, y, right - x, bottom - y};
                }
            }
            const auto& [x0, y0, w, h] = face.LightmapRect;
            if (face.LightmapIndex < 0 || static_cast<std::size_t>(face.LightmapIndex) >= pages
                || w <= 0 || h <= 0 || x0 < 0 || y0 < 0 || x0 + w > Size || y0 + h > Size)
            {
                continue;
            }
            const std::array<std::int32_t, 5> key{face.LightmapIndex, x0, y0, w, h};
            if (lighting.Brightest.contains(key))
            {
                continue;
            }
            const std::size_t page = static_cast<std::size_t>(face.LightmapIndex);
            Vector3 brightest(0.0F, 0.0F, 0.0F);
            float top = 0.0F;
            for (std::int32_t y = y0; y < y0 + h; ++y)
            {
                for (std::int32_t x = x0; x < x0 + w; ++x)
                {
                    const Vector3 at = light(page, x, y);
                    if (luminance(at) > top)
                    {
                        top = luminance(at);
                        brightest = at;
                    }
                }
            }
            (void)stored;
            lighting.Brightest.emplace(key, brightest);
            // Against the light as drawn: up to white, which with the
            // textures' overbright is twice their own brightness.
            const float drawnTop = std::max(luminance(brightest), 1.0F / 255.0F);
            for (std::int32_t y = y0; y < y0 + h; ++y)
            {
                for (std::int32_t x = x0; x < x0 + w; ++x)
                {
                    const float dark = 1.0F - std::clamp(luminance(light(page, x, y)) / drawnTop, 0.0F, 1.0F);
                    lighting.Pages[page][static_cast<std::size_t>(y) * Size + x]
                        = static_cast<std::uint8_t>(std::lround(dark * 31.0F));
                }
            }
        }
    }

    // A face's lightmap at (s, t), bilinear across its luxels, as the light
    // Quake draws it with (R_ColorShiftLightingBytes, like the vertices').
    Vector3 Q3Import::SampleLightmap(Q3Bsp* bsp, std::int32_t page, Vector2 st)
    {
        constexpr std::int32_t Size = Q3Bsp::LightmapSize;
        const std::vector<std::uint8_t>& pages = bsp->Lightmaps();
        const std::size_t base = static_cast<std::size_t>(page) * Size * Size * 3;
        const float x = std::clamp(st.X * Size - 0.5F, 0.0F, static_cast<float>(Size - 1));
        const float y = std::clamp(st.Y * Size - 0.5F, 0.0F, static_cast<float>(Size - 1));
        const std::int32_t x0 = static_cast<std::int32_t>(x);
        const std::int32_t y0 = static_cast<std::int32_t>(y);
        const std::int32_t x1 = std::min(x0 + 1, Size - 1);
        const std::int32_t y1 = std::min(y0 + 1, Size - 1);
        const float fx = x - static_cast<float>(x0);
        const float fy = y - static_cast<float>(y0);
        std::vector<std::uint8_t> rgb(3);
        for (std::size_t c = 0; c < 3; ++c)
        {
            const auto at = [&](std::int32_t px, std::int32_t py)
            {
                return static_cast<float>(pages[base + (static_cast<std::size_t>(py) * Size + px) * 3 + c]);
            };
            const float top = at(x0, y0) * (1.0F - fx) + at(x1, y0) * fx;
            const float bottom = at(x0, y1) * (1.0F - fx) + at(x1, y1) * fx;
            rgb[c] = static_cast<std::uint8_t>(std::clamp(std::lround(top * (1.0F - fy) + bottom * fy), 0L, 255L));
        }
        return Light(&rgb);
    }

    // One of the level's triangles as this game draws it: lit by its
    // lightmap where it has one -- cut finer where the light changes more
    // across it than its corners can say, down to two luxels -- by its
    // vertices where it has none, and tinted by the fog it stands in, deeper
    // in the fog the more of it.
    std::vector<BuiltFace*> Q3Import::LitFaces(
        Q3Bsp* bsp, const Lighting* lighting, Q3Face* face,
        std::array<LitCorner, 3> corners, Vector3 normal,
        std::int32_t material, float shade, bool sky)
    {
        // A patch is already cut fine, and its cells are its collision too.
        const bool subdivide = face->Type() != 2;
        // A surface with no lightmap by its shader (lava, a glowing panel)
        // Quake draws at its own colour, which its texture is baked at.
        const bool ownColour = face->Texture() >= 0
            && static_cast<std::size_t>(face->Texture()) < bsp->Textures().size()
            && (RequireReference(ManagedListAt(bsp->Textures(), face->Texture())).Flags() & Q3Bsp::SurfaceNoLightmap) != 0;
        if (ownColour && !sky)
        {
            for (LitCorner& corner : corners)
            {
                corner.Color = Vector3(1.0F, 1.0F, 1.0F);
            }
        }
        std::vector<BuiltFace*> faces;
        const std::int32_t page = face->LightmapIndex;
        constexpr std::size_t PageBytes = static_cast<std::size_t>(Q3Bsp::LightmapSize) * Q3Bsp::LightmapSize * 3;
        const bool lit = !sky && page >= 0
            && (static_cast<std::size_t>(page) + 1) * PageBytes <= bsp->Lightmaps().size();
        std::optional<Vector3> layered;
        if (lit && lighting != nullptr && lighting->LayerMaterial >= 0)
        {
            const auto& [x0, y0, w, h] = face->LightmapRect;
            if (const auto found = lighting->Brightest.find({page, x0, y0, w, h}); found != lighting->Brightest.end())
            {
                layered = found->second;
            }
        }
        // The light a corner is given: the lightmap's, or on a layered face
        // its hue at the face's brightest strength -- a red glow spreading
        // red across a white-lit wall -- which the layer then darkens.
        const auto lightAt = [&](Vector2 st)
        {
            const Vector3 sample = SampleLightmap(bsp, page, st);
            if (!layered.has_value())
            {
                return sample;
            }
            const float strongest = std::max(sample.X, std::max(sample.Y, sample.Z));
            const float top = std::max(layered->X, std::max(layered->Y, layered->Z));
            if (strongest < 0.02F)
            {
                return *layered;
            }
            return Multiply(sample, top / strongest);
        };
        if (lit)
        {
            for (LitCorner& corner : corners)
            {
                corner.Color = lightAt(corner.Lightmap);
            }
        }
        // Fog is drawn by the renderer from the level's volumes (what each
        // ray crosses of them, Scene::UpdateUniforms), not painted in.
        const FogVolume* fog = nullptr;
        const auto middle = [&](const LitCorner& a, const LitCorner& b)
        {
            LitCorner m;
            m.World = Divide(Add(a.World, b.World), 2.0F);
            m.Quake = Divide(Add(a.Quake, b.Quake), 2.0F);
            m.Uv = Vector2((a.Uv.X + b.Uv.X) / 2.0F, (a.Uv.Y + b.Uv.Y) / 2.0F);
            m.Lightmap = Vector2((a.Lightmap.X + b.Lightmap.X) / 2.0F, (a.Lightmap.Y + b.Lightmap.Y) / 2.0F);
            m.Color = lit ? lightAt(m.Lightmap) : Divide(Add(a.Color, b.Color), 2.0F);
            return m;
        };
        const auto off = [](Vector3 a, Vector3 b)
        {
            return std::max(std::fabs(a.X - b.X), std::max(std::fabs(a.Y - b.Y), std::fabs(a.Z - b.Z)));
        };
        std::function<void(const std::array<LitCorner, 3>&, std::int32_t)> emit;
        emit = [&](const std::array<LitCorner, 3>& tri, std::int32_t depth)
        {
            if (lit && subdivide && depth < (layered.has_value() ? 5 : 7))
            {
                std::size_t longest = 0;
                float length = -1.0F;
                for (std::size_t i = 0; i < 3; ++i)
                {
                    const Vector2 a = tri[i].Lightmap;
                    const Vector2 b = tri[(i + 1) % 3].Lightmap;
                    const float luxels = std::sqrt((a.X - b.X) * (a.X - b.X) + (a.Y - b.Y) * (a.Y - b.Y))
                        * static_cast<float>(Q3Bsp::LightmapSize);
                    if (luxels > length)
                    {
                        length = luxels;
                        longest = i;
                    }
                }
                if (length > 4.0F)
                {
                    float error = 0.0F;
                    for (std::size_t i = 0; i < 3; ++i)
                    {
                        const LitCorner& a = tri[i];
                        const LitCorner& b = tri[(i + 1) % 3];
                        const Vector2 st((a.Lightmap.X + b.Lightmap.X) / 2.0F, (a.Lightmap.Y + b.Lightmap.Y) / 2.0F);
                        error = std::max(error, off(lightAt(st), Divide(Add(a.Color, b.Color), 2.0F)));
                    }
                    const Vector2 centre(
                        (tri[0].Lightmap.X + tri[1].Lightmap.X + tri[2].Lightmap.X) / 3.0F,
                        (tri[0].Lightmap.Y + tri[1].Lightmap.Y + tri[2].Lightmap.Y) / 3.0F);
                    error = std::max(error, off(lightAt(centre),
                        Divide(Add(Add(tri[0].Color, tri[1].Color), tri[2].Color), 3.0F)));
                    if (error > (layered.has_value() ? 0.12F : 0.08F))
                    {
                        const std::size_t a = longest;
                        const std::size_t b = (longest + 1) % 3;
                        const std::size_t c = (longest + 2) % 3;
                        const LitCorner m = middle(tri[a], tri[b]);
                        emit({tri[a], m, tri[c]}, depth + 1);
                        emit({m, tri[b], tri[c]}, depth + 1);
                        return;
                    }
                }
            }
            std::vector<Vector3> points{tri[0].World, tri[1].World, tri[2].World};
            std::vector<Vector2> uvs{tri[0].Uv, tri[1].Uv, tri[2].Uv};
            std::vector<Vector3> colors;
            if (!sky)
            {
                for (const LitCorner& corner : tri)
                {
                    Vector3 color = corner.Color;
                    if (fog != nullptr)
                    {
                        // How deep under the fog's surface -- or inside its
                        // nearest side, for a fog with none -- against the
                        // depth Quake's fog is opaque at.
                        float depth = std::numeric_limits<float>::max();
                        const auto inside = [&corner](const Vector4& plane)
                        {
                            return plane.W - (plane.X * corner.Quake.X + plane.Y * corner.Quake.Y + plane.Z * corner.Quake.Z);
                        };
                        if (fog->Surface.has_value())
                        {
                            depth = inside(*fog->Surface);
                        }
                        else
                        {
                            for (const Vector4& plane : fog->Planes)
                            {
                                depth = std::min(depth, inside(plane));
                            }
                        }
                        const float amount = std::clamp(depth / fog->Distance, 0.0F, 1.0F) * 0.9F;
                        color = Add(Multiply(color, 1.0F - amount), Multiply(fog->Color, amount));
                    }
                    colors.push_back(color);
                }
            }
            if (layered.has_value())
            {
                // The layer: the same triangle, at its lightmap coordinates
                // in luxels of its page's layer.
                std::vector<Vector3> again = points;
                std::vector<Vector2> luxels;
                for (const LitCorner& corner : tri)
                {
                    luxels.emplace_back(corner.Lightmap.X * static_cast<float>(Q3Bsp::LightmapSize),
                        corner.Lightmap.Y * static_cast<float>(Q3Bsp::LightmapSize));
                }
                BuiltFace* over = MakeFace(std::move(again), std::move(luxels), normal,
                    lighting->LayerMaterial + page, shade,
                    std::vector<Vector3>(3, Vector3(1.0F, 1.0F, 1.0F)));
                over->Overlay = true;
                faces.push_back(MakeFace(std::move(points), std::move(uvs), normal, material, shade, std::move(colors)));
                faces.push_back(over);
                return;
            }
            faces.push_back(MakeFace(std::move(points), std::move(uvs), normal, material, shade, std::move(colors)));
        };
        emit(corners, 0);
        return faces;
    }

    MphRead::Enumerable<BuiltFace*> Q3Import::Triangles(
        Q3Bsp* bsp,
        Q3Face* face,
        float unit,
        std::int32_t width,
        std::int32_t height,
        std::int32_t material,
        bool sky,
        const Lighting* lighting)
    {
        return MphRead::Enumerable<BuiltFace*>(
            [bsp, face, unit, width, height, material, sky, lighting]()
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
                    std::vector<Vector3> lights(3);
                    std::array<LitCorner, 3> corners{};
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
                        lights[static_cast<std::size_t>(j)] = Light(color);
                        const std::vector<float>& raw = RequireReference(vertex->Position());
                        corners[static_cast<std::size_t>(j)] = LitCorner{
                            points[static_cast<std::size_t>(j)],
                            Vector3(raw[0], raw[1], raw[2]),
                            uvs[static_cast<std::size_t>(j)],
                            Vector2(vertex->Lightmap[0], vertex->Lightmap[1]),
                            lights[static_cast<std::size_t>(j)]};
                    }

                    const Vector3 normal = TriangleNormal(points, vertexNormal,
                        ToDirection(&RequireReference(face->Normal())));
                    for (BuiltFace* lit : LitFaces(bsp, lighting, face, corners, normal,
                             material, Shade(shade / 3.0F, sky), sky))
                    {
                        co_yield lit;
                    }
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
        std::int32_t level,
        const Lighting* lighting)
    {
        return MphRead::Enumerable<BuiltFace*>(
            [bsp, face, unit, width, height, material, sky, level, lighting]()
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
                        std::vector<Vector3> lights(valueCount);
                        std::vector<Vector3> quakes(valueCount);
                        std::vector<Vector2> lightmaps(valueCount);

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
                                Vector3 light(0.0F, 0.0F, 0.0F);
                                Vector3 quake(0.0F, 0.0F, 0.0F);
                                Vector2 lightmap(0.0F, 0.0F);

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
                                        light = Add(light, Multiply(Light(color), weight));
                                        const std::vector<float>& raw = RequireReference(vertex->Position());
                                        quake = Add(quake, Multiply(Vector3(raw[0], raw[1], raw[2]), weight));
                                        lightmap = Add(lightmap, Multiply(
                                            Vector2(vertex->Lightmap[0], vertex->Lightmap[1]), weight));
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
                                lights[index] = light;
                                quakes[index] = quake;
                                lightmaps[index] = lightmap;
                            }
                        }

                        for (std::int32_t a = 0; a < tessLevel; ++a)
                        {
                            for (std::int32_t b = 0; b < tessLevel; ++b)
                            {
                                for (const std::array<GridPoint, 3>& cell : {
                                         std::array<GridPoint, 3>{GridPoint{a, b}, GridPoint{a, b + 1}, GridPoint{a + 1, b + 1}},
                                         std::array<GridPoint, 3>{GridPoint{a, b}, GridPoint{a + 1, b + 1}, GridPoint{a + 1, b}}})
                                {
                                    BuiltFace* shape = Cell(points, uvs, normals, shades, lights,
                                        dimension, material, sky, cell[0], cell[1], cell[2]);
                                    std::unique_ptr<BuiltFace> shapeOwner(shape);
                                    std::array<LitCorner, 3> corners{};
                                    for (std::size_t k = 0; k < 3; ++k)
                                    {
                                        const std::size_t at = RectIndex(dimension, cell[k].A, cell[k].B);
                                        corners[k] = LitCorner{points.at(at), quakes.at(at), uvs.at(at), lightmaps.at(at), lights.at(at)};
                                    }
                                    for (BuiltFace* lit : LitFaces(bsp, lighting, face, corners, shape->Normal(),
                                             material, shape->Shade(), sky))
                                    {
                                        co_yield lit;
                                    }
                                }
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
        const std::vector<Vector3>& lights,
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
            Shade(shade, sky),
            sky ? std::vector<Vector3>{} : std::vector<Vector3>{lights.at(i0), lights.at(i1), lights.at(i2)});
    }

    float Q3Import::Shade(float baked, bool sky) noexcept
    {
        return sky
            ? 1.0F
            : std::clamp(0.62F + baked * 0.75F, 0.55F, 1.0F);
    }

    Vector3 Q3Import::Light(const std::vector<std::uint8_t>* color)
    {
        // x4 is Quake's shift; the 1.22 brings this game's 5-bit, DS-style
        // modulation level with ioquake3's picture (measured on lightmapped
        // levels with no fog, ACID3DM12 and CSU1_A).
        // Halved for the textures baked twice as bright (MapTextureBake's
        // Overbright), which leaves light up to twice a texture's own.
        constexpr float Gain = 4.0F * 1.22F / 2.0F;
        float r = static_cast<float>(ManagedAt(color, 0)) * Gain;
        float g = static_cast<float>(ManagedAt(color, 1)) * Gain;
        float b = static_cast<float>(ManagedAt(color, 2)) * Gain;
        const float most = std::max(r, std::max(g, b));
        if (most > 255.0F)
        {
            r = r * 255.0F / most;
            g = g * 255.0F / most;
            b = b * 255.0F / most;
        }
        return Vector3(r / 255.0F, g / 255.0F, b / 255.0F);
    }

    BuiltFace* Q3Import::MakeFace(
        std::vector<Vector3> points,
        std::vector<Vector2> uvs,
        Vector3 normal,
        std::int32_t material,
        float shade,
        std::vector<Vector3> colors)
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
            if (colors.size() == 3)
            {
                std::swap(colors[1], colors[2]);
            }
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

        BuiltFace* made = new BuiltFace(
            std::move(pointArray),
            std::move(uvArray),
            normal,
            material,
            shade);

        if (colors.size() == points.size() && made != nullptr)

        {

            made->Colors = std::move(colors);

        }

        return made;
    }

    void Q3Import::AddCloudBox(
        BuiltMap* map,
        Vector3 min,
        Vector3 max,
        float unit,
        float cloudHeight,
        const std::vector<std::int32_t>& materials,
        const std::vector<std::pair<std::int32_t, std::int32_t>>& textureSizes)
    {
        // The same sides as AddSkyBox (tr_sky.c's st_to_vec).
        static constexpr std::array<std::array<std::int32_t, 3>, 6> StToVec{{
            {{3, -1, 2}}, {{-3, 1, 2}}, {{1, 3, 2}}, {{-1, -3, 2}}, {{-2, -1, 3}}, {{2, -1, -3}}
        }};
        constexpr std::int32_t Cells = 8;
        constexpr float Radius = 4096.0F;
        const Vector3 centre = Divide(Add(min, max), 2.0F);
        const Vector3 half = Divide(Subtract(max, min), 2.0F);
        const Vector3 centreWorld = ToWorld(centre, unit);
        // R_InitSkyTexCoords: the view along `d` meets a sphere of radius
        // 4096 + cloudHeight centred 4096 below the eye; s and t are the
        // angles of that point.
        const auto cloud = [&](float sx, float tx, std::size_t side)
        {
            const float source[3] = {sx, tx, 1.0F};
            float d[3]{};
            for (std::size_t j = 0; j < 3; ++j)
            {
                const std::int32_t k = StToVec[side][j];
                d[j] = k < 0 ? -source[-k - 1] : source[k - 1];
            }
            const float length2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
            const float h = cloudHeight;
            const float p = (-2.0F * d[2] * Radius + 2.0F * std::sqrt(d[2] * d[2] * Radius * Radius
                + length2 * (2.0F * Radius * h + h * h))) / (2.0F * length2);
            float v[3] = {d[0] * p, d[1] * p, d[2] * p + Radius};
            const float norm = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
            for (float& c : v)
            {
                c /= norm;
            }
            const float pi = 3.14159265358979F;
            return std::array<float, 5>{
                std::acos(std::clamp(v[0], -1.0F, 1.0F)) / pi,
                std::acos(std::clamp(v[1], -1.0F, 1.0F)) / pi,
                d[0], d[1], d[2]};
        };
        for (std::size_t layer = 0; layer < materials.size(); ++layer)
        {
            const std::int32_t material = materials[layer];
            if (material < 0 || static_cast<std::size_t>(material) >= textureSizes.size())
            {
                continue;
            }
            const auto [width, height] = textureSizes[static_cast<std::size_t>(material)];
            for (std::size_t side = 0; side < StToVec.size(); ++side)
            {
                for (std::int32_t row = 0; row < Cells; ++row)
                {
                    for (std::int32_t column = 0; column < Cells; ++column)
                    {
                        std::vector<Vector3> points;
                        std::vector<Vector2> texcoords;
                        for (const auto& [ds, dt] : std::array<std::array<std::int32_t, 2>, 4>{{
                                 {{0, 0}}, {{1, 0}}, {{1, 1}}, {{0, 1}}}})
                        {
                            const float s = -1.0F + 2.0F * static_cast<float>(column + ds) / Cells;
                            const float t = -1.0F + 2.0F * static_cast<float>(row + dt) / Cells;
                            const auto at = cloud(s, t, side);
                            points.push_back(ToWorld(Vector3(
                                centre.X + at[2] * half.X,
                                centre.Y + at[3] * half.Y,
                                centre.Z + at[4] * half.Z), unit));
                            texcoords.emplace_back(at[0] * static_cast<float>(width), at[1] * static_cast<float>(height));
                        }
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
                        face->Overlay = layer > 0;
                        face->Colors.assign(points.size(), Vector3(1.0F, 1.0F, 1.0F));
                        map->Faces().push_back(map->OwnFace(std::move(face)));
                    }
                }
            }
        }
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
            // OpenArena's pictures of what the level lacks, after its own and
            // ahead of the cartridge's stand-ins.
            std::vector<std::string> shaders;
            for (const auto& texture : bsp->Textures())
            {
                if (texture)
                {
                    shaders.push_back(texture->Name().Value());
                }
            }
            OpenArenaTextures::Fetch(*level, shaders);
            if (std::optional<std::string> openArena = OpenArenaTextures::Pack())
            {
                archivePaths->push_back(std::move(*openArena));
            }

            // Each portal's view, where CustomRooms keeps it once rendered.
            std::vector<std::string> portalImages;
            const std::filesystem::path directory = PathFromUtf8(target).parent_path();
            for (std::size_t i = 0; i < Portals(bsp.get()).size(); ++i)
            {
                portalImages.push_back(PathToUtf8(directory / ("portal_" + std::to_string(i) + ".png")));
            }
            // The movers' models' shaders (Q3Md3).
            std::vector<std::string> archiveList;
            for (const std::optional<std::string>& path : *archivePaths)
            {
                if (path.has_value())
                {
                    archiveList.push_back(*path);
                }
            }
            const std::vector<std::string> modelShaders = Q3Md3::Shaders(Q3Md3::Load(bsp.get(), archiveList));
            std::shared_ptr<MapTextureBake::Result> result
                = MapTextureBake::Bake(
                    bsp,
                    archivePaths,
                    std::optional<std::string>(target),
                    MapTextureBake::DefaultSize,
                    true,
                    &import->TextureAliases(),
                    &portalImages,
                    &modelShaders);
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
                for (const std::string& line : RequireReference(result->Substituted))
                {
                    WriteLine("    stand-in: " + line);
                }
                for (const std::string& line : RequireReference(result->Missing))
                {
                    WriteLine("    left out: " + line);
                }
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
            float,
            Q3StringHash,
            Q3StringEqual> targetAngles;
        // Where each named entity is, in Quake's own units and axes: the
        // pads aim at them and the teleporters send you to them.
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
                q3Targets[*name] = Vector3(
                    ManagedAt(raw.get(), 0), ManagedAt(raw.get(), 1), ManagedAt(raw.get(), 2));
                float angle = 0.0F;
                const std::string* value = EntityValue(entity, "angle");
                if (value != nullptr)
                {
                    (void)::MphRead::NativeRuntime::SingleTryParseInvariant(*value, angle);
                }
                else if (const std::string* angles = EntityValue(entity, "angles"))
                {
                    // "pitch yaw roll"
                    const std::shared_ptr<std::vector<float>> parsed = ParseVector(*angles);
                    angle = ManagedAt(parsed.get(), 1);
                }
                targetAngles[*name] = angle;
            }
        }

        // The level's gravity: worldspawn's "gravity", or g_gravity's 800.
        float q3Gravity = 800.0F;
        for (const std::shared_ptr<Q3Entity>& entityValue : bsp->Entities())
        {
            const Q3Entity* entity = &RequireReference(entityValue);
            const std::string* classname = EntityValue(entity, "classname");
            const std::string* gravity = EntityValue(entity, "gravity");
            float parsed = 0.0F;
            if (classname != nullptr && gravity != nullptr
                && StringEqualsOrdinalIgnoreCase(*classname, "worldspawn")
                && ::MphRead::NativeRuntime::SingleTryParseInvariant(*gravity, parsed)
                && parsed > 0.0F)
            {
                q3Gravity = parsed;
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
            else if (StringEqualsOrdinalIgnoreCase(*classname, "trigger_push")
                || StringEqualsOrdinalIgnoreCase(*classname, "trigger_teleport"))
            {
                // Both are brush volumes in Quake, and both become a trigger
                // that does what Quake's does (JumpPadEntity::Q3Trigger),
                // drawn by nothing but the level's own surfaces.
                const bool push = StringEqualsOrdinalIgnoreCase(*classname, "trigger_push");
                const std::string* model = EntityValue(entity, "model");
                const std::string* target = EntityValue(entity, "target");
                std::int32_t flags = 0;
                if (const std::string* spawnflags = EntityValue(entity, "spawnflags"))
                {
                    (void)Int32TryParseCurrentCulture(*spawnflags, flags);
                }
                std::int32_t modelIndex = 0;
                // Spawnflag 1 on a teleporter is a spectators-only one.
                if (model == nullptr
                    || !::MphRead::NativeRuntime::StringStartsWithCurrentCulture(*model, "*")
                    || target == nullptr
                    || (!push && (flags & 1) != 0)
                    || !q3Targets.contains(*target)
                    || !Int32TryParseCurrentCulture(std::string_view(*model).substr(1), modelIndex)
                    || modelIndex < 0
                    || static_cast<std::size_t>(modelIndex) >= bsp->Models().size())
                {
                    continue;
                }
                Q3Model* volume = &RequireReference(ManagedListAt(bsp->Models(), modelIndex));
                const std::vector<float>& q3Min = RequireReference(volume->Mins());
                const std::vector<float>& q3Max = RequireReference(volume->Maxs());
                const float unit = import->UnitsPerUnit();
                const Vector3 q3Centre(
                    (q3Min[0] + q3Max[0]) / 2.0F,
                    (q3Min[1] + q3Max[1]) / 2.0F,
                    (q3Min[2] + q3Max[2]) / 2.0F);

                // The floor under the trigger -- which often reaches into it
                // -- is where the trigger stands and where a bot walks to.
                float floorZ = q3Min[2];
                if (const std::optional<float> floor = Q3Floor(bsp, q3Centre.X, q3Centre.Y, q3Max[2]);
                    floor && *floor >= q3Min[2] - 1.0F)
                {
                    floorZ = *floor;
                }
                const Vector3 position = ToWorld(Vector3(q3Centre.X, q3Centre.Y, floorZ), unit);

                // Quake touches a trigger when the player's box overlaps it,
                // 15 units either side and from the feet to 56 above them
                // (G_TouchTriggers, bg_public's playerMins/Maxs); this game
                // tests the feet alone, so the volume is the trigger grown by
                // the player's box.
                // A pad on the ground reaches a step higher: a Quake player
                // drops into the hollow a pad is often set in -- a hunter's
                // round body rides over one narrower than itself -- and steps
                // up onto one at the crest of a slope, so a hunter at that
                // height is where a Quake player would have touched it. Not
                // a pad in mid-air, which a flight's path meets exactly.
                const bool grounded = push && q3Min[2] - floorZ < 8.0F;
                const Vector3 cornerA = ToWorld(
                    Vector3(q3Min[0] - 15.0F, q3Min[1] - 15.0F, q3Min[2] - 56.0F), unit);
                const Vector3 cornerB = ToWorld(
                    Vector3(q3Max[0] + 15.0F, q3Max[1] + 15.0F, q3Max[2] + (grounded ? 18.0F : 0.0F)), unit);
                const Vector3 boxMin = Subtract(ComponentMin(cornerA, cornerB), position);
                const Vector3 boxMax = Subtract(ComponentMax(cornerA, cornerB), position);

                auto pad = std::make_shared<MapJumpPad>();
                pad->Position(std::make_shared<std::vector<float>>(
                    std::initializer_list<float>{position.X, position.Y, position.Z}));
                pad->Box(std::make_shared<std::vector<float>>(
                    std::initializer_list<float>{
                        boxMin.X, boxMin.Y, boxMin.Z, boxMax.X, boxMax.Y, boxMax.Z}));
                // The level draws its own pad and teleporter, so the
                // cartridge's stay off: JumpPadEntity::HiddenModelId.
                pad->ModelId(0xFFFFFFFFU);
                // Quake sets the velocity every frame the player touches it.
                pad->CooldownTime(0);
                const Vector3 destination = q3Targets.at(*target);
                MapDefinition::JumpPadList* jumpPads = def->JumpPads();
                if (jumpPads == nullptr)
                {
                    throw System::NullReferenceException();
                }

                if (!push)
                {
                    // TeleportPlayer: at the destination, 1 above it, facing
                    // its angle, moving at 400 along it.
                    constexpr float DegreesToRadians = 0.017453292519943295769F;
                    pad->Q3Kind(2);
                    pad->Q3Value((90.0F + targetAngles[*target]) * DegreesToRadians);
                    const Vector3 feet = ToWorld(
                        Vector3(destination.X, destination.Y, destination.Z + 1.0F - 24.0F), unit);
                    pad->Target(std::make_shared<std::vector<float>>(
                        std::initializer_list<float>{feet.X, feet.Y, feet.Z}));
                    pad->Speed(400.0F / unit / 30.0F);
                    jumpPads->push_back(std::move(pad));
                    teleporters = UncheckedAdd(teleporters, 1);
                    continue;
                }

                // AimAtTarget: the target is the top of the arc, often over
                // the void, and the velocity is the one that reaches it from
                // the trigger's centre under the level's gravity. It is used
                // as it is -- the same speed and the same gravity, in this
                // game's units -- so the flight is Quake's in its path and in
                // its time.
                const float height = destination.Z - q3Centre.Z;
                if (height <= 0.0F)
                {
                    // Quake frees a pad it cannot aim.
                    continue;
                }
                const float time = std::sqrt(height / (0.5F * q3Gravity));
                const Vector3 velocity(
                    (destination.X - q3Centre.X) / time,
                    (destination.Y - q3Centre.Y) / time,
                    time * q3Gravity);
                const Vector3 world = ToWorld(velocity, unit * 30.0F);
                const float length = std::sqrt(LengthSquared(world));
                pad->Vector(std::make_shared<std::vector<float>>(
                    std::initializer_list<float>{
                        world.X / length, world.Y / length, world.Z / length}));
                pad->Speed(length);
                pad->Q3Kind(1);
                pad->Q3Value(q3Gravity / unit / 900.0F);
                pad->Q3Unit(static_cast<std::uint16_t>(std::lround(unit)));

                // Where the feet come down, from standing on the floor under
                // the trigger, for the bots' routes; and how long that takes,
                // which is how long the pad holds the player.
                Vector3 landing = ToWorld(destination, unit);
                float flight = 2.0F;
                for (std::int32_t step = 1; step < 60 * 12; ++step)
                {
                    const float t = static_cast<float>(step) / 60.0F;
                    if (velocity.Z - q3Gravity * t > 0.0F)
                    {
                        continue;
                    }
                    const Vector3 at(
                        q3Centre.X + velocity.X * t,
                        q3Centre.Y + velocity.Y * t,
                        floorZ + velocity.Z * t - 0.5F * q3Gravity * t * t);
                    const std::optional<float> floor = Q3Floor(bsp, at.X, at.Y, at.Z + 32.0F);
                    if (floor && at.Z <= *floor)
                    {
                        landing = ToWorld(Vector3(at.X, at.Y, *floor), unit);
                        flight = t;
                        break;
                    }
                }
                pad->ControlLockTime(static_cast<std::uint16_t>(std::ceil(flight * 30.0F)));
                pad->Target(std::make_shared<std::vector<float>>(
                    std::initializer_list<float>{landing.X, landing.Y, landing.Z}));
                jumpPads->push_back(std::move(pad));
                pads = UncheckedAdd(pads, 1);
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

        // A level that holds no weapon of its own -- a race map -- is given
        // this game's six, one each, spread as far from the starts and from
        // one another as the level's own places allow: where its pickups
        // stand, where its pads land and its teleporters lead.
        const auto isWeapon = [](const std::shared_ptr<MapItem>& item)
        {
            const std::string& type = RequireReference(item).Type();
            return type == ::MphRead::ToString(ItemType::VoltDriver)
                || type == ::MphRead::ToString(ItemType::Battlehammer)
                || type == ::MphRead::ToString(ItemType::Imperialist)
                || type == ::MphRead::ToString(ItemType::Judicator)
                || type == ::MphRead::ToString(ItemType::Magmaul)
                || type == ::MphRead::ToString(ItemType::ShockCoil);
        };
        if (import->KeepItems() && std::none_of(mapItems->begin(), mapItems->end(), isWeapon))
        {
            std::vector<Vector3> places;
            for (const Q3Pickup& pickup : pickups)
            {
                places.push_back(pickup.Position);
            }
            for (const std::shared_ptr<MapJumpPad>& pad : RequireReference(def->JumpPads()))
            {
                if (const std::vector<float>* target = RequireReference(pad).Target())
                {
                    places.emplace_back((*target)[0], (*target)[1], (*target)[2]);
                }
            }
            // Not on a pad or a teleporter -- a pad that throws straight up
            // lands on itself -- where it would sit on the trigger.
            const auto onTrigger = [&def](const Vector3& place)
            {
                for (const std::shared_ptr<MapJumpPad>& pad : RequireReference(def->JumpPads()))
                {
                    const std::vector<float>& at = RequireReference(RequireReference(pad).Position());
                    const float dx = place.X - at[0];
                    const float dz = place.Z - at[2];
                    if (dx * dx + dz * dz < 9.0F && std::fabs(place.Y - at[1]) < 3.0F)
                    {
                        return true;
                    }
                }
                return false;
            };
            places.erase(std::remove_if(places.begin(), places.end(), onTrigger), places.end());
            std::vector<Vector3> taken;
            for (const std::shared_ptr<MapSpawn>& spawn : RequireReference(def->Spawns()))
            {
                const std::vector<float>& at = RequireReference(RequireReference(spawn).Position());
                taken.emplace_back(at[0], at[1], at[2]);
            }
            constexpr ItemType Arsenal[] = {
                ItemType::Imperialist, ItemType::Magmaul, ItemType::Battlehammer,
                ItemType::ShockCoil, ItemType::Judicator, ItemType::VoltDriver};
            std::int32_t given = 0;
            for (const ItemType weapon : Arsenal)
            {
                float bestScore = 0.0F;
                std::optional<std::size_t> best;
                for (std::size_t i = 0; i < places.size(); ++i)
                {
                    float score = std::numeric_limits<float>::max();
                    for (const Vector3& other : taken)
                    {
                        score = std::min(score, LengthSquared(Subtract(places[i], other)));
                    }
                    if (score > bestScore)
                    {
                        bestScore = score;
                        best = i;
                    }
                }
                // Not on top of a start or of another weapon.
                if (!best.has_value() || bestScore < 4.0F)
                {
                    break;
                }
                auto item = std::make_shared<MapItem>();
                item->Position(std::make_shared<std::vector<float>>(
                    std::initializer_list<float>{places[*best].X, places[*best].Y, places[*best].Z}));
                item->Type(::MphRead::ToString(weapon));
                mapItems->push_back(std::move(item));
                taken.push_back(places[*best]);
                given = UncheckedAdd(given, 1);
            }
            if (verbose && given > 0)
            {
                WriteLine("  no weapons in the level: gave it " + Runtime::ToString(given));
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

    // A Quake 3 level's portals: each misc_portal_surface with the camera
    // its target names (misc_portal_camera), looking at that camera's own
    // target -- or along its angle -- turned by its roll. A surface with no
    // camera is a mirror, which this game has no way to draw: left out.
    std::vector<Q3Import::Q3Portal> Q3Import::Portals(Q3Bsp* bsp)
    {
        std::vector<Q3Portal> portals;
        if (bsp == nullptr)
        {
            return portals;
        }
        const auto named = [bsp](const std::string& name) -> const Q3Entity*
        {
            for (const std::shared_ptr<Q3Entity>& entity : bsp->Entities())
            {
                const std::string* value = EntityValue(entity.get(), "targetname");
                if (value != nullptr && *value == name)
                {
                    return entity.get();
                }
            }
            return nullptr;
        };
        const auto origin = [](const Q3Entity* entity) -> std::optional<Vector3>
        {
            const std::string* value = EntityValue(entity, "origin");
            if (value == nullptr)
            {
                return std::nullopt;
            }
            const std::shared_ptr<std::vector<float>> parsed = ParseVector(*value);
            return Vector3((*parsed)[0], (*parsed)[1], (*parsed)[2]);
        };
        for (const std::shared_ptr<Q3Entity>& entityValue : bsp->Entities())
        {
            const Q3Entity* entity = entityValue.get();
            const std::string* classname = EntityValue(entity, "classname");
            const std::string* target = EntityValue(entity, "target");
            if (classname == nullptr || target == nullptr
                || !StringEqualsOrdinalIgnoreCase(*classname, "misc_portal_surface"))
            {
                continue;
            }
            const Q3Entity* camera = named(*target);
            const std::optional<Vector3> surface = origin(entity);
            const std::optional<Vector3> eye = camera != nullptr ? origin(camera) : std::nullopt;
            if (!surface || !eye)
            {
                continue;
            }
            Q3Portal portal;
            portal.Surface = *surface;
            portal.Camera = *eye;
            std::optional<Vector3> aim;
            if (const std::string* cameraTarget = EntityValue(camera, "target"))
            {
                if (const Q3Entity* looked = named(*cameraTarget))
                {
                    aim = origin(looked);
                }
            }
            if (!aim)
            {
                float yaw = 0.0F;
                float pitch = 0.0F;
                if (const std::string* angles = EntityValue(camera, "angles"))
                {
                    const std::shared_ptr<std::vector<float>> parsed = ParseVector(*angles);
                    pitch = (*parsed)[0];
                    yaw = (*parsed)[1];
                }
                else if (const std::string* angle = EntityValue(camera, "angle"))
                {
                    (void)Runtime::SingleTryParseInvariant(*angle, yaw);
                }
                constexpr float DegreesToRadians = 0.017453292519943295769F;
                aim = Vector3(
                    eye->X + std::cos(yaw * DegreesToRadians) * std::cos(pitch * DegreesToRadians) * 64.0F,
                    eye->Y + std::sin(yaw * DegreesToRadians) * std::cos(pitch * DegreesToRadians) * 64.0F,
                    eye->Z - std::sin(pitch * DegreesToRadians) * 64.0F);
            }
            portal.Aim = *aim;
            if (const std::string* roll = EntityValue(camera, "roll"))
            {
                (void)Runtime::SingleTryParseInvariant(*roll, portal.Roll);
            }
            portal.Up = PortalUp(bsp, portal);
            portals.push_back(portal);
        }
        return portals;
    }

    // Which way is up through a portal, as Quake works it out: the surface's
    // axes and the camera's are each the normal (the camera's facing) and
    // q_math's PerpendicularVector of it -- some perpendicular, not the
    // world's up, which is why mappers roll their portal cameras -- the
    // camera's turned by its roll (cg_ents.c CG_Portal, tr_main.c
    // R_GetPortalOrientations), and a direction seen through the portal
    // carried from the one set of axes to the other (R_MirrorVector).
    OpenTK::Mathematics::Vector3 Q3Import::PortalUp(Q3Bsp* bsp, const Q3Portal& portal)
    {
        const auto perpendicular = [](Vector3 v)
        {
            const float values[3] = {std::fabs(v.X), std::fabs(v.Y), std::fabs(v.Z)};
            std::int32_t smallest = 0;
            float least = 1.0F;
            for (std::int32_t i = 0; i < 3; i++)
            {
                if (values[i] < least)
                {
                    smallest = i;
                    least = values[i];
                }
            }
            const Vector3 axis(smallest == 0 ? 1.0F : 0.0F, smallest == 1 ? 1.0F : 0.0F, smallest == 2 ? 1.0F : 0.0F);
            const float along = Vector3::Dot(axis, v) / Vector3::Dot(v, v);
            return Vector3(axis.X - v.X * along, axis.Y - v.Y * along, axis.Z - v.Z * along).Normalized();
        };
        // The portal's surface: the drawn face nearest its entity.
        Vector3 normal(0.0F, 0.0F, 1.0F);
        float best = std::numeric_limits<float>::max();
        for (const std::shared_ptr<Q3Face>& faceValue : bsp->Faces())
        {
            Q3Face* face = faceValue.get();
            if (face == nullptr || face->VertexCount() <= 0)
            {
                continue;
            }
            const Q3Texture* texture = &RequireReference(ManagedListAt(bsp->Textures(), face->Texture()));
            if (!IsPortalShader(texture->Name().Value()))
            {
                continue;
            }
            Vector3 centre(0.0F, 0.0F, 0.0F);
            for (std::int32_t i = 0; i < face->VertexCount(); ++i)
            {
                const std::vector<float>& at = RequireReference(RequireReference(
                    ManagedListAt(bsp->Vertices(), UncheckedAdd(face->Vertex(), i))).Position());
                centre = Add(centre, Vector3(at[0], at[1], at[2]));
            }
            centre = Divide(centre, static_cast<float>(face->VertexCount()));
            const float distance = LengthSquared(Subtract(centre, portal.Surface));
            if (distance < best)
            {
                best = distance;
                const std::vector<float>& n = RequireReference(face->Normal());
                normal = Vector3(n[0], n[1], n[2]);
            }
        }
        const Vector3 s0 = normal;
        const Vector3 s1 = perpendicular(s0);
        const Vector3 s2 = Vector3::Cross(s0, s1);

        Vector3 c0 = Subtract(portal.Aim, portal.Camera);
        if (LengthSquared(c0) < 1.0e-6F)
        {
            return Vector3(0.0F, 0.0F, 1.0F);
        }
        c0 = c0.Normalized();
        // cgame's axes (CG_Portal): the facing, the negated perpendicular,
        // and their cross product.
        const Vector3 p = perpendicular(c0);
        Vector3 c1(-p.X, -p.Y, -p.Z);
        Vector3 c2 = Vector3::Cross(c0, c1);
        // The renderer's (R_GetPortalOrientations): facing and side turned
        // round, then the side rolled about the turned facing -- the roll as
        // the byte cgame is sent and back (clientNum / 256 * 360) -- and the
        // third axis made again from the two.
        const Vector3 camera0(-c0.X, -c0.Y, -c0.Z);
        Vector3 camera1(-c1.X, -c1.Y, -c1.Z);
        const float roll = std::floor(portal.Roll / 360.0F * 256.0F) / 256.0F * 360.0F;
        if (roll != 0.0F)
        {
            constexpr float DegreesToRadians = 0.017453292519943295769F;
            const float angle = roll * DegreesToRadians;
            const Vector3 across = Vector3::Cross(camera0, camera1);
            const float along = Vector3::Dot(camera0, camera1);
            camera1 = Vector3(
                camera1.X * std::cos(angle) + across.X * std::sin(angle) + camera0.X * along * (1.0F - std::cos(angle)),
                camera1.Y * std::cos(angle) + across.Y * std::sin(angle) + camera0.Y * along * (1.0F - std::cos(angle)),
                camera1.Z * std::cos(angle) + across.Z * std::sin(angle) + camera0.Z * along * (1.0F - std::cos(angle)));
            c2 = Vector3::Cross(camera0, camera1);
        }

        // Up for somebody facing the surface: the world's, along the surface.
        Vector3 up(0.0F, 0.0F, 1.0F);
        up = Subtract(up, Vector3(s0.X * Vector3::Dot(up, s0), s0.Y * Vector3::Dot(up, s0), s0.Z * Vector3::Dot(up, s0)));
        if (LengthSquared(up) < 1.0e-6F)
        {
            up = Vector3(1.0F, 0.0F, 0.0F);
        }
        up = up.Normalized();
        const float d0 = Vector3::Dot(up, s0);
        const float d1 = Vector3::Dot(up, s1);
        const float d2 = Vector3::Dot(up, s2);
        return Vector3(
            camera0.X * d0 + camera1.X * d1 + c2.X * d2,
            camera0.Y * d0 + camera1.Y * d1 + c2.Y * d2,
            camera0.Z * d0 + camera1.Z * d1 + c2.Z * d2);
    }

    bool Q3Import::IsPortalShader(const std::string& shader)
    {
        std::string lower = shader;
        for (char& ch : lower)
        {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        return lower.find("portal") != std::string::npos;
    }

    // The portal's face, painted with the view from its camera, the image
    // filling the face the way it is seen from in front of it.
    void Q3Import::AddPortalFace(
        BuiltMap* map, Q3Bsp* bsp, Q3Face* face, float unit,
        std::int32_t material, std::int32_t width, std::int32_t height)
    {
        std::vector<Vector3> points;
        for (std::int32_t i = 0; i < face->VertexCount(); ++i)
        {
            Q3Vertex* vertex = &RequireReference(ManagedListAt(bsp->Vertices(), UncheckedAdd(face->Vertex(), i)));
            points.push_back(ToWorld(&RequireReference(vertex->Position()), unit));
        }
        if (points.size() < 3)
        {
            return;
        }
        const Vector3 normal = ToDirection(&RequireReference(face->Normal()));
        const Vector3 forward(-normal.X, -normal.Y, -normal.Z);
        Vector3 right = Vector3::Cross(forward, Vector3(0.0F, 1.0F, 0.0F));
        if (LengthSquared(right) < 1.0e-4F)
        {
            right = Vector3(1.0F, 0.0F, 0.0F);
        }
        right = right.Normalized();
        const Vector3 up = Vector3::Cross(right, forward).Normalized();
        float uLow = std::numeric_limits<float>::max();
        float uHigh = std::numeric_limits<float>::lowest();
        float vLow = std::numeric_limits<float>::max();
        float vHigh = std::numeric_limits<float>::lowest();
        for (const Vector3 point : points)
        {
            const float u = Vector3::Dot(point, right);
            const float v = Vector3::Dot(point, up);
            uLow = std::min(uLow, u);
            uHigh = std::max(uHigh, u);
            vLow = std::min(vLow, v);
            vHigh = std::max(vHigh, v);
        }
        const float uSpan = std::max(uHigh - uLow, 1.0e-4F);
        const float vSpan = std::max(vHigh - vLow, 1.0e-4F);
        const auto uv = [&](Vector3 point)
        {
            return Vector2(
                (Vector3::Dot(point, right) - uLow) / uSpan * static_cast<float>(width),
                (vHigh - Vector3::Dot(point, up)) / vSpan * static_cast<float>(height));
        };
        for (std::int32_t i = 0; UncheckedAdd(i, 2) < face->MeshVertCount(); i = UncheckedAdd(i, 3))
        {
            std::vector<Vector3> corners(3);
            std::vector<Vector2> uvs(3);
            for (std::int32_t j = 0; j < 3; ++j)
            {
                const std::int32_t offset = ManagedListAt(bsp->MeshVerts(), UncheckedAdd(UncheckedAdd(face->MeshVert(), i), j));
                corners[static_cast<std::size_t>(j)] = points.at(static_cast<std::size_t>(offset));
                uvs[static_cast<std::size_t>(j)] = uv(corners[static_cast<std::size_t>(j)]);
            }
            BuiltFace* built = MakeFace(std::move(corners), std::move(uvs), normal, material, Shade(1.0F, true));
            std::unique_ptr<BuiltFace> owner(built);
            map->Faces().push_back(map->OwnFace(std::move(owner)));
        }
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

        // An ammo box is worth as many shots of the weapon this game puts in
        // place of Quake's as it held of Quake's own, priced at that weapon's
        // UA cost (the multiplayer table, -mechanics): 100 and more is the
        // big UA pickup, less the small one. A box for a weapon the level
        // does not hold -- and the machine gun's, whose Power Beam needs
        // none -- is missiles instead, at 10 a shot, the other weapon every
        // hunter carries.
        struct AmmoBox
        {
            const char* Classname;
            const char* Weapon;
            std::int32_t Shots;
            std::int32_t Cost;
        };
        static constexpr AmmoBox AmmoBoxes[] = {
            {"ammo_slugs", "weapon_railgun", 10, 20},
            {"ammo_rockets", "weapon_rocketlauncher", 5, 10},
            {"ammo_cells", "weapon_plasmagun", 30, 4},
            {"ammo_lightning", "weapon_lightning", 60, 10},
            {"ammo_grenades", "weapon_grenadelauncher", 5, 5},
            {"ammo_shells", "weapon_shotgun", 10, 5},
            {"ammo_nails", "weapon_nailgun", 20, 5},
            {"ammo_belt", "weapon_chaingun", 100, 4},
            {"ammo_mines", "weapon_prox_launcher", 10, 5},
            {"ammo_bullets", nullptr, 50, 0},
        };

        std::vector<std::string> placed;
        for (const std::shared_ptr<Q3Entity>& entityValue : bsp->Entities())
        {
            const std::string* classname = EntityValue(entityValue.get(), "classname");
            if (classname != nullptr && MapItemType(*classname) != ItemType::None)
            {
                placed.push_back(Runtime::ToLowerInvariant(*classname));
            }
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
            // Only what a free-for-all places: "notfree" keeps an item to the
            // team games.
            if (const std::string* notFree = EntityValue(entity, "notfree");
                notFree != nullptr && *notFree != "0")
            {
                continue;
            }

            ItemType type = MapItemType(*classname);
            for (const AmmoBox& box : AmmoBoxes)
            {
                if (!Q3StringEqual{}(*classname, box.Classname))
                {
                    continue;
                }
                const bool held = box.Weapon != nullptr
                    && std::find(placed.begin(), placed.end(), box.Weapon) != placed.end();
                const std::int32_t amount = box.Shots * (held ? box.Cost : 10);
                type = held
                    ? (amount >= 100 ? ItemType::UABig : ItemType::UASmall)
                    : (amount >= 100 ? ItemType::MissileBig : ItemType::MissileSmall);
                break;
            }
            if (type == ItemType::None)
            {
                continue;
            }

            // Quake drops an item to the floor under it unless spawnflag 1
            // hangs it where it was placed (FinishSpawningItem).
            std::shared_ptr<std::vector<float>> at = ParseVector(*origin);
            std::int32_t flags = 0;
            if (const std::string* spawnflags = EntityValue(entity, "spawnflags"))
            {
                (void)Int32TryParseCurrentCulture(*spawnflags, flags);
            }
            if ((flags & 1) == 0)
            {
                if (const std::optional<float> floor = Q3Floor(bsp,
                        ManagedAt(at.get(), 0), ManagedAt(at.get(), 1), ManagedAt(at.get(), 2));
                    floor.has_value())
                {
                    ManagedAt(at.get(), 2) = *floor;
                }
            }

            const std::string* targetName = EntityValue(entity, "targetname");
            pickups.push_back(Q3Pickup{*classname, type, ToWorld(at.get(), unitsPerUnit),
                targetName != nullptr ? std::optional<std::string>(*targetName) : std::nullopt});
        }
        return pickups;
    }

    // What stands in for Quake's weapons, health and armour. Weapons go by
    // what they do: the rail by the sniper's Imperialist, rockets by the
    // Magmaul's explosive shots, the plasma gun's stream of splash by the
    // Battlehammer, the lightning gun by the Shock Coil, grenades that bounce
    // by the Judicator's, the shotgun by the Volt Driver's charged burst.
    // Nothing for the BFG -- the Omega Cannon is out of custom maps -- nor
    // the machine gun and gauntlet every hunter's Power Beam already is.
    // There is no armour here, health being the shield, so armour is health
    // of the same size -- except the 5-point bubble and shard, which nothing
    // here is as small as: those are left out. Powerups and holdables are
    // nothing: custom maps carry none. Ammo is Pickups' affair.
    ItemType Q3Import::MapItemType(
        const std::string& classname) noexcept
    {
        const auto equal = [&classname](const char* value) noexcept
        {
            return Q3StringEqual{}(classname, value);
        };

        if (equal("weapon_railgun")) return ItemType::Imperialist;
        if (equal("weapon_rocketlauncher")) return ItemType::Magmaul;
        if (equal("weapon_plasmagun")) return ItemType::Battlehammer;
        if (equal("weapon_lightning")) return ItemType::ShockCoil;
        if (equal("weapon_grenadelauncher")) return ItemType::Judicator;
        if (equal("weapon_shotgun")) return ItemType::VoltDriver;
        if (equal("weapon_nailgun")) return ItemType::VoltDriver;
        if (equal("weapon_chaingun")) return ItemType::Battlehammer;
        if (equal("weapon_prox_launcher")) return ItemType::Judicator;
        if (equal("item_health")) return ItemType::HealthMedium;
        if (equal("item_health_large")) return ItemType::HealthBig;
        if (equal("item_health_mega")) return ItemType::HealthBig;
        if (equal("item_armor_combat")) return ItemType::HealthMedium;
        if (equal("item_armor_body")) return ItemType::HealthBig;
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
