#include "MapCollisionPacker.hpp"

#include "../../Formats/Collision.hpp"
#include "../../Program.hpp"
#include "../../Utility/Repack.hpp"
#include "../../Utility/RepackCollision.hpp"
#include "../../NativeRuntime/System/Managed.hpp"
#include "../../Formats/Types.hpp"
#include "NativeRuntime/System/Globalization.hpp"
#include "NativeRuntime/System/Console.hpp"

#include <algorithm>
#include <map>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using ::MphRead::NativeRuntime::ConvertToInt32Net9;
using ::MphRead::NativeRuntime::UncheckedAdd;
using ::MphRead::NativeRuntime::UncheckedMultiply;
using ::OpenTK::Mathematics::ComponentMax;
using ::OpenTK::Mathematics::ComponentMin;

namespace
{
    using MphRead::Utility::CollisionDataEditor;
    using OpenTK::Mathematics::Vector3;
    using OpenTK::Mathematics::Vector4;

    [[nodiscard]] constexpr std::int32_t ToInt32Unchecked(
        std::size_t value) noexcept
    {
        return std::bit_cast<std::int32_t>(
            static_cast<std::uint32_t>(value));
    }

    [[nodiscard]] std::int32_t ListCount(std::size_t count)
    {
        if (count > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
        {
            throw std::length_error("List count exceeds Int32.MaxValue.");
        }
        return static_cast<std::int32_t>(count);
    }

    struct Vector3Equal final
    {
        [[nodiscard]] bool operator()(Vector3 left, Vector3 right) const noexcept
        {
            return left.X == right.X
                && left.Y == right.Y
                && left.Z == right.Z;
        }
    };

    struct Vector4Equal final
    {
        [[nodiscard]] bool operator()(Vector4 left, Vector4 right) const noexcept
        {
            return left.X == right.X
                && left.Y == right.Y
                && left.Z == right.Z
                && left.W == right.W;
        }
    };

    [[nodiscard]] std::size_t HashFloat(float value) noexcept
    {
        return std::hash<float>{}(value);
    }

    [[nodiscard]] std::size_t CombineHash(std::size_t hash, std::size_t value) noexcept
    {
        return hash ^ (value + static_cast<std::size_t>(0x9e3779b9U)
            + (hash << 6U) + (hash >> 2U));
    }

    struct Vector3Hash final
    {
        [[nodiscard]] std::size_t operator()(Vector3 value) const noexcept
        {
            std::size_t hash = HashFloat(value.X);
            hash = CombineHash(hash, HashFloat(value.Y));
            return CombineHash(hash, HashFloat(value.Z));
        }
    };

    struct Vector4Hash final
    {
        [[nodiscard]] std::size_t operator()(Vector4 value) const noexcept
        {
            std::size_t hash = HashFloat(value.X);
            hash = CombineHash(hash, HashFloat(value.Y));
            hash = CombineHash(hash, HashFloat(value.Z));
            return CombineHash(hash, HashFloat(value.W));
        }
    };

    template <typename TKey, typename THash, typename TEqual>
    class ManagedDictionary final
    {
    public:
        [[nodiscard]] bool TryGetValue(const TKey& key, std::uint16_t& value) const
        {
            const auto bucket = _buckets.find(THash{}(key));
            if (bucket != _buckets.end())
            {
                for (const auto& entry : bucket->second)
                {
                    if (TEqual{}(entry.first, key))
                    {
                        value = entry.second;
                        return true;
                    }
                }
            }
            value = 0;
            return false;
        }

        void Add(const TKey& key, std::uint16_t value)
        {
            auto& bucket = _buckets[THash{}(key)];
            for (const auto& entry : bucket)
            {
                if (TEqual{}(entry.first, key))
                {
                    throw std::invalid_argument(
                        "An item with the same key has already been added.");
                }
            }
            bucket.emplace_back(key, value);
        }

    private:
        std::unordered_map<std::size_t,
            std::vector<std::pair<TKey, std::uint16_t>>> _buckets{};
    };

    struct Face final
    {
        std::uint16_t PlaneIndex = 0;
        std::shared_ptr<CollisionDataEditor> Editor;
        std::uint16_t Count = 0;
        std::uint16_t Start = 0;
    };

    struct Entry final
    {
        std::uint16_t Count = 0;
        std::uint32_t Start = 0;
    };

    [[nodiscard]] std::shared_ptr<CollisionDataEditor> RequireEditor(
        const std::shared_ptr<CollisionDataEditor>& editor)
    {
        if (!editor)
        {
            throw System::NullReferenceException();
        }
        return editor;
    }
    using FaceList = std::vector<std::shared_ptr<CollisionDataEditor>>;

    struct CellKey final
    {
        std::int64_t X = 0;
        std::int64_t Y = 0;
        std::int64_t Z = 0;

        [[nodiscard]] bool operator==(const CellKey& other) const noexcept
        {
            return X == other.X && Y == other.Y && Z == other.Z;
        }
    };

    struct CellKeyHash final
    {
        [[nodiscard]] std::size_t operator()(const CellKey& key) const noexcept
        {
            std::size_t hash = std::hash<std::int64_t>{}(key.X);
            hash = CombineHash(hash, std::hash<std::int64_t>{}(key.Y));
            return CombineHash(hash, std::hash<std::int64_t>{}(key.Z));
        }
    };

    [[nodiscard]] std::size_t CountDistinctPoints(const FaceList& faces)
    {
        std::unordered_set<Vector3, Vector3Hash, Vector3Equal> seen;
        for (const std::shared_ptr<CollisionDataEditor>& face : faces)
        {
            for (const Vector3 point : *RequireEditor(face)->Points)
            {
                seen.insert(point);
            }
        }
        return seen.size();
    }

    // The faces with every point moved onto the first point seen within the
    // same cell of a grid `cell` units wide, a face left with fewer than three
    // different corners dropped. A level that has more corners than the format
    // can index is brought down to a number it can, by the amount that costs
    // least: two corners a fraction of a unit apart are one corner as far as
    // anybody can walk into or shoot at the difference.
    [[nodiscard]] std::shared_ptr<FaceList> Weld(const FaceList& faces, float cell, std::size_t& dropped)
    {
        auto result = std::make_shared<FaceList>();
        result->reserve(faces.size());
        // Corners already chosen, by the grid cell they are in. A corner is the
        // nearest chosen one within `cell` of it, looked for in the 27 cells
        // around, so two copies of a point that fall either side of a cell
        // edge are still found to be one point.
        std::unordered_map<CellKey, std::vector<Vector3>, CellKeyHash> representatives;
        const auto cellOf = [&](float value) { return static_cast<std::int64_t>(std::floor(value / cell)); };
        dropped = 0;
        for (const std::shared_ptr<CollisionDataEditor>& face : faces)
        {
            const std::shared_ptr<CollisionDataEditor> source = RequireEditor(face);
            std::vector<Vector3> moved;
            moved.reserve(source->Points->size());
            for (const Vector3 point : *source->Points)
            {
                const std::int64_t cx = cellOf(point.X);
                const std::int64_t cy = cellOf(point.Y);
                const std::int64_t cz = cellOf(point.Z);
                bool found = false;
                Vector3 corner = point;
                float nearest = cell * cell;
                for (std::int64_t dx = -1; dx <= 1; ++dx)
                {
                    for (std::int64_t dy = -1; dy <= 1; ++dy)
                    {
                        for (std::int64_t dz = -1; dz <= 1; ++dz)
                        {
                            const auto at = representatives.find(CellKey{cx + dx, cy + dy, cz + dz});
                            if (at == representatives.end())
                            {
                                continue;
                            }
                            for (const Vector3 candidate : at->second)
                            {
                                const float ex = candidate.X - point.X;
                                const float ey = candidate.Y - point.Y;
                                const float ez = candidate.Z - point.Z;
                                const float distance = ex * ex + ey * ey + ez * ez;
                                if (distance <= nearest)
                                {
                                    nearest = distance;
                                    corner = candidate;
                                    found = true;
                                }
                            }
                        }
                    }
                }
                if (!found)
                {
                    representatives[CellKey{cx, cy, cz}].push_back(point);
                }
                if (moved.empty() || !Vector3Equal{}(moved.back(), corner))
                {
                    moved.push_back(corner);
                }
            }
            while (moved.size() > 1 && Vector3Equal{}(moved.front(), moved.back()))
            {
                moved.pop_back();
            }
            if (moved.size() < 3)
            {
                ++dropped;
                continue;
            }
            auto welded = std::make_shared<CollisionDataEditor>();
            *welded->Points = std::move(moved);
            welded->Plane = source->Plane;
            welded->LayerMask = source->LayerMask;
            welded->Flags = source->Flags;
            result->push_back(std::move(welded));
        }
        return result;
    }

    // The faces as they are if their corners fit the format's 16-bit index,
    // else welded at the smallest cell that makes them fit, or nothing if no
    // cell small enough to be harmless does.
    [[nodiscard]] std::shared_ptr<FaceList> FitCorners(const FaceList& faces)
    {
        constexpr std::size_t Limit = 65535;
        constexpr float Smallest = 0.005F;
        constexpr float Largest = 0.4F;
        const std::size_t before = CountDistinctPoints(faces);
        if (before <= Limit)
        {
            return nullptr;
        }
        for (float cell = Smallest; cell <= Largest; cell *= 1.5F)
        {
            std::size_t dropped = 0;
            std::shared_ptr<FaceList> welded = Weld(faces, cell, dropped);
            if (welded->empty())
            {
                break;
            }
            const std::size_t after = CountDistinctPoints(*welded);
            if (after <= Limit)
            {
                ::MphRead::NativeRuntime::ConsoleWriteLine("[mapgen] the collision has " + std::to_string(before)
                    + " distinct corners and the format indexes " + std::to_string(Limit) + ": welded corners closer than "
                    + std::to_string(cell) + " units, " + std::to_string(after) + " left, "
                    + std::to_string(dropped) + " of " + std::to_string(faces.size()) + " faces collapsed");
                return welded;
            }
        }
        return nullptr;
    }


}

namespace MphRead::Mods::MapGen
{
    std::vector<std::uint8_t> MapCollisionPacker::Pack(
        const std::shared_ptr<const std::vector<
            std::shared_ptr<MphRead::Utility::CollisionDataEditor>>>& requested)
    {
        using MphRead::Formats::Collision::CollisionHeader;
        using MphRead::Utility::BinaryWriter;

        if (!requested)
        {
            throw System::NullReferenceException();
        }
        if (requested->empty())
        {
            throw MphRead::ProgramException("A map needs at least one solid face.");
        }

        // A level with more corners than the format can index is welded down to
        // what it can, rather than refused outright (see FitCorners).
        const std::shared_ptr<FaceList> fitted = FitCorners(*requested);
        const std::shared_ptr<const FaceList> input = fitted ? fitted : requested;

        // The faces in the order of the grid's cells they start in, so the
        // cells along a row list runs of the same faces in the same order,
        // which the cells then share (below).
        Vector3 lowest(
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max());
        for (const std::shared_ptr<CollisionDataEditor>& editorValue : *input)
        {
            for (const Vector3 point : *RequireEditor(editorValue)->Points)
            {
                lowest = ComponentMin(lowest, point);
            }
        }
        std::vector<std::pair<std::array<std::int32_t, 6>, std::shared_ptr<CollisionDataEditor>>> keyed;
        keyed.reserve(input->size());
        for (const std::shared_ptr<CollisionDataEditor>& editorValue : *input)
        {
            const std::shared_ptr<CollisionDataEditor> editor = RequireEditor(editorValue);
            Vector3 low(
                std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max());
            Vector3 high(
                std::numeric_limits<float>::lowest(),
                std::numeric_limits<float>::lowest(),
                std::numeric_limits<float>::lowest());
            for (const Vector3 point : *editor->Points)
            {
                low = ComponentMin(low, point);
                high = ComponentMax(high, point);
            }
            const auto cell = [](float value, float origin)
            {
                return ConvertToInt32Net9(std::floor((value - origin) / CellSize));
            };
            keyed.emplace_back(std::array<std::int32_t, 6>{
                cell(low.Y, lowest.Y), cell(low.Z, lowest.Z), cell(low.X, lowest.X),
                cell(high.Y, lowest.Y), cell(high.Z, lowest.Z), cell(high.X, lowest.X)}, editor);
        }
        std::stable_sort(keyed.begin(), keyed.end(),
            [](const auto& left, const auto& right) { return left.first < right.first; });
        auto ordered = std::make_shared<std::vector<std::shared_ptr<CollisionDataEditor>>>();
        ordered->reserve(keyed.size());
        for (auto& [key, editor] : keyed)
        {
            ordered->push_back(std::move(editor));
        }
        const std::shared_ptr<std::vector<std::shared_ptr<CollisionDataEditor>>>& data = ordered;

        std::vector<Vector3> points;
        ManagedDictionary<Vector3, Vector3Hash, Vector3Equal> pointIds;
        std::vector<Vector4> planes;
        ManagedDictionary<Vector4, Vector4Hash, Vector4Equal> planeIds;
        std::vector<std::uint16_t> pointIndices;
        std::vector<Face> faces;
        Vector3 min(
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max());
        Vector3 max(
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest());

        for (const std::shared_ptr<CollisionDataEditor>& editorValue : *data)
        {
            const std::shared_ptr<CollisionDataEditor> editor = RequireEditor(editorValue);
            const std::int32_t editorPointCount = ListCount(editor->Points->size());
            if (editorPointCount < 3 || editorPointCount > 10)
            {
                throw MphRead::ProgramException(
                    "A collision face has " + ::MphRead::NativeRuntime::ToString(editorPointCount)
                    + " points; the format allows 3 to 10.");
            }

            std::uint16_t planeIndex = 0;
            if (!planeIds.TryGetValue(editor->Plane, planeIndex))
            {
                planeIndex = static_cast<std::uint16_t>(planes.size());
                planes.push_back(editor->Plane);
                planeIds.Add(editor->Plane, planeIndex);
            }

            const std::int32_t start = ListCount(pointIndices.size());
            for (const Vector3 point : *editor->Points)
            {
                std::uint16_t pointIndex = 0;
                if (!pointIds.TryGetValue(point, pointIndex))
                {
                    pointIndex = static_cast<std::uint16_t>(points.size());
                    if (points.size() > std::numeric_limits<std::uint16_t>::max())
                    {
                        throw MphRead::ProgramException(
                            "The map has more than 65535 distinct collision points, which the format cannot index. "
                            "Convert at a larger scale or with less geometry.");
                    }
                    points.push_back(point);
                    pointIds.Add(point, pointIndex);
                    min = ComponentMin(min, point);
                    max = ComponentMax(max, point);
                }
                pointIndices.push_back(pointIndex);
            }
            pointIndices.push_back(pointIndices.at(static_cast<std::size_t>(start)));
            faces.push_back(Face{
                planeIndex,
                editor,
                static_cast<std::uint16_t>(editor->Points->size()),
                static_cast<std::uint16_t>(start)
            });
        }

        const auto partCount = [](float maximum, float minimum)
        {
            const float quotient = (maximum - minimum) / CellSize;
            const std::int32_t floored = ConvertToInt32Net9(std::floor(quotient));
            return std::max<std::int32_t>(1, UncheckedAdd(floored, 1));
        };
        const std::int32_t partsX = partCount(max.X, min.X);
        const std::int32_t partsY = partCount(max.Y, min.Y);
        const std::int32_t partsZ = partCount(max.Z, min.Z);
        const std::int32_t cellCount = UncheckedMultiply(UncheckedMultiply(partsX, partsY), partsZ);
        if (cellCount < 0)
        {
            throw System::OverflowException();
        }
        std::vector<std::optional<std::vector<std::uint16_t>>> cells(
            static_cast<std::size_t>(cellCount));

        const std::int32_t dataCount = ListCount(data->size());
        for (std::int32_t i = 0; i < dataCount; i++)
        {
            const std::shared_ptr<CollisionDataEditor> editor = RequireEditor(
                data->at(static_cast<std::size_t>(i)));
            Vector3 faceMin(
                std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max());
            Vector3 faceMax(
                std::numeric_limits<float>::lowest(),
                std::numeric_limits<float>::lowest(),
                std::numeric_limits<float>::lowest());
            for (const Vector3 point : *editor->Points)
            {
                faceMin = ComponentMin(faceMin, point);
                faceMax = ComponentMax(faceMax, point);
            }

            const std::int32_t x0 = CellIndex(faceMin.X, min.X, partsX);
            const std::int32_t x1 = CellIndex(faceMax.X, min.X, partsX);
            const std::int32_t y0 = CellIndex(faceMin.Y, min.Y, partsY);
            const std::int32_t y1 = CellIndex(faceMax.Y, min.Y, partsY);
            const std::int32_t z0 = CellIndex(faceMin.Z, min.Z, partsZ);
            const std::int32_t z1 = CellIndex(faceMax.Z, min.Z, partsZ);
            for (std::int32_t y = y0; y <= y1; y = UncheckedAdd(y, 1))
            {
                for (std::int32_t z = z0; z <= z1; z = UncheckedAdd(z, 1))
                {
                    for (std::int32_t x = x0; x <= x1; x = UncheckedAdd(x, 1))
                    {
                        const std::int32_t index = UncheckedAdd(
                            UncheckedAdd(
                                UncheckedMultiply(UncheckedMultiply(y, partsX), partsZ),
                                UncheckedMultiply(z, partsX)),
                            x);
                        if (index < 0 || index >= cellCount)
                        {
                            throw System::IndexOutOfRangeException();
                        }
                        auto& cell = cells.at(static_cast<std::size_t>(index));
                        if (!cell)
                        {
                            cell.emplace();
                        }
                        cell->push_back(static_cast<std::uint16_t>(i));
                    }
                }
            }
        }

        std::int32_t references = 0;
        for (const std::optional<std::vector<std::uint16_t>>& cell : cells)
        {
            references = UncheckedAdd(
                references,
                cell ? ListCount(cell->size()) : 0);
        }

        // A cell names where its faces start in one list of face indices,
        // and how many there are, both in 16 bits -- so cells holding the
        // same faces, as the cells a wide floor crosses do, share one run of
        // the list. A large level fits where a list per cell would not.
        std::vector<std::uint16_t> dataIndices;
        std::vector<Entry> entries;
        std::map<std::vector<std::uint16_t>, std::int32_t> runs;
        for (const std::optional<std::vector<std::uint16_t>>& cell : cells)
        {
            if (!cell || cell->empty())
            {
                entries.push_back(Entry{0, 0});
                continue;
            }
            const auto found = runs.find(*cell);
            std::int32_t start = 0;
            // Or anywhere in the list already: a cell holding a few of a
            // wide run's faces, in the same order, is a slice of it.
            const auto within = found != runs.end() ? dataIndices.end()
                : std::search(dataIndices.begin(), dataIndices.end(), cell->begin(), cell->end());
            if (found != runs.end())
            {
                start = found->second;
            }
            else if (within != dataIndices.end())
            {
                start = ListCount(static_cast<std::size_t>(within - dataIndices.begin()));
                runs.emplace(*cell, start);
            }
            else
            {
                // Neighbouring cells list their faces in the same order, so a
                // new run often begins with the end of the last one: it
                // starts inside it.
                std::size_t overlap = std::min(cell->size(), dataIndices.size());
                while (overlap > 0
                    && !std::equal(cell->begin(), cell->begin() + static_cast<std::ptrdiff_t>(overlap),
                        dataIndices.end() - static_cast<std::ptrdiff_t>(overlap)))
                {
                    --overlap;
                }
                start = ListCount(dataIndices.size() - overlap);
                dataIndices.insert(dataIndices.end(), cell->begin() + static_cast<std::ptrdiff_t>(overlap), cell->end());
                runs.emplace(*cell, start);
            }
            entries.push_back(Entry{
                static_cast<std::uint16_t>(cell->size()),
                static_cast<std::uint32_t>(start)
            });
        }

        static_assert(sizeof(CollisionHeader) == 84);
        BinaryWriter writer;
        writer.Position(sizeof(CollisionHeader));

        const std::int32_t pointOffset = ToInt32Unchecked(writer.Position());
        for (const Vector3 point : points)
        {
            writer.WriteVector3(point);
        }
        const std::int32_t planeOffset = ToInt32Unchecked(writer.Position());
        for (const Vector4 plane : planes)
        {
            writer.WriteVector4(plane);
        }
        const std::int32_t pointIndexOffset = ToInt32Unchecked(writer.Position());
        for (const std::uint16_t index : pointIndices)
        {
            writer.Write(index);
        }
        Align(writer);

        const std::int32_t dataOffset = ToInt32Unchecked(writer.Position());
        for (const Face& face : faces)
        {
            writer.Write(static_cast<std::uint32_t>(0));
            writer.Write(face.PlaneIndex);
            writer.Write(static_cast<std::uint16_t>(face.Editor->Flags));
            writer.Write(face.Editor->LayerMask);
            writer.Write(static_cast<std::uint16_t>(0));
            writer.Write(face.Count);
            writer.Write(face.Start);
        }

        const std::int32_t dataIndexOffset = ToInt32Unchecked(writer.Position());
        for (const std::uint16_t index : dataIndices)
        {
            writer.Write(index);
        }
        Align(writer);

        // A run starting past 16 bits -- a level as large as q3ctf2 -- needs
        // the wide cells of wc32, which only this game reads.
        const bool wide = std::any_of(entries.begin(), entries.end(),
            [](const Entry& entry) { return entry.Start > std::numeric_limits<std::uint16_t>::max(); });
        const std::int32_t entryOffset = ToInt32Unchecked(writer.Position());
        for (const Entry& entry : entries)
        {
            if (wide)
            {
                writer.Write(static_cast<std::uint32_t>(entry.Count));
                writer.Write(entry.Start);
            }
            else
            {
                writer.Write(entry.Count);
                writer.Write(static_cast<std::uint16_t>(entry.Start));
            }
        }
        const std::int32_t portalOffset = ToInt32Unchecked(writer.Position());

        writer.Position(0);
        writer.WriteString(wide ? "wc32" : "wc01", 4);
        writer.Write(ListCount(points.size()));
        writer.Write(pointOffset);
        writer.Write(ListCount(planes.size()));
        writer.Write(planeOffset);
        writer.Write(ListCount(pointIndices.size()));
        writer.Write(pointIndexOffset);
        writer.Write(ListCount(faces.size()));
        writer.Write(dataOffset);
        writer.Write(ListCount(dataIndices.size()));
        writer.Write(dataIndexOffset);
        writer.Write(partsX);
        writer.Write(partsY);
        writer.Write(partsZ);
        writer.WriteVector3(min);
        writer.Write(ListCount(entries.size()));
        writer.Write(entryOffset);
        writer.Write(static_cast<std::int32_t>(0));
        writer.Write(portalOffset);
        return writer.ToArray();
    }

    std::int32_t MapCollisionPacker::CellIndex(
        float value, float origin, std::int32_t parts) noexcept
    {
        const std::int32_t index = ConvertToInt32Net9((value - origin) / CellSize);
        return std::clamp(index, 0, parts - 1);
    }

    void MapCollisionPacker::Align(MphRead::Utility::BinaryWriter& writer)
    {
        while (writer.Position() % 4U != 0U)
        {
            writer.Write(static_cast<std::uint8_t>(0));
        }
    }
}
