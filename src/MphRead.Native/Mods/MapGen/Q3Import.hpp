#pragma once

#include "../../Formats/Enums.hpp"
#include "../../Formats/Types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace MphRead
{
    template <typename T>
    class Enumerable;
}

namespace MphRead::Mods::MapGen
{
    class BuiltFace;
    class BuiltMap;
    class MapDefinition;
    class MapImport;
    class Q3Bsp;
    class Q3Brush;
    class Q3Face;

    class Q3Import final
    {
        friend class Q3Convert;
        friend class CustomRooms;
    public:
        [[nodiscard]] static std::shared_ptr<BuiltMap> Build(
            MapDefinition* def, bool verbose = true);

        // One of the level's pickups: what it is in Quake, what this game has
        // in its place, where that lands, and the name the level's scripts
        // hand it out by, when they do.
        struct Q3Pickup
        {
            std::string Classname{};
            ItemType Type{};
            OpenTK::Mathematics::Vector3 Position{};
            std::optional<std::string> TargetName{};
        };

        [[nodiscard]] static std::vector<Q3Pickup> Pickups(Q3Bsp* bsp, float unitsPerUnit);

        // A portal of the level, in Quake's units and axes: where its
        // surface entity stands, where its camera is and what it looks at,
        // and the camera's roll in degrees.
        struct Q3Portal
        {
            OpenTK::Mathematics::Vector3 Surface{};
            OpenTK::Mathematics::Vector3 Camera{};
            OpenTK::Mathematics::Vector3 Aim{};
            float Roll = 0.0F;
            // Up in the view through it, seen from straight in front.
            OpenTK::Mathematics::Vector3 Up{0.0F, 0.0F, 1.0F};
        };
        [[nodiscard]] static std::vector<Q3Portal> Portals(Q3Bsp* bsp);
        [[nodiscard]] static bool IsPortalShader(const std::string& shader);
        [[nodiscard]] static OpenTK::Mathematics::Vector3 PortalUp(Q3Bsp* bsp, const Q3Portal& portal);
        // A point in Quake's units and axes, in this game's.
        [[nodiscard]] static OpenTK::Mathematics::Vector3 WorldPoint(
            OpenTK::Mathematics::Vector3 position, float unit) noexcept { return ToWorld(position, unit); }

        // The top of the highest solid world brush below (x, y, z), in Quake
        // units and axes; none when nothing is under it.
        [[nodiscard]] static std::optional<float> Floor(Q3Bsp* bsp, float x, float y, float z);

        [[nodiscard]] static std::optional<std::string> BakeTextures(
            const std::shared_ptr<Q3Bsp>& bsp, MapImport* import, bool verbose);

        Q3Import() = delete;
        Q3Import(const Q3Import&) = delete;
        Q3Import& operator=(const Q3Import&) = delete;

    private:
        [[nodiscard]] static OpenTK::Mathematics::Vector3 TriangleNormal(
            const std::vector<OpenTK::Mathematics::Vector3>& points, OpenTK::Mathematics::Vector3 vertexNormal,
            OpenTK::Mathematics::Vector3 faceNormal);
        struct GridPoint final
        {
            std::int32_t A = 0;
            std::int32_t B = 0;
        };

        struct BrushPolygon final
        {
            std::vector<OpenTK::Mathematics::Vector3> Points;
            OpenTK::Mathematics::Vector3 Normal;
        };

        struct BrushBounds final
        {
            OpenTK::Mathematics::Vector3 Min;
            OpenTK::Mathematics::Vector3 Max;
        };

        struct BrushSideEntry final
        {
            std::int32_t Brush = 0;
            std::vector<OpenTK::Mathematics::Vector3> Points;
            OpenTK::Mathematics::Vector3 Normal;
        };

        struct CellKey final
        {
            std::int32_t X = 0;
            std::int32_t Y = 0;
            std::int32_t Z = 0;

            [[nodiscard]] bool operator==(const CellKey& other) const noexcept;
        };

        struct CellKeyHash final
        {
            [[nodiscard]] std::size_t operator()(const CellKey& value) const noexcept;
        };

        using BrushLookup = std::unordered_map<CellKey, std::vector<std::int32_t>, CellKeyHash>;

        static constexpr float SkyTiles = 2.0F;
        static constexpr float BrushCellSize = 512.0F;

        // A corner of a triangle on its way to being drawn: where it is in
        // this game and in Quake, its texture and lightmap coordinates, and
        // the light at it.
        struct LitCorner
        {
            OpenTK::Mathematics::Vector3 World{};
            OpenTK::Mathematics::Vector3 Quake{};
            OpenTK::Mathematics::Vector2 Uv{};
            OpenTK::Mathematics::Vector2 Lightmap{};
            OpenTK::Mathematics::Vector3 Color{1.0F, 1.0F, 1.0F};
        };
    public:
        // A fog volume: its colour and the depth it is opaque at (fogparms),
        // its brush's planes and the one it is seen through, Quake's axes.
        struct FogVolume
        {
            OpenTK::Mathematics::Vector3 Color{};
            float Distance = 0.0F;
            std::vector<OpenTK::Mathematics::Vector4> Planes{};
            std::optional<OpenTK::Mathematics::Vector4> Surface{};
            // A fog filling most of the level, which a player stands in and
            // looks through: drawn as the room's distance fog, not on its
            // surfaces (Q3Convert::Translate).
            bool Room = false;
        };
        struct Lighting
        {
            std::vector<FogVolume> Fogs{};
            // The light layer: the material of page 0's layer (-1 none), the
            // brightest light of each face's block of a page (page, x, y,
            // width, height), and the pages.
            std::int32_t LayerMaterial = -1;
            std::map<std::array<std::int32_t, 5>, OpenTK::Mathematics::Vector3> Brightest{};
            std::vector<std::vector<std::uint8_t>> Pages{};
        };
        // The level's lightmap as a layer drawn over its faces: each face lit
        // by the brightest of its own light, and the layer darkening it luxel
        // by luxel to the rest, as sharp as Quake's own -- the pools of light
        // on a floor, a torch's glow, a pillar's shadow -- where light at the
        // corners of its triangles can hold none of them.
        static void LightLayer(Lighting& lighting, Q3Bsp* bsp);
        [[nodiscard]] static Lighting MakeLighting(Q3Bsp* bsp, const std::vector<std::string>& archives);
    private:
        [[nodiscard]] static OpenTK::Mathematics::Vector3 SampleLightmap(
            Q3Bsp* bsp, std::int32_t page, OpenTK::Mathematics::Vector2 st);
        [[nodiscard]] static std::vector<BuiltFace*> LitFaces(
            Q3Bsp* bsp, const Lighting* lighting, Q3Face* face,
            std::array<LitCorner, 3> corners, OpenTK::Mathematics::Vector3 normal,
            std::int32_t material, float shade, bool sky);

        [[nodiscard]] static MphRead::Enumerable<BuiltFace*> Triangles(
            Q3Bsp* bsp, Q3Face* face, float unit,
            std::int32_t width, std::int32_t height,
            std::int32_t material, bool sky, const Lighting* lighting);

        [[nodiscard]] static MphRead::Enumerable<BuiltFace*> Tessellate(
            Q3Bsp* bsp, Q3Face* face, float unit,
            std::int32_t width, std::int32_t height,
            std::int32_t material, bool sky, std::int32_t level, const Lighting* lighting);

        [[nodiscard]] static std::tuple<float, float, float> Weights(float t) noexcept;

        [[nodiscard]] static BuiltFace* Cell(
            const std::vector<OpenTK::Mathematics::Vector3>& points,
            const std::vector<OpenTK::Mathematics::Vector2>& uvs,
            const std::vector<OpenTK::Mathematics::Vector3>& normals,
            const std::vector<float>& shades,
            const std::vector<OpenTK::Mathematics::Vector3>& lights,
            std::int32_t dimension,
            std::int32_t material,
            bool sky,
            GridPoint p0,
            GridPoint p1,
            GridPoint p2);

        // A Quake vertex colour as the light it gives: brightened as
        // R_ColorShiftLightingBytes does (mapOverBrightBits 2, no hardware
        // gamma), a channel past full bringing the others down with it.
        [[nodiscard]] static OpenTK::Mathematics::Vector3 Light(const std::vector<std::uint8_t>* color);
        [[nodiscard]] static float Shade(float baked, bool sky) noexcept;

        [[nodiscard]] static BuiltFace* MakeFace(
            std::vector<OpenTK::Mathematics::Vector3> points,
            std::vector<OpenTK::Mathematics::Vector2> uvs,
            OpenTK::Mathematics::Vector3 normal,
            std::int32_t material,
            float shade,
            std::vector<OpenTK::Mathematics::Vector3> colors = {});

        static void ProjectSky(BuiltFace* face, float texelsPerUnit);

        // The six sides of a Quake sky box as a box of drawn faces spanning
        // [min, max] (Quake units and axes), each side its own material.
        // Quake's cloud sky (tr_sky.c): a box round the level, each side cut
        // 8 by 8, its texcoords where the view through each point meets a
        // dome cloudHeight high; the base and then each layer over it.
        static void AddCloudBox(
            BuiltMap* map,
            OpenTK::Mathematics::Vector3 min,
            OpenTK::Mathematics::Vector3 max,
            float unit,
            float cloudHeight,
            const std::vector<std::int32_t>& materials,
            const std::vector<std::pair<std::int32_t, std::int32_t>>& textureSizes);
        static void AddSkyBox(
            BuiltMap* map,
            OpenTK::Mathematics::Vector3 min,
            OpenTK::Mathematics::Vector3 max,
            float unit,
            const std::array<std::int32_t, 6>& materials,
            const std::vector<std::pair<std::int32_t, std::int32_t>>& textureSizes);

        [[nodiscard]] static bool Inside(
            OpenTK::Mathematics::Vector3 point,
            OpenTK::Mathematics::Vector3 min,
            OpenTK::Mathematics::Vector3 max) noexcept;

        [[nodiscard]] static bool IsSky(Q3Bsp* bsp, Q3Brush* brush);

        static void Bounds(
            BuiltMap* map,
            OpenTK::Mathematics::Vector3& min,
            OpenTK::Mathematics::Vector3& max);

        [[nodiscard]] static std::vector<OpenTK::Mathematics::Vector2> Rebase(
            const std::vector<OpenTK::Mathematics::Vector2>& uvs);

        [[nodiscard]] static std::vector<std::pair<std::int32_t, std::int32_t>>
            GetTextureSizes(MapDefinition* def);

        [[nodiscard]] static std::int32_t MatchMaterial(
            MapImport* import, const std::string& shader);

        [[nodiscard]] static std::vector<BrushPolygon> BrushSides(
            Q3Bsp* bsp, Q3Brush* brush);

        [[nodiscard]] static BrushLookup BuildBrushLookup(
            const std::vector<BrushBounds>& bounds);

        [[nodiscard]] static std::int32_t Cell(float value) noexcept;

        [[nodiscard]] static bool IsBuried(
            const std::vector<OpenTK::Mathematics::Vector3>& points,
            OpenTK::Mathematics::Vector3 normal,
            std::int32_t owner,
            const std::vector<std::vector<OpenTK::Mathematics::Vector4>>& brushes,
            const std::vector<BrushBounds>& bounds,
            const BrushLookup& lookup);

        [[nodiscard]] static bool Covered(
            OpenTK::Mathematics::Vector3 point,
            std::int32_t owner,
            const std::vector<std::vector<OpenTK::Mathematics::Vector4>>& brushes,
            const std::vector<BrushBounds>& bounds,
            const BrushLookup& lookup);

        [[nodiscard]] static std::vector<OpenTK::Mathematics::Vector3> MakeSheet(
            OpenTK::Mathematics::Vector3 normal, float distance);

        [[nodiscard]] static std::vector<OpenTK::Mathematics::Vector3> Clip(
            const std::vector<OpenTK::Mathematics::Vector3>& points,
            OpenTK::Mathematics::Vector3 normal,
            float distance);

        [[nodiscard]] static std::vector<OpenTK::Mathematics::Vector3> Weld(
            const std::vector<OpenTK::Mathematics::Vector3>& points);

        static void AddPortalFace(
            BuiltMap* map, Q3Bsp* bsp, Q3Face* face, float unit,
            std::int32_t material, std::int32_t width, std::int32_t height);

        static void AddEntities(
            BuiltMap* map, MapDefinition* def, Q3Bsp* bsp,
            MapImport* import, bool verbose);

        [[nodiscard]] static MphRead::ItemType MapItemType(
            const std::string& classname) noexcept;

        [[nodiscard]] static std::shared_ptr<std::vector<float>> ParseVector(
            const std::string& value);

        [[nodiscard]] static OpenTK::Mathematics::Vector3 ToWorld(
            const std::vector<float>* position, float unit);

        [[nodiscard]] static OpenTK::Mathematics::Vector3 ToWorld(
            OpenTK::Mathematics::Vector3 position, float unit) noexcept;

        [[nodiscard]] static OpenTK::Mathematics::Vector3 ToDirection(
            const std::vector<float>* direction);
    };
}
