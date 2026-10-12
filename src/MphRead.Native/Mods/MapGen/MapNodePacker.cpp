#include "MapNodePacker.hpp"

#include "../../Formats/NodeData.hpp"
#include "BuiltMap.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <deque>
#include <limits>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include "../../NativeRuntime/System/IO.hpp"
#include "../../Formats/Types.hpp"
#include "../../NativeRuntime/System/Console.hpp"
#include "../../NativeRuntime/System/Managed.hpp"
#include "../../NativeRuntime/OpenTK/Mathematics.hpp"
#include "../../NativeRuntime/System/Sort.hpp"

using ::MphRead::NativeRuntime::EnvironmentGetVariable;
using ::MphRead::NativeRuntime::MathMax;
using ::MphRead::NativeRuntime::MathMin;
using ::MphRead::NativeRuntime::RoundToEven;
using ::MphRead::NativeRuntime::UncheckedAdd;
using ::MphRead::NativeRuntime::UncheckedMultiply;
using ::OpenTK::Mathematics::Add;
using ::OpenTK::Mathematics::ComponentMax;
using ::OpenTK::Mathematics::ComponentMin;
using ::OpenTK::Mathematics::Length;
using ::OpenTK::Mathematics::Multiply;
using ::OpenTK::Mathematics::Subtract;

using ::MphRead::NativeRuntime::ManagedSort;
namespace MphRead::Mods::MapGen
{
    namespace
    {
        using OpenTK::Mathematics::Vector3;

        constexpr float FineSpacing = 2.0F;
        constexpr float Spacing = 6.0F;
        constexpr float Headroom = 1.9F;
        constexpr float StepUp = 1.2F;
        // A riser a hunter steps over (Quake's 18 units, PlayerInput's
        // TryQ3StepUp): a stair is not a wall.
        constexpr float StepHeight = 0.55F;
        constexpr float StepDown = 8.0F;
        constexpr float WalkNormal = 0.7F;
        constexpr std::int32_t MaxNeighbours = 16;
        constexpr std::int32_t MaxDrops = 8;
        constexpr float Reach = 1.6F;
        constexpr std::int32_t MaxNodes = 700;

        struct Node final
        {
            Vector3 Position{};
            std::int32_t Cell = 0;
            std::vector<std::int32_t> Neighbours{};
        };

        struct BucketGrid final
        {
            std::vector<std::unique_ptr<std::vector<std::int32_t>>> Buckets{};
            std::int32_t Columns = 0;
            std::int32_t Rows = 0;
        };

        using Route = std::pair<std::uint16_t, std::uint16_t>;
        using RoutesForNode = std::vector<Route>;

        [[nodiscard]] std::int32_t UncheckedInt32(float value) noexcept
        {
            if (std::isnan(value))
            {
                return 0;
            }
            const double wide = static_cast<double>(value);
            if (wide < static_cast<double>(std::numeric_limits<std::int32_t>::min()))
            {
                return std::numeric_limits<std::int32_t>::min();
            }
            if (wide > static_cast<double>(std::numeric_limits<std::int32_t>::max()))
            {
                return std::numeric_limits<std::int32_t>::max();
            }
            return static_cast<std::int32_t>(std::trunc(wide));
        }

        [[nodiscard]] float LinqMin(
            const MphRead::ManagedArray<Vector3>* points,
            float Vector3::* component)
        {
            if (points->Length() == 0)
            {
                throw System::InvalidOperationException("Sequence contains no elements");
            }
            float value = (*points)[0].*component;
            if (std::isnan(value))
            {
                return value;
            }
            for (std::size_t i = 1; i < points->Length(); i++)
            {
                const float current = (*points)[i].*component;
                if (current < value)
                {
                    value = current;
                }
                else if (std::isnan(current))
                {
                    return current;
                }
            }
            return value;
        }

        [[nodiscard]] float LinqMax(
            const MphRead::ManagedArray<Vector3>* points,
            float Vector3::* component)
        {
            if (points->Length() == 0)
            {
                throw System::InvalidOperationException("Sequence contains no elements");
            }
            std::size_t i = 0;
            float value = (*points)[i].*component;
            while (std::isnan(value))
            {
                i++;
                if (i == points->Length())
                {
                    return value;
                }
                value = (*points)[i].*component;
            }
            i++;
            while (i < points->Length())
            {
                const float current = (*points)[i].*component;
                if (current > value)
                {
                    value = current;
                }
                i++;
            }
            return value;
        }

        [[nodiscard]] std::unique_ptr<std::vector<std::int32_t>>& BucketAt(
            BucketGrid& grid, std::int32_t index)
        {
            if (index < 0 || static_cast<std::size_t>(index) >= grid.Buckets.size())
            {
                throw System::IndexOutOfRangeException();
            }
            return grid.Buckets[static_cast<std::size_t>(index)];
        }

        [[nodiscard]] const std::unique_ptr<std::vector<std::int32_t>>& BucketAt(
            const BucketGrid& grid, std::int32_t index)
        {
            if (index < 0 || static_cast<std::size_t>(index) >= grid.Buckets.size())
            {
                throw System::IndexOutOfRangeException();
            }
            return grid.Buckets[static_cast<std::size_t>(index)];
        }

        [[nodiscard]] const MphRead::ManagedArray<Vector3>* Points(const BuiltFace* face)
        {
            if (face == nullptr)
            {
                throw System::NullReferenceException();
            }
            const auto* points = face->Points();
            if (points == nullptr)
            {
                throw System::NullReferenceException();
            }
            return reinterpret_cast<const MphRead::ManagedArray<Vector3>*>(points);
        }

        [[nodiscard]] Vector3 Normal(const BuiltFace* face)
        {
            if (face == nullptr)
            {
                throw System::NullReferenceException();
            }
            return face->Normal();
        }

        [[nodiscard]] bool ContainsValue(
            const std::vector<std::int32_t>& values, std::int32_t value) noexcept
        {
            return std::find(values.begin(), values.end(), value) != values.end();
        }

        void Bounds(
            const std::vector<BuiltFace*>* solid,
            Vector3& min,
            Vector3& max)
        {
            if (solid == nullptr)
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
            for (const BuiltFace* face : *solid)
            {
                const auto* points = Points(face);
                for (std::size_t i = 0; i < points->Length(); i++)
                {
                    const Vector3 point = (*points)[i];
                    min = ComponentMin(min, point);
                    max = ComponentMax(max, point);
                }
            }
        }

        [[nodiscard]] bool Contains(
            const BuiltFace* face,
            float x,
            float z,
            float& y)
        {
            y = 0.0F;
            const auto* points = Points(face);
            bool inside = false;
            if (points->Length() > 0)
            {
                std::size_t j = points->Length() - 1;
                for (std::size_t i = 0; i < points->Length(); j = i++)
                {
                    const Vector3 pi = (*points)[i];
                    const Vector3 pj = (*points)[j];
                    if ((pi.Z > z) != (pj.Z > z)
                        && x < ((pj.X - pi.X) * (z - pi.Z) / (pj.Z - pi.Z)) + pi.X)
                    {
                        inside = !inside;
                    }
                }
            }
            const Vector3 normal = Normal(face);
            if (!inside || std::fabs(normal.Y) < 0.0001F)
            {
                return false;
            }
            const float d = Vector3::Dot(normal, (*points)[0]);
            y = (d - (normal.X * x) - (normal.Z * z)) / normal.Y;
            return true;
        }

        [[nodiscard]] BucketGrid Buckets(
            const std::vector<BuiltFace*>* solid,
            Vector3 min,
            float spacing)
        {
            static_cast<void>(min);
            Vector3 low{};
            Vector3 high{};
            Bounds(solid, low, high);
            const std::int32_t columnsRaw = UncheckedAdd(
                UncheckedInt32(std::ceil((high.X - low.X) / spacing)), 1);
            const std::int32_t rowsRaw = UncheckedAdd(
                UncheckedInt32(std::ceil((high.Z - low.Z) / spacing)), 1);
            const std::int32_t columns = std::max<std::int32_t>(1, columnsRaw);
            const std::int32_t rows = std::max<std::int32_t>(1, rowsRaw);

            BucketGrid result{};
            result.Columns = columns;
            result.Rows = rows;
            const std::int32_t bucketCount = UncheckedMultiply(columns, rows);
            if (bucketCount < 0)
            {
                throw System::OverflowException();
            }
            result.Buckets.resize(static_cast<std::size_t>(bucketCount));

            if (solid == nullptr)
            {
                throw System::NullReferenceException();
            }
            for (std::int32_t i = 0; i < static_cast<std::int32_t>(solid->size()); i++)
            {
                const BuiltFace* face = (*solid)[static_cast<std::size_t>(i)];
                if (face == nullptr)
                {
                    throw System::NullReferenceException();
                }
                const auto* rawPoints = face->Points();
                if (rawPoints == nullptr)
                {
                    throw System::ArgumentNullException("source");
                }
                const auto* points = reinterpret_cast<const MphRead::ManagedArray<Vector3>*>(rawPoints);
                const float minX = LinqMin(points, &Vector3::X);
                const float maxX = LinqMax(points, &Vector3::X);
                const float minZ = LinqMin(points, &Vector3::Z);
                const float maxZ = LinqMax(points, &Vector3::Z);
                const std::int32_t c0 = std::clamp(
                    UncheckedInt32(std::floor((minX - low.X) / spacing)), 0, columns - 1);
                const std::int32_t c1 = std::clamp(
                    UncheckedInt32(std::floor((maxX - low.X) / spacing)), 0, columns - 1);
                const std::int32_t r0 = std::clamp(
                    UncheckedInt32(std::floor((minZ - low.Z) / spacing)), 0, rows - 1);
                const std::int32_t r1 = std::clamp(
                    UncheckedInt32(std::floor((maxZ - low.Z) / spacing)), 0, rows - 1);
                for (std::int32_t r = r0; r <= r1; r++)
                {
                    for (std::int32_t c = c0; c <= c1; c++)
                    {
                        const std::int32_t index = UncheckedAdd(UncheckedMultiply(r, columns), c);
                        auto& bucket = BucketAt(result, index);
                        if (!bucket)
                        {
                            bucket = std::make_unique<std::vector<std::int32_t>>();
                        }
                        bucket->push_back(i);
                    }
                }
            }
            return result;
        }

        [[nodiscard]] bool Blocked(
            const std::vector<BuiltFace*>* solid,
            const std::vector<std::int32_t>& bucket,
            float x,
            float z,
            float y)
        {
            if (solid == nullptr)
            {
                throw System::NullReferenceException();
            }
            for (const std::int32_t index : bucket)
            {
                const BuiltFace* face = (*solid)[static_cast<std::size_t>(index)];
                if (Normal(face).Y > -0.3F)
                {
                    continue;
                }
                float ceiling = 0.0F;
                if (Contains(face, x, z, ceiling)
                    && ceiling > y + 0.2F
                    && ceiling < y + Headroom)
                {
                    return true;
                }
            }
            return false;
        }

        // A waypoint moved off the walls round it, to where a hunter's body
        // fits (Clearance from each): a bot heading for one it cannot reach
        // presses into the wall for ever. False where that leaves its floor.
        constexpr float Clearance = 1.0F;

        [[nodiscard]] bool Extent(const BuiltFace* face, Vector3 point, float low, float high);

        [[nodiscard]] bool Clear(
            const std::vector<BuiltFace*>* solid,
            const BucketGrid& grid,
            Vector3 min,
            float spacing,
            Vector3& at)
        {
            const float floor = at.Y - 0.5F;
            for (std::int32_t pass = 0; pass < 3; ++pass)
            {
                const std::int32_t column = static_cast<std::int32_t>(std::floor((at.X - min.X) / spacing));
                const std::int32_t row = static_cast<std::int32_t>(std::floor((at.Z - min.Z) / spacing));
                Vector3 push(0.0F, 0.0F, 0.0F);
                std::unordered_set<std::int32_t> seen{};
                for (std::int32_t dr = -1; dr <= 1; ++dr)
                {
                    for (std::int32_t dc = -1; dc <= 1; ++dc)
                    {
                        const std::int32_t c = column + dc;
                        const std::int32_t r = row + dr;
                        if (c < 0 || r < 0 || c >= grid.Columns || r >= grid.Rows)
                        {
                            continue;
                        }
                        const auto& bucketValue = BucketAt(grid, UncheckedAdd(UncheckedMultiply(r, grid.Columns), c));
                        if (!bucketValue)
                        {
                            continue;
                        }
                        for (const std::int32_t index : *bucketValue)
                        {
                            if (!seen.insert(index).second)
                            {
                                continue;
                            }
                            const BuiltFace* face = (*solid)[static_cast<std::size_t>(index)];
                            const Vector3 normal = Normal(face);
                            if (std::fabs(normal.Y) > 0.5F)
                            {
                                continue;
                            }
                            const float flat = std::sqrt(normal.X * normal.X + normal.Z * normal.Z);
                            const float distance = Vector3::Dot(normal, at) - Vector3::Dot(normal, (*Points(face))[0]);
                            if (distance < 0.0F || distance >= Clearance * flat)
                            {
                                continue;
                            }
                            const Vector3 onFace = Subtract(at, Multiply(normal, distance));
                            if (!Extent(face, onFace, floor + StepHeight, floor + Headroom))
                            {
                                continue;
                            }
                            const float gap = Clearance - distance / flat;
                            push = Add(push, Vector3(normal.X / flat * gap, 0.0F, normal.Z / flat * gap));
                        }
                    }
                }
                if (LengthSquared(push) < 0.0001F)
                {
                    break;
                }
                at = Add(at, push);
            }
            // Still on its floor, a step at most from where it was.
            const std::int32_t column = static_cast<std::int32_t>(std::floor((at.X - min.X) / spacing));
            const std::int32_t row = static_cast<std::int32_t>(std::floor((at.Z - min.Z) / spacing));
            if (column < 0 || row < 0 || column >= grid.Columns || row >= grid.Rows)
            {
                return false;
            }
            const auto& bucketValue = BucketAt(grid, UncheckedAdd(UncheckedMultiply(row, grid.Columns), column));
            if (!bucketValue)
            {
                return false;
            }
            float best = std::numeric_limits<float>::lowest();
            for (const std::int32_t index : *bucketValue)
            {
                const BuiltFace* face = (*solid)[static_cast<std::size_t>(index)];
                float y = 0.0F;
                if (Normal(face).Y >= WalkNormal && Contains(face, at.X, at.Z, y)
                    && std::fabs(y - floor) <= StepHeight && y > best)
                {
                    best = y;
                }
            }
            if (best == std::numeric_limits<float>::lowest())
            {
                return false;
            }
            at.Y = best + 0.5F;
            return true;
        }

        [[nodiscard]] std::vector<Node> Sample(
            const std::vector<BuiltFace*>* solid,
            float spacing)
        {
            Vector3 min{};
            Vector3 max{};
            Bounds(solid, min, max);
            BucketGrid grid = Buckets(solid, min, spacing);
            std::vector<Node> nodes{};
            std::vector<float> heights{};
            for (std::int32_t row = 0; row < grid.Rows; row++)
            {
                for (std::int32_t column = 0; column < grid.Columns; column++)
                {
                    const float x = min.X + (static_cast<float>(column) + 0.5F) * spacing;
                    const float z = min.Z + (static_cast<float>(row) + 0.5F) * spacing;
                    heights.clear();
                    const std::int32_t bucketIndex = UncheckedAdd(
                        UncheckedMultiply(row, grid.Columns), column);
                    const auto& bucketValue = BucketAt(grid, bucketIndex);
                    if (!bucketValue)
                    {
                        continue;
                    }
                    const std::vector<std::int32_t>* bucket = bucketValue.get();
                    for (const std::int32_t index : *bucket)
                    {
                        const BuiltFace* face = (*solid)[static_cast<std::size_t>(index)];
                        float y = 0.0F;
                        if (Normal(face).Y < WalkNormal || !Contains(face, x, z, y))
                        {
                            continue;
                        }
                        heights.push_back(y);
                    }
                    ManagedSort(heights);
                    float last = std::numeric_limits<float>::lowest();
                    for (const float y : heights)
                    {
                        if (y - last < Headroom)
                        {
                            last = y;
                            continue;
                        }
                        last = y;
                        if (Blocked(solid, *bucket, x, z, y))
                        {
                            continue;
                        }
                        Vector3 at(x, y + 0.5F, z);
                        if (!Clear(solid, grid, min, spacing, at))
                        {
                            continue;
                        }
                        Node node{};
                        node.Position = at;
                        node.Cell = UncheckedAdd(UncheckedMultiply(row, grid.Columns), column);
                        nodes.push_back(std::move(node));
                    }
                }
            }
            return nodes;
        }

        [[nodiscard]] std::vector<std::int32_t> Cells(
            Vector3 from,
            Vector3 to,
            Vector3 min,
            float spacing,
            std::int32_t columns,
            std::int32_t rows)
        {
            const Vector3 delta = Subtract(to, from);
            const std::int32_t steps = UncheckedAdd(1, UncheckedInt32(Length(delta) / spacing));
            std::vector<std::int32_t> result{};
            for (std::int32_t i = 0; i <= steps; i++)
            {
                const Vector3 point = Add(from, Multiply(delta,
                    static_cast<float>(i) / static_cast<float>(steps)));
                const std::int32_t column = UncheckedInt32(std::floor((point.X - min.X) / spacing));
                const std::int32_t row = UncheckedInt32(std::floor((point.Z - min.Z) / spacing));
                if (column >= 0 && row >= 0 && column < columns && row < rows)
                {
                    result.push_back(UncheckedAdd(UncheckedMultiply(row, columns), column));
                }
            }
            return result;
        }

        [[nodiscard]] bool Extent(
            const BuiltFace* face,
            Vector3 point,
            float low,
            float high)
        {
            float minX = std::numeric_limits<float>::max();
            float maxX = std::numeric_limits<float>::lowest();
            float minY = std::numeric_limits<float>::max();
            float maxY = std::numeric_limits<float>::lowest();
            float minZ = std::numeric_limits<float>::max();
            float maxZ = std::numeric_limits<float>::lowest();
            const auto* points = Points(face);
            for (std::size_t i = 0; i < points->Length(); i++)
            {
                const Vector3 corner = (*points)[i];
                minX = MathMin(minX, corner.X);
                maxX = MathMax(maxX, corner.X);
                minY = MathMin(minY, corner.Y);
                maxY = MathMax(maxY, corner.Y);
                minZ = MathMin(minZ, corner.Z);
                maxZ = MathMax(maxZ, corner.Z);
            }
            return point.X >= minX - 0.05F && point.X <= maxX + 0.05F
                && point.Z >= minZ - 0.05F && point.Z <= maxZ + 0.05F
                && maxY > low && minY < high;
        }

        [[nodiscard]] bool Wall(
            const std::vector<BuiltFace*>* solid,
            const BucketGrid& grid,
            Vector3 min,
            float spacing,
            Vector3 from,
            Vector3 to)
        {
            std::unordered_set<std::int32_t> seen{};
            for (const std::int32_t cell : Cells(from, to, min, spacing, grid.Columns, grid.Rows))
            {
                const auto& bucketValue = BucketAt(grid, cell);
                if (!bucketValue)
                {
                    continue;
                }
                const std::vector<std::int32_t>* bucket = bucketValue.get();
                for (const std::int32_t index : *bucket)
                {
                    if (!seen.insert(index).second)
                    {
                        continue;
                    }
                    const BuiltFace* face = (*solid)[static_cast<std::size_t>(index)];
                    const Vector3 normal = Normal(face);
                    if (std::fabs(normal.Y) > 0.5F)
                    {
                        continue;
                    }
                    const auto* points = Points(face);
                    const float d = Vector3::Dot(normal, (*points)[0]);
                    const float start = Vector3::Dot(normal, from) - d;
                    const float end = Vector3::Dot(normal, to) - d;
                    if (((start > 0.0F) == (end > 0.0F)) || std::fabs(start - end) < 0.0001F)
                    {
                        continue;
                    }
                    // A wall where it stands above the floor under the path
                    // more than a step, and below a hunter's head: the
                    // risers of a stair the path climbs are no higher than
                    // a step above it where it crosses them.
                    const Vector3 crossing = Add(from, Multiply(Subtract(to, from), start / (start - end)));
                    const float floor = crossing.Y - 0.5F;
                    if (Extent(face, crossing, floor + StepHeight, floor + Headroom))
                    {
                        return true;
                    }
                }
            }
            return false;
        }

        // Whether `point` is over the face, seen from above (its corners in
        // order round it).
        [[nodiscard]] bool Inside(const BuiltFace* face, Vector3 point)
        {
            const auto* points = Points(face);
            bool inside = false;
            const std::size_t count = points->Length();
            for (std::size_t i = 0, j = count - 1; i < count; j = i++)
            {
                const Vector3 a = (*points)[i];
                const Vector3 b = (*points)[j];
                if ((a.Z > point.Z) != (b.Z > point.Z)
                    && point.X < (b.X - a.X) * (point.Z - a.Z) / (b.Z - a.Z) + a.X)
                {
                    inside = !inside;
                }
            }
            return inside;
        }

        // A floor over `point` between `low` and `high`: what a drop to there
        // would land on first.
        [[nodiscard]] bool FloorBetween(
            const std::vector<BuiltFace*>* solid,
            const BucketGrid& grid,
            Vector3 min,
            float spacing,
            Vector3 point,
            float low,
            float high)
        {
            std::unordered_set<std::int32_t> seen{};
            for (const std::int32_t cell : Cells(point, point, min, spacing, grid.Columns, grid.Rows))
            {
                const auto& bucketValue = BucketAt(grid, cell);
                if (!bucketValue)
                {
                    continue;
                }
                for (const std::int32_t index : *bucketValue)
                {
                    if (!seen.insert(index).second)
                    {
                        continue;
                    }
                    const BuiltFace* face = (*solid)[static_cast<std::size_t>(index)];
                    const Vector3 normal = Normal(face);
                    if (std::fabs(normal.Y) < 0.5F)
                    {
                        continue;
                    }
                    // The face's height over the point, on its plane.
                    const auto* points = Points(face);
                    const float d = Vector3::Dot(normal, (*points)[0]);
                    const float y = (d - normal.X * point.X - normal.Z * point.Z) / normal.Y;
                    if (y <= low || y >= high || !Extent(face, Vector3(point.X, y, point.Z), y - 0.01F, y + 0.01F))
                    {
                        continue;
                    }
                    if (Inside(face, point))
                    {
                        return true;
                    }
                }
            }
            return false;
        }

        void Connect(
            const std::vector<BuiltFace*>* solid,
            std::vector<Node>& nodes,
            float spacing)
        {
            Vector3 min{};
            Vector3 max{};
            Bounds(solid, min, max);
            BucketGrid grid = Buckets(solid, min, spacing);
            std::unordered_map<std::int32_t, std::vector<std::int32_t>> byCell{};
            for (std::int32_t i = 0; i < static_cast<std::int32_t>(nodes.size()); i++)
            {
                byCell[nodes[static_cast<std::size_t>(i)].Cell].push_back(i);
            }
            for (std::int32_t i = 0; i < static_cast<std::int32_t>(nodes.size()); i++)
            {
                Node& node = nodes[static_cast<std::size_t>(i)];
                const std::int32_t column = node.Cell % grid.Columns;
                const std::int32_t row = node.Cell / grid.Columns;
                // The cells round it first, then the ring beyond for a drop,
                // whose foot is kept off the wall under the ledge as the
                // ledge's edge is -- drops never crowding out the ways
                // round it (MaxNeighbours, then MaxDrops more).
                for (std::int32_t ring = 1; ring <= 2; ring++)
                for (std::int32_t dr = -ring; dr <= ring; dr++)
                {
                    for (std::int32_t dc = -ring; dc <= ring; dc++)
                    {
                        if (std::max(std::abs(dr), std::abs(dc)) != ring)
                        {
                            continue;
                        }
                        const bool far = ring == 2;
                        const std::int32_t nc = column + dc;
                        const std::int32_t nr = row + dr;
                        if (nc < 0 || nr < 0 || nc >= grid.Columns || nr >= grid.Rows)
                        {
                            continue;
                        }
                        const std::int32_t key = UncheckedAdd(UncheckedMultiply(nr, grid.Columns), nc);
                        const auto found = byCell.find(key);
                        if (found == byCell.end())
                        {
                            continue;
                        }
                        for (const std::int32_t j : found->second)
                        {
                            if (j == i
                                || node.Neighbours.size() >= static_cast<std::size_t>(
                                    far ? MaxNeighbours + MaxDrops : MaxNeighbours)
                                || ContainsValue(node.Neighbours, j))
                            {
                                continue;
                            }
                            // Up a step, a stair or a slope, or down a drop --
                            // one way: the drop is met again from below, and
                            // refused.
                            const Vector3 to = nodes[static_cast<std::size_t>(j)].Position;
                            const float rise = to.Y - node.Position.Y;
                            const float across = std::sqrt(
                                (to.X - node.Position.X) * (to.X - node.Position.X)
                                + (to.Z - node.Position.Z) * (to.Z - node.Position.Z));
                            // A drop is walked at the height it leaves from,
                            // over the edge, then fallen: the side of the ledge
                            // under it is no wall.
                            const Vector3 over = rise < -StepHeight
                                ? Vector3(to.X, node.Position.Y, to.Z) : to;
                            if (rise > StepUp + across || rise < -StepDown
                                || (far && rise >= -StepHeight)
                                || Wall(solid, grid, min, spacing, node.Position, over)
                                || (rise < -StepHeight && FloorBetween(solid, grid, min, spacing, to,
                                    to.Y + 0.1F, node.Position.Y - 0.1F)))
                            {
                                continue;
                            }
                            node.Neighbours.push_back(j);
                        }
                    }
                }
            }
        }

        [[nodiscard]] std::int32_t Largest(const std::vector<Node>& nodes)
        {
            std::vector<bool> seen(nodes.size(), false);
            std::int32_t best = 0;
            std::deque<std::int32_t> queue{};
            for (std::int32_t i = 0; i < static_cast<std::int32_t>(nodes.size()); i++)
            {
                if (seen[static_cast<std::size_t>(i)])
                {
                    continue;
                }
                std::int32_t size = 0;
                seen[static_cast<std::size_t>(i)] = true;
                queue.clear();
                queue.push_back(i);
                while (!queue.empty())
                {
                    const std::int32_t current = queue.front();
                    queue.pop_front();
                    size++;
                    for (const std::int32_t neighbour : nodes[static_cast<std::size_t>(current)].Neighbours)
                    {
                        if (!seen[static_cast<std::size_t>(neighbour)])
                        {
                            seen[static_cast<std::size_t>(neighbour)] = true;
                            queue.push_back(neighbour);
                        }
                    }
                }
                best = std::max(best, size);
            }
            return best;
        }

        [[nodiscard]] std::vector<Node> Decimate(
            const std::vector<Node>& fine,
            std::int32_t radius)
        {
            radius = std::max<std::int32_t>(1, radius);
            std::vector<std::int32_t> owner(fine.size(), -1);
            std::vector<std::int32_t> seeds{};
            std::deque<std::pair<std::int32_t, std::int32_t>> queue{};
            for (std::int32_t i = 0; i < static_cast<std::int32_t>(fine.size()); i++)
            {
                if (owner[static_cast<std::size_t>(i)] != -1)
                {
                    continue;
                }
                const std::int32_t index = static_cast<std::int32_t>(seeds.size());
                seeds.push_back(i);
                owner[static_cast<std::size_t>(i)] = index;
                queue.clear();
                queue.emplace_back(i, 0);
                while (!queue.empty())
                {
                    const auto [current, depth] = queue.front();
                    queue.pop_front();
                    if (depth == radius)
                    {
                        continue;
                    }
                    for (const std::int32_t neighbour : fine[static_cast<std::size_t>(current)].Neighbours)
                    {
                        if (owner[static_cast<std::size_t>(neighbour)] == -1
                            && ContainsValue(fine[static_cast<std::size_t>(neighbour)].Neighbours, current))
                        {
                            owner[static_cast<std::size_t>(neighbour)] = index;
                            queue.emplace_back(neighbour, depth + 1);
                        }
                    }
                }
            }
            std::vector<Node> nodes{};
            nodes.reserve(seeds.size());
            for (const std::int32_t seed : seeds)
            {
                Node node{};
                node.Position = fine[static_cast<std::size_t>(seed)].Position;
                node.Cell = fine[static_cast<std::size_t>(seed)].Cell;
                nodes.push_back(std::move(node));
            }
            std::unordered_set<std::int64_t> drops{};
            for (std::int32_t i = 0; i < static_cast<std::int32_t>(fine.size()); i++)
            {
                for (const std::int32_t j : fine[static_cast<std::size_t>(i)].Neighbours)
                {
                    const std::int32_t a = owner[static_cast<std::size_t>(i)];
                    const std::int32_t b = owner[static_cast<std::size_t>(j)];
                    if (a == b || a < 0 || b < 0)
                    {
                        continue;
                    }
                    const bool both = ContainsValue(fine[static_cast<std::size_t>(j)].Neighbours, i);
                    if (both)
                    {
                        Node& nodeA = nodes[static_cast<std::size_t>(a)];
                        if (nodeA.Neighbours.size() < static_cast<std::size_t>(MaxNeighbours)
                            && !ContainsValue(nodeA.Neighbours, b))
                        {
                            nodeA.Neighbours.push_back(b);
                        }
                        continue;
                    }
                    // A drop, one way, as waypoints of its own -- the lip it
                    // is walked off and the ground it lands on -- so a bot
                    // goes to the edge and over it instead of heading
                    // straight for the place under the floor it stands on.
                    if (!drops.insert((static_cast<std::int64_t>(a) << 32) | static_cast<std::uint32_t>(b)).second)
                    {
                        continue;
                    }
                    const std::int32_t lip = static_cast<std::int32_t>(nodes.size());
                    Node top{};
                    top.Position = fine[static_cast<std::size_t>(i)].Position;
                    top.Cell = fine[static_cast<std::size_t>(i)].Cell;
                    top.Neighbours.push_back(lip + 1);
                    Node foot{};
                    foot.Position = fine[static_cast<std::size_t>(j)].Position;
                    foot.Cell = fine[static_cast<std::size_t>(j)].Cell;
                    foot.Neighbours.push_back(b);
                    nodes.push_back(std::move(top));
                    nodes.push_back(std::move(foot));
                    nodes[static_cast<std::size_t>(a)].Neighbours.push_back(lip);
                }
            }
            return nodes;
        }

        // A pad and its landing as nodes of their own, joined one way. The pad
        // node sits on the pad itself rather than on whichever waypoint the
        // thinning left nearest, because a bot heading for a waypoint a few
        // units off the pad walks past it; the landing is the waypoint nearest
        // the target that is not above it. Returns the links that found no
        // ground at one end.
        // A point raised to the higher of two heights, so a wall test between
        // them runs over the step up to a raised pad rather than into its
        // side: Wall takes anything vertical above the lower end for a wall.
        [[nodiscard]] Vector3 Level(Vector3 point, Vector3 other) noexcept
        {
            return Vector3(point.X, MathMax(point.Y, other.Y), point.Z);
        }

        std::int32_t AddLinks(
            const std::vector<BuiltFace*>* solid,
            std::vector<Node>& nodes,
            const std::vector<BotLink>* links)
        {
            if (links == nullptr || links->empty() || nodes.empty())
            {
                return 0;
            }
            Vector3 min{};
            Vector3 max{};
            Bounds(solid, min, max);
            BucketGrid grid = Buckets(solid, min, FineSpacing);
            const auto join = [&nodes](std::int32_t a, std::int32_t b)
            {
                Node& node = nodes[static_cast<std::size_t>(a)];
                if (node.Neighbours.size() < static_cast<std::size_t>(MaxNeighbours)
                    && !ContainsValue(node.Neighbours, b))
                {
                    node.Neighbours.push_back(b);
                }
            };
            std::int32_t unlinked = 0;
            // Pads and ledges added below are not places to walk onto a pad from.
            const std::int32_t walkable = static_cast<std::int32_t>(nodes.size());
            for (const BotLink& link : *links)
            {
                const Vector3 from(link.From[0], link.From[1], link.From[2]);
                const Vector3 to(link.To[0], link.To[1], link.To[2]);

                std::int32_t landing = -1;
                float landingDistance = std::numeric_limits<float>::max();
                for (std::int32_t i = 0; i < walkable; i++)
                {
                    const Vector3 position = nodes[static_cast<std::size_t>(i)].Position;
                    if (position.Y > to.Y + 1.0F)
                    {
                        continue;
                    }
                    const float distance = Length(Subtract(position, to));
                    if (distance < landingDistance)
                    {
                        landing = i;
                        landingDistance = distance;
                    }
                }

                // Waypoints a bot can walk from to the pad: in reach, on its
                // level, nothing in between. A pad in an alcove may have
                // none in reach after the thinning, so the reach widens
                // once before giving up.
                std::vector<std::int32_t> approaches{};
                for (const float reach : {Spacing * 1.5F, Spacing * 2.5F})
                {
                    for (std::int32_t i = 0; i < walkable; i++)
                    {
                        const Vector3 position = nodes[static_cast<std::size_t>(i)].Position;
                        const float rise = from.Y - position.Y;
                        const Vector3 flat(position.X - from.X, 0.0F, position.Z - from.Z);
                        if (Length(flat) <= reach
                            && rise <= StepUp && rise >= -StepDown
                            && !Wall(solid, grid, min, FineSpacing, Level(position, from), Level(from, position)))
                        {
                            approaches.push_back(i);
                        }
                    }
                    if (!approaches.empty())
                    {
                        break;
                    }
                }

                // Somewhere the thinning left no waypoint -- a ledge, a
                // platform's far end -- the landing becomes one, joined both
                // ways to whatever it can walk to.
                if (landing != -1 && landingDistance > Spacing * 2.0F)
                {
                    std::vector<std::int32_t> around{};
                    for (std::int32_t i = 0; i < walkable; i++)
                    {
                        const Vector3 position = nodes[static_cast<std::size_t>(i)].Position;
                        const float rise = position.Y - to.Y;
                        const Vector3 flat(position.X - to.X, 0.0F, position.Z - to.Z);
                        if (Length(flat) <= Spacing * 2.5F
                            && rise <= StepUp && rise >= -StepDown
                            && !Wall(solid, grid, min, FineSpacing, Level(to, position), Level(position, to)))
                        {
                            around.push_back(i);
                        }
                    }
                    if (around.empty())
                    {
                        landing = -1;
                    }
                    else
                    {
                        Node ledge{};
                        ledge.Position = to;
                        ledge.Cell = -1;
                        nodes.push_back(std::move(ledge));
                        landing = static_cast<std::int32_t>(nodes.size()) - 1;
                        landingDistance = 0.0F;
                        for (const std::int32_t other : around)
                        {
                            join(landing, other);
                            join(other, landing);
                        }
                    }
                }

                if (landing == -1 || approaches.empty())
                {
                    if (EnvironmentGetVariable("FP_NODEDEBUG").has_value())
                    {
                        std::printf("  [nodes] route (%.1f %.1f %.1f) -> (%.1f %.1f %.1f) not joined:"
                            " landing waypoint %.1f away, %d approaches\n",
                            from.X, from.Y, from.Z, to.X, to.Y, to.Z,
                            landing == -1 ? -1.0 : static_cast<double>(landingDistance),
                            static_cast<std::int32_t>(approaches.size()));
                    }
                    unlinked++;
                    continue;
                }

                Node pad{};
                pad.Position = from;
                pad.Cell = -1;
                nodes.push_back(std::move(pad));
                const std::int32_t padIndex = static_cast<std::int32_t>(nodes.size()) - 1;
                // Standing on the pad throws you, so its only way out is the
                // landing.
                for (const std::int32_t approach : approaches)
                {
                    join(approach, padIndex);
                }
                join(padIndex, landing);
            }
            return unlinked;
        }

        // Edges may be one way since a pad joined the graph, so the distance
        // to a destination is found by walking edges backwards from it.
        [[nodiscard]] std::vector<RoutesForNode> Routes(const std::vector<Node>& nodes)
        {
            const std::int32_t count = static_cast<std::int32_t>(nodes.size());
            std::vector<std::vector<std::int32_t>> incoming(nodes.size());
            for (std::int32_t i = 0; i < count; i++)
            {
                for (const std::int32_t neighbour : nodes[static_cast<std::size_t>(i)].Neighbours)
                {
                    incoming[static_cast<std::size_t>(neighbour)].push_back(i);
                }
            }
            std::vector<std::uint16_t> hop(
                static_cast<std::size_t>(count) * static_cast<std::size_t>(count), 0);
            // Where each node can get to, and the first step there.
            std::vector<std::vector<std::int32_t>> firstStep(nodes.size());
            for (std::int32_t source = 0; source < count; source++)
            {
                std::vector<std::int32_t>& first = firstStep[static_cast<std::size_t>(source)];
                first.assign(nodes.size(), -1);
                std::deque<std::int32_t> open{};
                for (const std::int32_t neighbour : nodes[static_cast<std::size_t>(source)].Neighbours)
                {
                    if (neighbour != source && first[static_cast<std::size_t>(neighbour)] < 0)
                    {
                        first[static_cast<std::size_t>(neighbour)] = neighbour;
                        open.push_back(neighbour);
                    }
                }
                while (!open.empty())
                {
                    const std::int32_t current = open.front();
                    open.pop_front();
                    for (const std::int32_t next : nodes[static_cast<std::size_t>(current)].Neighbours)
                    {
                        if (next != source && first[static_cast<std::size_t>(next)] < 0)
                        {
                            first[static_cast<std::size_t>(next)] = first[static_cast<std::size_t>(current)];
                            open.push_back(next);
                        }
                    }
                }
            }
            std::vector<std::int32_t> distance(nodes.size(), 0);
            std::deque<std::int32_t> queue{};
            for (std::int32_t destination = 0; destination < count; destination++)
            {
                std::fill(distance.begin(), distance.end(), -1);
                distance[static_cast<std::size_t>(destination)] = 0;
                queue.clear();
                queue.push_back(destination);
                while (!queue.empty())
                {
                    const std::int32_t current = queue.front();
                    queue.pop_front();
                    for (const std::int32_t neighbour : incoming[static_cast<std::size_t>(current)])
                    {
                        if (distance[static_cast<std::size_t>(neighbour)] < 0)
                        {
                            distance[static_cast<std::size_t>(neighbour)]
                                = distance[static_cast<std::size_t>(current)] + 1;
                            queue.push_back(neighbour);
                        }
                    }
                }
                for (std::int32_t source = 0; source < count; source++)
                {
                    std::uint16_t answer = static_cast<std::uint16_t>(source);
                    if (source != destination && distance[static_cast<std::size_t>(source)] > 0)
                    {
                        for (const std::int32_t neighbour : nodes[static_cast<std::size_t>(source)].Neighbours)
                        {
                            if (distance[static_cast<std::size_t>(neighbour)]
                                == distance[static_cast<std::size_t>(source)] - 1)
                            {
                                answer = static_cast<std::uint16_t>(neighbour);
                                break;
                            }
                        }
                    }
                    // Out of reach (the far side of a one-way pad or drop):
                    // towards the waypoint nearest it that can be reached,
                    // so a bot sent there goes on moving instead of standing
                    // where it is for ever.
                    if (source != destination && distance[static_cast<std::size_t>(source)] < 0)
                    {
                        const Vector3 goal = nodes[static_cast<std::size_t>(destination)].Position;
                        const std::vector<std::int32_t>& first = firstStep[static_cast<std::size_t>(source)];
                        float best = std::numeric_limits<float>::max();
                        for (std::int32_t other = 0; other < count; other++)
                        {
                            if (first[static_cast<std::size_t>(other)] < 0)
                            {
                                continue;
                            }
                            const float away = Length(Subtract(nodes[static_cast<std::size_t>(other)].Position, goal));
                            if (away < best)
                            {
                                best = away;
                                answer = static_cast<std::uint16_t>(first[static_cast<std::size_t>(other)]);
                            }
                        }
                        // Already the nearest: no nearer is to be had.
                        if (best >= Length(Subtract(nodes[static_cast<std::size_t>(source)].Position, goal)))
                        {
                            answer = static_cast<std::uint16_t>(source);
                        }
                    }
                    hop[static_cast<std::size_t>(source) * static_cast<std::size_t>(count)
                        + static_cast<std::size_t>(destination)] = answer;
                }
            }
            std::vector<RoutesForNode> results(nodes.size());
            for (std::int32_t source = 0; source < count; source++)
            {
                RoutesForNode runs{};
                std::int32_t start = 0;
                while (start < count)
                {
                    const std::uint16_t value = hop[
                        static_cast<std::size_t>(source) * static_cast<std::size_t>(count)
                        + static_cast<std::size_t>(start)];
                    std::int32_t end = start + 1;
                    while (end < count
                        && hop[static_cast<std::size_t>(source) * static_cast<std::size_t>(count)
                            + static_cast<std::size_t>(end)] == value
                        && end - start < std::numeric_limits<std::uint16_t>::max())
                    {
                        end++;
                    }
                    runs.emplace_back(static_cast<std::uint16_t>(end - start), value);
                    start = end;
                }
                results[static_cast<std::size_t>(source)] = std::move(runs);
            }
            return results;
        }

        void WriteU16(std::vector<std::uint8_t>& bytes, std::uint16_t value)
        {
            bytes.push_back(static_cast<std::uint8_t>(value & 0xFFU));
            bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFU));
        }

        void WriteU32(std::vector<std::uint8_t>& bytes, std::uint32_t value)
        {
            bytes.push_back(static_cast<std::uint8_t>(value & 0xFFU));
            bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFU));
            bytes.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFU));
            bytes.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFFU));
        }

        void WriteI32(std::vector<std::uint8_t>& bytes, std::int32_t value)
        {
            WriteU32(bytes, std::bit_cast<std::uint32_t>(value));
        }

        [[nodiscard]] std::vector<std::uint8_t> Write(
            const std::vector<Node>& nodes,
            float spacing)
        {
            static_cast<void>(spacing);
            const std::vector<RoutesForNode> routes = Routes(nodes);
            constexpr std::int32_t indexOffset = 14;
            constexpr std::int32_t dataOffset = 16;
            constexpr std::int32_t listOffset = 24;
            constexpr std::int32_t nodeOffset = 32;
            const std::int32_t valuesOffset = UncheckedAdd(
                nodeOffset,
                UncheckedMultiply(static_cast<std::int32_t>(nodes.size()), 36));
            std::vector<std::int32_t> routeOffset(nodes.size(), 0);
            std::int32_t cursor = valuesOffset;
            for (std::int32_t i = 0; i < static_cast<std::int32_t>(nodes.size()); i++)
            {
                routeOffset[static_cast<std::size_t>(i)] = cursor;
                cursor = UncheckedAdd(cursor,
                    UncheckedMultiply(static_cast<std::int32_t>(routes[static_cast<std::size_t>(i)].size()), 4));
            }
            std::vector<std::uint8_t> bytes{};
            WriteU16(bytes, 6);
            WriteU16(bytes, 1);
            WriteU32(bytes, static_cast<std::uint32_t>(indexOffset));
            WriteU32(bytes, static_cast<std::uint32_t>(dataOffset));
            WriteU16(bytes, 0);
            WriteU16(bytes, 0);
            WriteU32(bytes, static_cast<std::uint32_t>(listOffset));
            WriteU16(bytes, 1);
            WriteU16(bytes, 0x5C);
            WriteU32(bytes, static_cast<std::uint32_t>(nodeOffset));
            WriteU16(bytes, static_cast<std::uint16_t>(nodes.size()));
            WriteU16(bytes, 0x5C);
            for (std::int32_t i = 0; i < static_cast<std::int32_t>(nodes.size()); i++)
            {
                const Node& node = nodes[static_cast<std::size_t>(i)];
                WriteU16(bytes, static_cast<std::uint16_t>(MphRead::Formats::NodeType::Navigation));
                WriteU16(bytes, static_cast<std::uint16_t>(i));
                WriteU16(bytes, 0);
                WriteU16(bytes, 0);
                WriteI32(bytes, MphRead::Fixed::ToInt(node.Position.X));
                WriteI32(bytes, MphRead::Fixed::ToInt(node.Position.Y));
                WriteI32(bytes, MphRead::Fixed::ToInt(node.Position.Z));
                WriteI32(bytes, MphRead::Fixed::ToInt(Reach));
                WriteU32(bytes, static_cast<std::uint32_t>(routeOffset[static_cast<std::size_t>(i)]));
                WriteU32(bytes, static_cast<std::uint32_t>(routeOffset[static_cast<std::size_t>(i)]));
                WriteU32(bytes, 0);
            }
            for (std::int32_t i = 0; i < static_cast<std::int32_t>(nodes.size()); i++)
            {
                for (const auto& [run, hop] : routes[static_cast<std::size_t>(i)])
                {
                    WriteU16(bytes, run);
                    WriteU16(bytes, hop);
                }
            }
            return bytes;
        }

        [[nodiscard]] std::int32_t DegreeSum(const std::vector<Node>& nodes)
        {
            std::int32_t total = 0;
            for (const Node& node : nodes)
            {
                const std::int32_t value = static_cast<std::int32_t>(node.Neighbours.size());
                if (value > 0 && total > std::numeric_limits<std::int32_t>::max() - value)
                {
                    throw System::OverflowException();
                }
                total += value;
            }
            return total;
        }

        [[nodiscard]] std::int32_t MaxDegree(const std::vector<Node>& nodes)
        {
            if (nodes.empty())
            {
                throw System::InvalidOperationException("Sequence contains no elements");
            }
            std::int32_t result = static_cast<std::int32_t>(nodes.front().Neighbours.size());
            for (std::size_t i = 1; i < nodes.size(); i++)
            {
                result = std::max(result, static_cast<std::int32_t>(nodes[i].Neighbours.size()));
            }
            return result;
        }

        [[nodiscard]] std::int32_t IsolatedCount(const std::vector<Node>& nodes) noexcept
        {
            std::int32_t count = 0;
            for (const Node& node : nodes)
            {
                if (node.Neighbours.empty())
                {
                    count++;
                }
            }
            return count;
        }
    }

    std::tuple<std::vector<std::uint8_t>, std::int32_t, std::int32_t> MapNodePacker::Pack(
        const std::vector<BuiltFace*>* solid,
        const std::vector<BotLink>* links)
    {
        std::vector<Node> fine = Sample(solid, FineSpacing);
        Connect(solid, fine, FineSpacing);
        if (EnvironmentGetVariable("FP_NODEDEBUG").has_value())
        {
            const std::int32_t edges = DegreeSum(fine) / 2;
            std::printf("  [nodes] fine %d nodes, %d edges, largest component %d\n",
                static_cast<std::int32_t>(fine.size()), edges, Largest(fine));
            // Each fine node and its component, for looking at where the
            // graph breaks (FP_NODEDEBUG names the file).
            const std::string path = *EnvironmentGetVariable("FP_NODEDEBUG");
            if (path.size() > 1)
            {
                std::vector<std::int32_t> component(fine.size(), -1);
                std::int32_t next = 0;
                for (std::size_t i = 0; i < fine.size(); ++i)
                {
                    if (component[i] != -1)
                    {
                        continue;
                    }
                    std::deque<std::size_t> queue{i};
                    component[i] = next;
                    while (!queue.empty())
                    {
                        const std::size_t current = queue.front();
                        queue.pop_front();
                        for (const std::int32_t neighbour : fine[current].Neighbours)
                        {
                            if (component[static_cast<std::size_t>(neighbour)] == -1)
                            {
                                component[static_cast<std::size_t>(neighbour)] = next;
                                queue.push_back(static_cast<std::size_t>(neighbour));
                            }
                        }
                    }
                    ++next;
                }
                if (FILE* file = std::fopen(path.c_str(), "w"))
                {
                    for (std::size_t i = 0; i < fine.size(); ++i)
                    {
                        std::fprintf(file, "%.3f,%.3f,%.3f,%d\n", fine[i].Position.X, fine[i].Position.Y,
                            fine[i].Position.Z, component[i]);
                    }
                    std::fclose(file);
                }
                // And its ways out, a line a node: "from to to ...".
                if (FILE* file = std::fopen((path + ".edges").c_str(), "w"))
                {
                    for (std::size_t i = 0; i < fine.size(); ++i)
                    {
                        std::fprintf(file, "%zu", i);
                        for (const std::int32_t j : fine[i].Neighbours)
                        {
                            std::fprintf(file, " %d", j);
                        }
                        std::fprintf(file, "\n");
                    }
                    std::fclose(file);
                }
            }
        }
        float spacing = Spacing;
        std::vector<Node> nodes{};
        while (true)
        {
            nodes = Decimate(fine, UncheckedInt32(RoundToEven(spacing / FineSpacing)));
            if (nodes.size() <= static_cast<std::size_t>(MaxNodes) || spacing > 64.0F)
            {
                break;
            }
            spacing *= 1.4F;
        }
        const std::int32_t unlinked = AddLinks(solid, nodes, links);
        // A waypoint no way leads to or from is no place to head for: a bot
        // nearest one would stand at it for ever.
        {
            std::vector<bool> used(nodes.size(), false);
            for (std::size_t i = 0; i < nodes.size(); ++i)
            {
                if (!nodes[i].Neighbours.empty())
                {
                    used[i] = true;
                    for (const std::int32_t j : nodes[i].Neighbours)
                    {
                        used[static_cast<std::size_t>(j)] = true;
                    }
                }
            }
            std::vector<std::int32_t> index(nodes.size(), -1);
            std::vector<Node> kept{};
            for (std::size_t i = 0; i < nodes.size(); ++i)
            {
                if (used[i])
                {
                    index[i] = static_cast<std::int32_t>(kept.size());
                    kept.push_back(nodes[i]);
                }
            }
            for (Node& node : kept)
            {
                for (std::int32_t& j : node.Neighbours)
                {
                    j = index[static_cast<std::size_t>(j)];
                }
            }
            if (!kept.empty())
            {
                nodes = std::move(kept);
            }
        }
        if (links != nullptr && !links->empty())
        {
            std::printf("  [nodes] %d of %d jump pad/teleporter routes joined to the bots' graph\n",
                static_cast<std::int32_t>(links->size()) - unlinked,
                static_cast<std::int32_t>(links->size()));
        }
        const std::int32_t edges = DegreeSum(nodes) / 2;
        if (EnvironmentGetVariable("FP_NODEDEBUG").has_value())
        {
            const std::int32_t largest = Largest(nodes);
            const std::int32_t maxDegree = MaxDegree(nodes);
            const std::int32_t isolated = IsolatedCount(nodes);
            std::printf(
                "  [nodes] coarse %d nodes, %d edges, largest component %d, degree max %d, isolated %d\n",
                static_cast<std::int32_t>(nodes.size()),
                edges,
                largest,
                maxDegree,
                isolated);
        }
        std::vector<std::uint8_t> bytes = Write(nodes, spacing);
        return std::make_tuple(
            std::move(bytes),
            static_cast<std::int32_t>(nodes.size()),
            edges);
    }
}
