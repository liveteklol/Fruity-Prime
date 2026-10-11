#pragma once

#include "../../Formats/Enums.hpp"
#include "../../Formats/Types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
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

        [[nodiscard]] static MphRead::Enumerable<BuiltFace*> Triangles(
            Q3Bsp* bsp, Q3Face* face, float unit,
            std::int32_t width, std::int32_t height,
            std::int32_t material, bool sky);

        [[nodiscard]] static MphRead::Enumerable<BuiltFace*> Tessellate(
            Q3Bsp* bsp, Q3Face* face, float unit,
            std::int32_t width, std::int32_t height,
            std::int32_t material, bool sky, std::int32_t level);

        [[nodiscard]] static std::tuple<float, float, float> Weights(float t) noexcept;

        [[nodiscard]] static BuiltFace* Cell(
            const std::vector<OpenTK::Mathematics::Vector3>& points,
            const std::vector<OpenTK::Mathematics::Vector2>& uvs,
            const std::vector<OpenTK::Mathematics::Vector3>& normals,
            const std::vector<float>& shades,
            std::int32_t dimension,
            std::int32_t material,
            bool sky,
            GridPoint p0,
            GridPoint p1,
            GridPoint p2);

        [[nodiscard]] static float Shade(float baked, bool sky) noexcept;

        [[nodiscard]] static BuiltFace* MakeFace(
            std::vector<OpenTK::Mathematics::Vector3> points,
            std::vector<OpenTK::Mathematics::Vector2> uvs,
            OpenTK::Mathematics::Vector3 normal,
            std::int32_t material,
            float shade);

        static void ProjectSky(BuiltFace* face, float texelsPerUnit);

        // The six sides of a Quake sky box as a box of drawn faces spanning
        // [min, max] (Quake units and axes), each side its own material.
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
