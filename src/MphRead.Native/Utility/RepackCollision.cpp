#include "RepackCollision.hpp"

#include "../Metadata/Metadata.hpp"
#include "../Metadata/Rooms.hpp"
#include "../Program.hpp"
#include "../Read.hpp"
#include "../Scene.hpp"
#include "../SceneSetup.hpp"
#include "../Formats/Types.hpp"
#include "../NativeRuntime/System/IO.hpp"
#include "../NativeRuntime/System/Managed.hpp"
#include "../NativeRuntime/OpenTK/Mathematics.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

#if defined(DEBUG)
#define REPACK_COLLISION_DEBUG_ASSERT(condition) do { if (!(condition)) { std::abort(); } } while (false)
#else
#define REPACK_COLLISION_DEBUG_ASSERT(condition) do { } while (false)
#endif

using ::MphRead::NativeRuntime::FileWriteAllBytes;
using ::MphRead::NativeRuntime::ManagedListAt;
using ::MphRead::NativeRuntime::MathMax;
using ::MphRead::NativeRuntime::MathMin;
using ::MphRead::NativeRuntime::RequireReference;
using ::MphRead::NativeRuntime::UncheckedAdd;
using ::MphRead::NativeRuntime::UncheckedMultiply;
using ::OpenTK::Mathematics::Divide;
using ::OpenTK::Mathematics::Length;

namespace
{
    using MphRead::Fixed;
    using MphRead::ProgramException;
    using MphRead::RoomMetadata;
    using MphRead::ModelMetadata;
    using MphRead::Utility::BinaryWriter;
    using MphRead::Utility::CollisionDataEditor;
    using MphRead::Formats::Collision::Collision;
    using MphRead::Formats::Collision::CollisionData;
    using MphRead::Formats::Collision::CollisionEntry;
    using MphRead::Formats::Collision::CollisionFlags;
    using MphRead::Formats::Collision::CollisionHeader;
    using MphRead::Formats::Collision::CollisionInfo;
    using MphRead::Formats::Collision::CollisionInstance;
    using MphRead::Formats::Collision::FhCollisionData;
    using MphRead::Formats::Collision::FhCollisionInfo;
    using MphRead::Formats::Collision::FhCollisionVector;
    using MphRead::Formats::Collision::MphCollisionInfo;
    using MphRead::Formats::Collision::Portal;
    using MphRead::Formats::Collision::RawCollisionPortal;
    using OpenTK::Mathematics::Vector3;
    using OpenTK::Mathematics::Vector4;

    [[nodiscard]] std::int32_t ToIntCount(std::size_t value) noexcept
    {
        return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(value));
    }

    [[nodiscard]] bool Vector3Equals(Vector3 left, Vector3 right) noexcept
    {
        return left.X == right.X && left.Y == right.Y && left.Z == right.Z;
    }

    [[nodiscard]] bool Vector4Equals(Vector4 left, Vector4 right) noexcept
    {
        return left.X == right.X && left.Y == right.Y
            && left.Z == right.Z && left.W == right.W;
    }

    [[nodiscard]] float ComponentMin(const std::vector<Vector3>& points, std::int32_t component)
    {
        if (points.empty())
        {
            throw System::InvalidOperationException("Sequence contains no elements");
        }
        const auto get = [component](Vector3 value)
        {
            return component == 0 ? value.X : component == 1 ? value.Y : value.Z;
        };
        float result = get(points.front());
        if (std::isnan(result))
        {
            return result;
        }
        for (std::size_t i = 1; i < points.size(); ++i)
        {
            const float current = get(points[i]);
            if (current < result)
            {
                result = current;
            }
            else if (std::isnan(current))
            {
                return current;
            }
        }
        return result;
    }

    [[nodiscard]] float ComponentMax(const std::vector<Vector3>& points, std::int32_t component)
    {
        if (points.empty())
        {
            throw System::InvalidOperationException("Sequence contains no elements");
        }
        const auto get = [component](Vector3 value)
        {
            return component == 0 ? value.X : component == 1 ? value.Y : value.Z;
        };
        std::size_t i = 0;
        float result = get(points[i]);
        while (std::isnan(result))
        {
            ++i;
            if (i == points.size())
            {
                return result;
            }
            result = get(points[i]);
        }
        for (++i; i < points.size(); ++i)
        {
            const float current = get(points[i]);
            if (current > result)
            {
                result = current;
            }
        }
        return result;
    }

    [[nodiscard]] std::int32_t FindPoint(
        const std::vector<Vector3>& values, Vector3 value) noexcept
    {
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            if (Vector3Equals(values[i], value))
            {
                return ToIntCount(i);
            }
        }
        return -1;
    }

    [[nodiscard]] std::int32_t FindPlane(
        const std::vector<Vector4>& values, Vector4 value) noexcept
    {
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            if (Vector4Equals(values[i], value))
            {
                return ToIntCount(i);
            }
        }
        return -1;
    }

    [[nodiscard]] std::span<const std::uint8_t> Span(
        const std::vector<std::uint8_t>& bytes) noexcept
    {
        return std::span<const std::uint8_t>(bytes.data(), bytes.size());
    }

    [[nodiscard]] RoomMetadata& GetRoomMetadata(const std::string& room)
    {
        const auto iterator = MphRead::Metadata::RoomMetadata.find(room);
        if (iterator == MphRead::Metadata::RoomMetadata.end())
        {
            throw MphRead::SceneDetail::KeyNotFoundException();
        }
        return RequireReference(iterator->second);
    }

    struct CollisionDataPack final
    {
        const std::shared_ptr<CollisionDataEditor> Editor;
        std::uint16_t PlaneIndex = 0;
        std::uint16_t PointIndexCount = 0;
        std::uint16_t PointStartIndex = 0;

        CollisionDataPack(
            std::shared_ptr<CollisionDataEditor> editor,
            std::int32_t planeIndex,
            std::int32_t pointIndexCount,
            std::int32_t pointStartIndex)
            : Editor(std::move(editor)),
              PlaneIndex(static_cast<std::uint16_t>(planeIndex)),
              PointIndexCount(static_cast<std::uint16_t>(pointIndexCount)),
              PointStartIndex(static_cast<std::uint16_t>(pointStartIndex))
        {
        }
    };

    struct CollisionPortalPack final
    {
        const std::shared_ptr<Portal> PortalValue;
        std::uint16_t PlaneIndex = 0;
        std::uint16_t PointIndexCount = 0;
        std::uint16_t PointStartIndex = 0;

        CollisionPortalPack(
            std::shared_ptr<Portal> portal,
            std::int32_t planeIndex,
            std::int32_t pointIndexCount,
            std::int32_t pointStartIndex)
            : PortalValue(std::move(portal)),
              PlaneIndex(static_cast<std::uint16_t>(planeIndex)),
              PointIndexCount(static_cast<std::uint16_t>(pointIndexCount)),
              PointStartIndex(static_cast<std::uint16_t>(pointStartIndex))
        {
        }
    };

    struct TreeNodePack final
    {
        Vector3 MinBounds{};
        Vector3 MaxBounds{};
        std::int32_t LeftIndex = 0;
        std::int32_t RightIndex = 0;
    };

    [[nodiscard]] std::int32_t GetPrimaryAxis(Vector3 normal) noexcept
    {
        const float x = std::fabs(normal.X);
        const float y = std::fabs(normal.Y);
        const float z = std::fabs(normal.Z);
        if (y > x && y >= z)
        {
            return 1;
        }
        if (z > x && z > y)
        {
            return 2;
        }
        return 0;
    }

    [[nodiscard]] std::vector<std::shared_ptr<CollisionDataEditor>> GetEditors(
        const MphCollisionInfo& info)
    {
        std::vector<std::shared_ptr<CollisionDataEditor>> editors;
        const auto& dataValues = RequireReference(info.Data);
        for (const CollisionData& data : dataValues)
        {
            const Vector4 plane = ManagedListAt(
                RequireReference(info.Planes), static_cast<std::int32_t>(data.PlaneIndex));
            const Vector3 normal = plane.Xyz();
            auto editor = std::make_shared<CollisionDataEditor>();
            editor->LayerMask = static_cast<std::uint16_t>(
                (data.LayerMask & 0xFFFCU) | static_cast<std::uint16_t>(GetPrimaryAxis(normal)));
            editor->Flags = data.Flags;
            editor->Plane = Vector4(normal, plane.W);
            for (std::int32_t i = 0; i < static_cast<std::int32_t>(data.PointIndexCount); ++i)
            {
                const std::int32_t pointIndexPosition = UncheckedAdd(
                    static_cast<std::int32_t>(data.PointStartIndex), i);
                const std::uint16_t pointIndex = ManagedListAt(
                    RequireReference(info.PointIndices), pointIndexPosition);
                editor->Points->push_back(ManagedListAt(
                    RequireReference(info.Points), static_cast<std::int32_t>(pointIndex)));
            }
            editors.push_back(std::move(editor));
        }
        return editors;
    }

    [[nodiscard]] std::vector<std::shared_ptr<CollisionDataEditor>> GetEditors(
        const FhCollisionInfo& info)
    {
#if defined(DEBUG)
        const auto& dataIndices = RequireReference(info.DataIndices);
        REPACK_COLLISION_DEBUG_ASSERT(info.Header.DataCount == dataIndices.size());
        const auto& debugDataValues = RequireReference(info.Data);
        REPACK_COLLISION_DEBUG_ASSERT(info.Header.DataCount == debugDataValues.size());
        std::size_t nonPortalCount = 0;
        std::size_t portalPaddingCount = 0;
        std::unordered_set<std::uint16_t> distinctNonPortal;
        for (std::uint16_t value : dataIndices)
        {
            if (value == 0xCDCDU)
            {
                ++portalPaddingCount;
            }
            else
            {
                ++nonPortalCount;
                distinctNonPortal.insert(value);
            }
        }
        REPACK_COLLISION_DEBUG_ASSERT(
            static_cast<std::uint32_t>(info.Header.DataCount) - info.Header.PortalCount
                == nonPortalCount);
        REPACK_COLLISION_DEBUG_ASSERT(info.Header.PortalCount == portalPaddingCount);
        REPACK_COLLISION_DEBUG_ASSERT(nonPortalCount == distinctNonPortal.size());
#endif

        std::vector<std::shared_ptr<CollisionDataEditor>> editors;
        const std::uint32_t sourceCount
            = static_cast<std::uint32_t>(info.Header.DataCount) - info.Header.PortalCount;
        for (std::int32_t i = 0;
            static_cast<std::int64_t>(i) < static_cast<std::int64_t>(sourceCount);
            i = UncheckedAdd(i, 1))
        {
            const std::int32_t dataIndex = UncheckedAdd(
                i, std::bit_cast<std::int32_t>(info.Header.PortalCount));
            const FhCollisionData& data = ManagedListAt(RequireReference(info.Data), dataIndex);
            const Vector4 plane = ManagedListAt(
                RequireReference(info.Planes), static_cast<std::int32_t>(data.PlaneIndex));
            const Vector3 normal = plane.Xyz();
            const std::int32_t axis = GetPrimaryAxis(normal);
            auto editor = std::make_shared<CollisionDataEditor>();
            editor->LayerMask = static_cast<std::uint16_t>(4 | axis);
            editor->Plane = Vector4(normal, plane.W);
            for (std::int32_t j = 0; j < static_cast<std::int32_t>(data.VectorCount); ++j)
            {
                const std::int32_t vectorIndex = UncheckedAdd(
                    static_cast<std::int32_t>(data.VectorStartIndex), j);
                const FhCollisionVector& vector = ManagedListAt(
                    RequireReference(info.Vectors), vectorIndex);
                editor->Points->push_back(ManagedListAt(
                    RequireReference(info.Points), static_cast<std::int32_t>(vector.Point2Index)));
            }
            editors.push_back(std::move(editor));
        }
        return editors;
    }

    [[nodiscard]] std::vector<std::shared_ptr<CollisionDataEditor>> GetEditors(
        const std::shared_ptr<CollisionInstance>& collision)
    {
        CollisionInstance& value = RequireReference(collision);
        CollisionInfo& info = RequireReference(value.Info);
        if (auto* mphInfo = dynamic_cast<MphCollisionInfo*>(std::addressof(info)))
        {
            return GetEditors(*mphInfo);
        }
        if (auto* fhInfo = dynamic_cast<FhCollisionInfo*>(std::addressof(info)))
        {
            return GetEditors(*fhInfo);
        }
        throw MphRead::SceneDetail::InvalidCastException();
    }

    [[nodiscard]] bool TestIntersection(
        Vector3 point1, Vector3 point2,
        Vector3 v0, Vector3 v1, Vector3 v2, float& t)
    {
        t = 0.0F;
        const float eps = 0.00001F;
        std::array<float, 3> b{};
        const Vector3 between = point2 - point1;
        const Vector3 p = point1;
        const Vector3 w = between.Normalized();
        const Vector3 e1 = v1 - v0;
        const Vector3 e2 = v2 - v0;
        const Vector3 n = Vector3::Cross(e1, e2).Normalized();
        (void)n;
        const Vector3 q = Vector3::Cross(w, e2);
        const float a = Vector3::Dot(e1, q);
        if (std::fabs(a) <= eps)
        {
            return false;
        }
        const Vector3 s = Divide(p - v0, a);
        const Vector3 r = Vector3::Cross(s, e1);
        b[0] = Vector3::Dot(s, q);
        b[1] = Vector3::Dot(r, w);
        b[2] = 1.0F - b[0] - b[1];
        if (b[0] < 0.0F || b[1] < 0.0F || b[2] < 0.0F)
        {
            return false;
        }
        t = Vector3::Dot(e2, r);
        return t >= 0.0F;
    }

    [[nodiscard]] bool CheckIntersection(
        Vector3 point1, Vector3 point2, const std::vector<Vector3>& face)
    {
        const Vector3 between = point2 - point1;
        const Vector3 ro = point1;
        const Vector3 rd = between.Normalized();
        (void)ro;
        (void)rd;
        const std::int32_t count = ToIntCount(face.size());
        for (std::int32_t i = 0; i < count - 2; ++i)
        {
            const Vector3 v0 = ManagedListAt(face, 0);
            const Vector3 v1 = ManagedListAt(face, UncheckedAdd(i, 1));
            const Vector3 v2 = ManagedListAt(face, UncheckedAdd(i, 2));
            float t = 0.0F;
            const bool intersect = TestIntersection(point1, point2, v0, v1, v2, t);
            if (intersect && t <= Length(between))
            {
                return true;
            }
        }
        return false;
    }

    void ThrowIfInvalid(const std::shared_ptr<CollisionDataEditor>& data)
    {
        CollisionDataEditor& value = RequireReference(data);
        const std::int32_t count = ToIntCount(value.Points->size());
        if (count < 3)
        {
            throw ProgramException("Collision face must have at least 3 vertices.");
        }
        if (count > 10)
        {
            throw ProgramException("Collision face may not have more than 10 vertices.");
        }
    }

    [[nodiscard]] std::vector<std::uint16_t> GetDataInRegion(
        Vector3 minBounds, Vector3 maxBounds,
        const std::vector<std::shared_ptr<CollisionDataEditor>>& data)
    {
        std::vector<std::uint16_t> indices;
        const float xStart = minBounds.X;
        const float yStart = minBounds.Y;
        const float zStart = minBounds.Z;
        const float xEnd = maxBounds.X;
        const float yEnd = maxBounds.Y;
        const float zEnd = maxBounds.Z;
        const Vector3 p1(xStart, yStart, zStart);
        const Vector3 p2(xEnd, yStart, zStart);
        const Vector3 p3(xStart, yStart, zEnd);
        const Vector3 p4(xEnd, yStart, zEnd);
        const Vector3 p5(xStart, yEnd, zStart);
        const Vector3 p6(xEnd, yEnd, zStart);
        const Vector3 p7(xStart, yEnd, zEnd);
        const Vector3 p8(xEnd, yEnd, zEnd);
        const std::vector<std::vector<Vector3>> faces{
            {p1, p5, p6, p2},
            {p1, p3, p4, p2},
            {p1, p5, p7, p3},
            {p3, p7, p8, p4},
            {p2, p6, p8, p4},
            {p5, p7, p8, p6}
        };
        const std::vector<std::array<Vector3, 2>> edges{
            {p1, p5}, {p3, p7}, {p2, p6}, {p4, p8},
            {p1, p3}, {p2, p4}, {p5, p7}, {p6, p8},
            {p1, p2}, {p5, p6}, {p3, p4}, {p7, p8}
        };

        for (std::int32_t i = 0; i < ToIntCount(data.size()); ++i)
        {
            CollisionDataEditor& item = RequireReference(ManagedListAt(data, i));
            const std::vector<Vector3>& itemPoints = *item.Points;
            if (ComponentMin(itemPoints, 0) > xEnd
                || ComponentMax(itemPoints, 0) < xStart
                || ComponentMin(itemPoints, 1) > yEnd
                || ComponentMax(itemPoints, 1) < yStart
                || ComponentMin(itemPoints, 2) > zEnd
                || ComponentMax(itemPoints, 2) < zStart)
            {
                continue;
            }

            bool intersects = false;
            for (Vector3 point : itemPoints)
            {
                if (point.X >= xStart && point.X < xEnd
                    && point.Y >= yStart && point.Y < yEnd
                    && point.Z >= zStart && point.Z < zEnd)
                {
                    intersects = true;
                    break;
                }
            }

            if (!intersects)
            {
                for (std::int32_t j = 0;
                    j < ToIntCount(faces.size()) && !intersects; ++j)
                {
                    const std::vector<Vector3>& face = ManagedListAt(faces, j);
                    const std::int32_t pointCount = ToIntCount(itemPoints.size());
                    for (std::int32_t k = 0; k < pointCount - 1 && !intersects; ++k)
                    {
                        intersects |= CheckIntersection(
                            ManagedListAt(itemPoints, k),
                            ManagedListAt(itemPoints, UncheckedAdd(k, 1)), face);
                    }
                    if (!intersects)
                    {
                        intersects |= CheckIntersection(
                            ManagedListAt(itemPoints, UncheckedAdd(pointCount, -1)),
                            ManagedListAt(itemPoints, 0), face);
                    }
                }
            }

            if (!intersects)
            {
                for (const auto& edge : edges)
                {
                    intersects |= CheckIntersection(edge[0], edge[1], itemPoints);
                    if (intersects)
                    {
                        break;
                    }
                }
            }
            if (intersects)
            {
                indices.push_back(static_cast<std::uint16_t>(i));
            }
        }
        return indices;
    }

    [[nodiscard]] Vector4 GetPlane(Vector3 point1, Vector3 point2, Vector3 normal)
    {
        const Vector3 p2to1 = point1 - point2;
        const Vector3 p2to1n = p2to1.Normalized();
        const Vector3 cross = Vector3::Cross(p2to1n, normal).Normalized();
        const float dist = cross.X * point1.X
            + cross.Y * point1.Y + cross.Z * point1.Z;
        return Vector4(cross, dist);
    }

    void WriteMphPortal(BinaryWriter& writer, const std::shared_ptr<Portal>& portal)
    {
        Portal& value = RequireReference(portal);
        const auto& points = RequireReference(value.Points);
        const auto& planes = RequireReference(value.Planes);
        REPACK_COLLISION_DEBUG_ASSERT(points.size() == 4);
        REPACK_COLLISION_DEBUG_ASSERT(planes.size() == 4);
        writer.WriteString(value.Name, 40);
        writer.WriteString(value.NodeName1, 24);
        writer.WriteString(value.NodeName2, 24);
        for (Vector3 point : points)
        {
            writer.WriteVector3(point);
        }
        for (Vector4 vector : planes)
        {
            writer.WriteVector4(vector);
        }
        writer.WriteVector4(value.Plane);
        writer.Write(static_cast<std::uint16_t>(0));
        writer.Write(value.LayerMask);
        writer.Write(static_cast<std::uint16_t>(ToIntCount(points.size())));
        writer.Write(value.Unknown00);
        writer.Write(value.Unknown01);
    }

    [[nodiscard]] std::vector<std::uint8_t> RepackMphCollisionSimple(
        const MphCollisionInfo& info)
    {
        const std::uint32_t padInt = 0;
        const std::uint16_t padShort = 0;
        const std::uint8_t padByte = 0;
        BinaryWriter writer;
        writer.Position(sizeof(CollisionHeader));

        const std::int32_t pointOffset = ToIntCount(writer.Position());
        for (Vector3 point : RequireReference(info.Points))
        {
            writer.WriteVector3(point);
        }
        const std::int32_t planeOffset = ToIntCount(writer.Position());
        for (Vector4 plane : RequireReference(info.Planes))
        {
            writer.WriteVector4(plane);
        }
        const std::int32_t pointIdxOffset = ToIntCount(writer.Position());
        for (std::uint16_t index : RequireReference(info.PointIndices))
        {
            writer.Write(index);
        }
        while (writer.Position() % 4 != 0)
        {
            writer.Write(padByte);
        }
        const std::int32_t dataOffset = ToIntCount(writer.Position());
        for (const CollisionData& data : RequireReference(info.Data))
        {
            writer.Write(padInt);
            writer.Write(data.PlaneIndex);
            writer.Write(static_cast<std::uint16_t>(data.Flags));
            writer.Write(data.LayerMask);
            writer.Write(padShort);
            writer.Write(data.PointIndexCount);
            writer.Write(data.PointStartIndex);
        }
        const std::int32_t dataIdxOffset = ToIntCount(writer.Position());
        for (std::uint16_t index : RequireReference(info.DataIndices))
        {
            writer.Write(index);
        }
        while (writer.Position() % 4 != 0)
        {
            writer.Write(padByte);
        }
        const std::int32_t entryOffset = ToIntCount(writer.Position());
        for (const CollisionEntry& entry : RequireReference(info.Entries))
        {
            writer.Write(entry.DataCount);
            writer.Write(static_cast<std::uint16_t>(entry.DataStartIndex));
        }
        const std::int32_t portalOffset = ToIntCount(writer.Position());
        for (const auto& portal : RequireReference(info.Portals))
        {
            WriteMphPortal(writer, portal);
        }

        writer.Position(0);
        writer.Write(static_cast<std::uint8_t>('w'));
        writer.Write(static_cast<std::uint8_t>('c'));
        writer.Write(static_cast<std::uint8_t>('0'));
        writer.Write(static_cast<std::uint8_t>('1'));
        writer.Write(ToIntCount(RequireReference(info.Points).size()));
        writer.Write(pointOffset);
        writer.Write(ToIntCount(RequireReference(info.Planes).size()));
        writer.Write(planeOffset);
        writer.Write(ToIntCount(RequireReference(info.PointIndices).size()));
        writer.Write(pointIdxOffset);
        writer.Write(ToIntCount(RequireReference(info.Data).size()));
        writer.Write(dataOffset);
        writer.Write(ToIntCount(RequireReference(info.DataIndices).size()));
        writer.Write(dataIdxOffset);
        writer.Write(info.Header.PartsX);
        writer.Write(info.Header.PartsY);
        writer.Write(info.Header.PartsZ);
        writer.WriteVector3(info.MinPosition);
        writer.Write(ToIntCount(RequireReference(info.Entries).size()));
        writer.Write(entryOffset);
        writer.Write(ToIntCount(RequireReference(info.Portals).size()));
        writer.Write(portalOffset);
        return writer.ToArray();
    }

    [[nodiscard]] std::vector<std::uint8_t> RepackMphCollision(
        const std::vector<std::shared_ptr<CollisionDataEditor>>& data,
        const std::shared_ptr<const std::vector<std::shared_ptr<Portal>>>& portals)
    {
        const std::uint32_t padInt = 0;
        const std::uint16_t padShort = 0;
        const std::uint8_t padByte = 0;
        std::vector<Vector3> points;
        std::vector<Vector4> planes;
        std::vector<std::uint16_t> pointIdxs;
        std::vector<CollisionDataPack> dataPack;
        std::int32_t partsX = 1;
        std::int32_t partsY = 1;
        std::int32_t partsZ = 1;
        float minX = std::numeric_limits<float>::max();
        float minY = std::numeric_limits<float>::max();
        float minZ = std::numeric_limits<float>::max();
        float maxX = std::numeric_limits<float>::lowest();
        float maxY = std::numeric_limits<float>::lowest();
        float maxZ = std::numeric_limits<float>::lowest();
        std::vector<std::uint16_t> dataIdxs;
        std::vector<std::pair<std::uint16_t, std::uint16_t>> entries;

        REPACK_COLLISION_DEBUG_ASSERT(!data.empty());
        for (std::int32_t i = 0; i < ToIntCount(data.size()); ++i)
        {
            const auto& item = ManagedListAt(data, i);
            ThrowIfInvalid(item);
            CollisionDataEditor& itemValue = RequireReference(item);
            for (Vector3 point : *itemValue.Points)
            {
                if (FindPoint(points, point) == -1)
                {
                    points.push_back(point);
                    minX = MathMin(minX, point.X);
                    minY = MathMin(minY, point.Y);
                    minZ = MathMin(minZ, point.Z);
                    maxX = MathMax(maxX, point.X);
                    maxY = MathMax(maxY, point.Y);
                    maxZ = MathMax(maxZ, point.Z);
                }
            }
            const Vector4 plane = itemValue.Plane;
            std::int32_t planeIndex = FindPlane(planes, plane);
            if (planeIndex == -1)
            {
                planeIndex = ToIntCount(planes.size());
                planes.push_back(plane);
            }
            const std::int32_t idxCount = ToIntCount(itemValue.Points->size());
            const std::int32_t idxStart = ToIntCount(pointIdxs.size());
            for (Vector3 point : *itemValue.Points)
            {
                pointIdxs.push_back(static_cast<std::uint16_t>(FindPoint(points, point)));
            }
            pointIdxs.push_back(ManagedListAt(pointIdxs, idxStart));
            dataPack.emplace_back(item, planeIndex, idxCount, idxStart);
        }

        while (minX + static_cast<float>(UncheckedMultiply(partsX, 4)) <= maxX)
        {
            partsX = UncheckedAdd(partsX, 1);
        }
        while (minY + static_cast<float>(UncheckedMultiply(partsY, 4)) <= maxY)
        {
            partsY = UncheckedAdd(partsY, 1);
        }
        while (minZ + static_cast<float>(UncheckedMultiply(partsZ, 4)) <= maxZ)
        {
            partsZ = UncheckedAdd(partsZ, 1);
        }

        for (std::int32_t py = 0; py < partsY; py = UncheckedAdd(py, 1))
        {
            for (std::int32_t pz = 0; pz < partsZ; pz = UncheckedAdd(pz, 1))
            {
                for (std::int32_t px = 0; px < partsX; px = UncheckedAdd(px, 1))
                {
                    const std::int32_t index = UncheckedAdd(
                        UncheckedAdd(UncheckedMultiply(UncheckedMultiply(py, partsX), partsZ),
                            UncheckedMultiply(pz, partsX)), px);
                    (void)index;
                    const float xStart = minX + static_cast<float>(UncheckedMultiply(px, 4));
                    const float xEnd = xStart + 4.0F;
                    const float yStart = minY + static_cast<float>(UncheckedMultiply(py, 4));
                    const float yEnd = yStart + 4.0F;
                    const float zStart = minZ + static_cast<float>(UncheckedMultiply(pz, 4));
                    const float zEnd = zStart + 4.0F;
                    const Vector3 minBounds(xStart, yStart, zStart);
                    const Vector3 maxBounds(xEnd, yEnd, zEnd);
                    const std::int32_t idxStart = ToIntCount(dataIdxs.size());
                    std::vector<std::uint16_t> region = GetDataInRegion(minBounds, maxBounds, data);
                    dataIdxs.insert(dataIdxs.end(), region.begin(), region.end());
                    const std::int32_t idxCount = UncheckedAdd(ToIntCount(dataIdxs.size()), -idxStart);
                    entries.emplace_back(
                        static_cast<std::uint16_t>(idxCount),
                        static_cast<std::uint16_t>(idxStart));
                }
            }
        }

        BinaryWriter writer;
        writer.Position(sizeof(CollisionHeader));
        const std::int32_t pointOffset = ToIntCount(writer.Position());
        for (Vector3 point : points)
        {
            writer.WriteVector3(point);
        }
        const std::int32_t planeOffset = ToIntCount(writer.Position());
        for (Vector4 plane : planes)
        {
            writer.WriteVector4(plane);
        }
        const std::int32_t pointIdxOffset = ToIntCount(writer.Position());
        for (std::uint16_t index : pointIdxs)
        {
            writer.Write(index);
        }
        while (writer.Position() % 4 != 0)
        {
            writer.Write(padByte);
        }
        const std::int32_t dataOffset = ToIntCount(writer.Position());
        for (const CollisionDataPack& pack : dataPack)
        {
            CollisionDataEditor& editor = RequireReference(pack.Editor);
            writer.Write(padInt);
            writer.Write(pack.PlaneIndex);
            writer.Write(static_cast<std::uint16_t>(editor.Flags));
            writer.Write(editor.LayerMask);
            writer.Write(padShort);
            writer.Write(pack.PointIndexCount);
            writer.Write(pack.PointStartIndex);
        }
        const std::int32_t dataIdxOffset = ToIntCount(writer.Position());
        for (std::uint16_t index : dataIdxs)
        {
            writer.Write(index);
        }
        while (writer.Position() % 4 != 0)
        {
            writer.Write(padByte);
        }
        const std::int32_t entryOffset = ToIntCount(writer.Position());
        for (const auto& entry : entries)
        {
            writer.Write(entry.first);
            writer.Write(entry.second);
        }
        const std::int32_t portalOffset = ToIntCount(writer.Position());
        for (const auto& portal : RequireReference(portals))
        {
            WriteMphPortal(writer, portal);
        }

        writer.Position(0);
        writer.Write(static_cast<std::uint8_t>('w'));
        writer.Write(static_cast<std::uint8_t>('c'));
        writer.Write(static_cast<std::uint8_t>('0'));
        writer.Write(static_cast<std::uint8_t>('1'));
        writer.Write(ToIntCount(points.size()));
        writer.Write(pointOffset);
        writer.Write(ToIntCount(planes.size()));
        writer.Write(planeOffset);
        writer.Write(ToIntCount(pointIdxs.size()));
        writer.Write(pointIdxOffset);
        writer.Write(ToIntCount(data.size()));
        writer.Write(dataOffset);
        writer.Write(ToIntCount(dataIdxs.size()));
        writer.Write(dataIdxOffset);
        writer.Write(partsX);
        writer.Write(partsY);
        writer.Write(partsZ);
        writer.WriteFloat(minX);
        writer.WriteFloat(minY);
        writer.WriteFloat(minZ);
        writer.Write(ToIntCount(entries.size()));
        writer.Write(entryOffset);
        writer.Write(ToIntCount(RequireReference(portals).size()));
        writer.Write(portalOffset);
        return writer.ToArray();
    }

    [[nodiscard]] std::vector<std::uint8_t> RepackFhCollision(
        const std::vector<std::shared_ptr<CollisionDataEditor>>& data,
        const std::shared_ptr<const std::vector<std::shared_ptr<Portal>>>& portals)
    {
        const std::uint8_t padByte = 0;
        const std::uint16_t padShort = 0;
        float minX = std::numeric_limits<float>::max();
        float minY = std::numeric_limits<float>::max();
        float minZ = std::numeric_limits<float>::max();
        float maxX = std::numeric_limits<float>::lowest();
        float maxY = std::numeric_limits<float>::lowest();
        float maxZ = std::numeric_limits<float>::lowest();
        std::vector<CollisionPortalPack> portalPacks;
        std::vector<CollisionDataPack> dataPacks;
        std::vector<Vector3> points;
        std::vector<std::tuple<std::int32_t, std::int32_t, std::int32_t>> vectors;
        std::vector<Vector4> planes;

        const auto addItems = [&](const std::vector<Vector3>& verts, Vector3 normal)
        {
            const std::int32_t count = ToIntCount(verts.size());
            for (std::int32_t i = 0; i < count; ++i)
            {
                const Vector3 point1 = ManagedListAt(
                    verts, i == 0 ? UncheckedAdd(count, -1) : UncheckedAdd(i, -1));
                const Vector3 point2 = ManagedListAt(verts, i);
                const Vector4 plane = GetPlane(point1, point2, normal);
                std::int32_t point1Index = FindPoint(points, point1);
                std::int32_t point2Index = FindPoint(points, point2);
                std::int32_t planeIndex = FindPlane(planes, plane);
                if (point1Index == -1)
                {
                    point1Index = ToIntCount(points.size());
                    points.push_back(point1);
                }
                if (point2Index == -1)
                {
                    point2Index = ToIntCount(points.size());
                    points.push_back(point2);
                }
                if (planeIndex == -1)
                {
                    planeIndex = ToIntCount(planes.size());
                    planes.push_back(plane);
                }
                vectors.emplace_back(point1Index, point2Index, planeIndex);
                minX = MathMin(minX, point2.X);
                minY = MathMin(minY, point2.Y);
                minZ = MathMin(minZ, point2.Z);
                maxX = MathMax(maxX, point2.X);
                maxY = MathMax(maxY, point2.Y);
                maxZ = MathMax(maxZ, point2.Z);
            }
        };

        for (const auto& portal : RequireReference(portals))
        {
            Portal& value = RequireReference(portal);
            std::int32_t planeIndex = FindPlane(planes, value.Plane);
            if (planeIndex == -1)
            {
                planeIndex = ToIntCount(planes.size());
                planes.push_back(value.Plane);
            }
            const std::int32_t vectorIndex = ToIntCount(vectors.size());
            const auto& portalPoints = RequireReference(value.Points);
            addItems(portalPoints, value.Plane.Xyz());
            portalPacks.emplace_back(
                portal, planeIndex, ToIntCount(portalPoints.size()), vectorIndex);
        }
        for (const auto& item : data)
        {
            CollisionDataEditor& value = RequireReference(item);
            std::int32_t planeIndex = FindPlane(planes, value.Plane);
            if (planeIndex == -1)
            {
                planeIndex = ToIntCount(planes.size());
                planes.push_back(value.Plane);
            }
            const std::int32_t vectorIndex = ToIntCount(vectors.size());
            addItems(*value.Points, value.Plane.Xyz());
            dataPacks.emplace_back(
                item, planeIndex, ToIntCount(value.Points->size()), vectorIndex);
        }

        std::vector<std::shared_ptr<TreeNodePack>> treeNodes;
        const float dummyMin = Fixed::ToFloat(static_cast<std::int32_t>(0x50000000));
        const float dummyMax = Fixed::ToFloat(std::bit_cast<std::int32_t>(0xB0000000U));
        auto dummy = std::make_shared<TreeNodePack>();
        dummy->MinBounds = Vector3(dummyMin, dummyMin, dummyMin);
        dummy->MaxBounds = Vector3(dummyMax, dummyMax, dummyMax);
        dummy->LeftIndex = 0xCDCD;
        dummy->RightIndex = 0xCDCD;
        treeNodes.push_back(dummy);

        std::function<void(const std::shared_ptr<TreeNodePack>&)> makeNodes;
        makeNodes = [&](const std::shared_ptr<TreeNodePack>& parent)
        {
            TreeNodePack& parentValue = RequireReference(parent);
            const float sizeX = parentValue.MaxBounds.X - parentValue.MinBounds.X;
            const float sizeY = parentValue.MaxBounds.Y - parentValue.MinBounds.Y;
            const float sizeZ = parentValue.MaxBounds.Z - parentValue.MinBounds.Z;
            if (sizeX < 8.0F && sizeY < 8.0F && sizeZ < 8.0F)
            {
                parentValue.LeftIndex = parentValue.RightIndex = 0x8000;
                return;
            }
            Vector3 newMax;
            Vector3 newMin;
            if (sizeX >= sizeY && sizeX >= sizeZ)
            {
                newMax = Vector3(
                    parentValue.MaxBounds.X - sizeX / 2.0F,
                    parentValue.MaxBounds.Y, parentValue.MaxBounds.Z);
                newMin = Vector3(
                    parentValue.MinBounds.X + sizeX / 2.0F,
                    parentValue.MinBounds.Y, parentValue.MinBounds.Z);
            }
            else if (sizeY > sizeX && sizeY >= sizeZ)
            {
                newMax = Vector3(
                    parentValue.MaxBounds.X,
                    parentValue.MaxBounds.Y - sizeY / 2.0F,
                    parentValue.MaxBounds.Z);
                newMin = Vector3(
                    parentValue.MinBounds.X,
                    parentValue.MinBounds.Y + sizeY / 2.0F,
                    parentValue.MinBounds.Z);
            }
            else
            {
                newMax = Vector3(
                    parentValue.MaxBounds.X, parentValue.MaxBounds.Y,
                    parentValue.MaxBounds.Z - sizeZ / 2.0F);
                newMin = Vector3(
                    parentValue.MinBounds.X, parentValue.MinBounds.Y,
                    parentValue.MinBounds.Z + sizeZ / 2.0F);
            }
            parentValue.LeftIndex = ToIntCount(treeNodes.size());
            auto leftNode = std::make_shared<TreeNodePack>();
            leftNode->MinBounds = parentValue.MinBounds;
            leftNode->MaxBounds = newMax;
            treeNodes.push_back(leftNode);
            parentValue.RightIndex = ToIntCount(treeNodes.size());
            auto rightNode = std::make_shared<TreeNodePack>();
            rightNode->MinBounds = newMin;
            rightNode->MaxBounds = parentValue.MaxBounds;
            treeNodes.push_back(rightNode);
            makeNodes(leftNode);
            makeNodes(rightNode);
        };

        auto startNode = std::make_shared<TreeNodePack>();
        startNode->MinBounds = Vector3(minX, minY, minZ);
        startNode->MaxBounds = Vector3(maxX, maxY, maxZ);
        treeNodes.push_back(startNode);
        makeNodes(startNode);

        std::vector<std::uint16_t> dataIdxs;
        std::vector<std::shared_ptr<TreeNodePack>> entries;
        for (const auto& node : treeNodes)
        {
            TreeNodePack& nodeValue = RequireReference(node);
            if (nodeValue.RightIndex != 0x8000)
            {
                continue;
            }
            nodeValue.LeftIndex = ToIntCount(entries.size());
            const std::int32_t idxStart = ToIntCount(dataIdxs.size());
            std::vector<std::uint16_t> region
                = GetDataInRegion(nodeValue.MinBounds, nodeValue.MaxBounds, data);
            dataIdxs.insert(dataIdxs.end(), region.begin(), region.end());
            const std::int32_t idxCount = UncheckedAdd(ToIntCount(dataIdxs.size()), -idxStart);
            auto entry = std::make_shared<TreeNodePack>();
            entry->MinBounds = nodeValue.MinBounds;
            entry->MaxBounds = nodeValue.MaxBounds;
            entry->LeftIndex = idxCount;
            entry->RightIndex = idxStart;
            entries.push_back(std::move(entry));
        }
        for (std::int32_t i = 0; i < ToIntCount(dataIdxs.size()); ++i)
        {
            dataIdxs[static_cast<std::size_t>(i)] = static_cast<std::uint16_t>(
                UncheckedAdd(
                    static_cast<std::int32_t>(dataIdxs[static_cast<std::size_t>(i)]),
                    ToIntCount(RequireReference(portals).size())));
        }

        using FhCollisionHeader = MphRead::Formats::Collision::FhCollisionHeader;
        BinaryWriter writer;
        writer.Position(sizeof(FhCollisionHeader));
        const std::int32_t pointOffset = ToIntCount(writer.Position());
        for (Vector3 point : points)
        {
            writer.WriteVector3(point);
        }
        const std::int32_t planeOffset = ToIntCount(writer.Position());
        for (Vector4 plane : planes)
        {
            writer.WriteVector4(plane);
        }
        const std::int32_t vectorOffset = ToIntCount(writer.Position());
        for (const auto& vector : vectors)
        {
            writer.Write(static_cast<std::uint16_t>(std::get<0>(vector)));
            writer.Write(static_cast<std::uint16_t>(std::get<1>(vector)));
            writer.Write(static_cast<std::uint16_t>(std::get<2>(vector)));
        }
        while (writer.Position() % 4 != 0)
        {
            writer.Write(padByte);
        }
        const std::int32_t dataOffset = ToIntCount(writer.Position());
        for (const CollisionPortalPack& portal : portalPacks)
        {
            writer.Write(portal.PlaneIndex);
            writer.Write(portal.PointIndexCount);
            writer.Write(portal.PointStartIndex);
        }
        for (const CollisionDataPack& item : dataPacks)
        {
            writer.Write(item.PlaneIndex);
            writer.Write(item.PointIndexCount);
            writer.Write(item.PointStartIndex);
        }
        while (writer.Position() % 4 != 0)
        {
            writer.Write(padByte);
        }
        const std::int32_t dataIndexOffset = ToIntCount(writer.Position());
        for (std::uint16_t index : dataIdxs)
        {
            writer.Write(index);
        }
        while (writer.Position() % 4 != 0)
        {
            writer.Write(padByte);
        }
        const std::int32_t entryOffset = ToIntCount(writer.Position());
        for (const auto& entry : entries)
        {
            TreeNodePack& value = RequireReference(entry);
            writer.WriteVector3(value.MinBounds);
            writer.WriteVector3(value.MaxBounds);
            writer.Write(static_cast<std::uint16_t>(value.LeftIndex));
            writer.Write(static_cast<std::uint16_t>(value.RightIndex));
        }
        const std::int32_t treeNodeIndexOffset = ToIntCount(writer.Position());
        const std::int32_t treeNodeIndexCount = 1;
        writer.Write(static_cast<std::uint16_t>(1));
        writer.Write(padShort);
        const std::int32_t treeNodeOffset = ToIntCount(writer.Position());
        for (const auto& treeNode : treeNodes)
        {
            TreeNodePack& value = RequireReference(treeNode);
            writer.WriteVector3(value.MinBounds);
            writer.WriteVector3(value.MaxBounds);
            writer.Write(static_cast<std::uint16_t>(value.LeftIndex));
            writer.Write(static_cast<std::uint16_t>(value.RightIndex));
        }
        const std::int32_t portalOffset = ToIntCount(writer.Position());
        for (const CollisionPortalPack& portal : portalPacks)
        {
            Portal& value = RequireReference(portal.PortalValue);
            writer.WriteString(value.Name, 40);
            writer.WriteString(value.NodeName1, 16);
            writer.WriteString(value.NodeName2, 16);
            writer.WriteVector4(value.Plane);
            writer.Write(portal.PointIndexCount);
            writer.Write(portal.PointStartIndex);
            writer.Write(value.Unknown00);
            writer.Write(value.Unknown01);
            writer.Write(padShort);
        }

        writer.Position(0);
        writer.Write(ToIntCount(points.size()));
        writer.Write(pointOffset);
        writer.Write(ToIntCount(planes.size()));
        writer.Write(planeOffset);
        writer.Write(ToIntCount(vectors.size()));
        writer.Write(vectorOffset);
        writer.Write(static_cast<std::uint16_t>(
            UncheckedAdd(ToIntCount(data.size()), ToIntCount(RequireReference(portals).size()))));
        writer.Write(padShort);
        writer.Write(dataOffset);
        writer.Write(ToIntCount(dataIdxs.size()));
        writer.Write(dataIndexOffset);
        writer.Write(ToIntCount(entries.size()));
        writer.Write(entryOffset);
        writer.Write(treeNodeIndexCount);
        writer.Write(treeNodeIndexOffset);
        writer.Write(ToIntCount(treeNodes.size()));
        writer.Write(treeNodeOffset);
        writer.Write(ToIntCount(RequireReference(portals).size()));
        writer.Write(portalOffset);
        return writer.ToArray();
    }

    [[nodiscard]] std::shared_ptr<MphCollisionInfo> GetCollision(
        const std::vector<std::uint8_t>& bytes)
    {
        const CollisionHeader header = MphRead::Read::ReadStruct<CollisionHeader>(Span(bytes));
        return Collision::ReadMphCollision(header, Span(bytes), -1);
    }

    void CompareCollision(
        const MphCollisionInfo& info,
        const std::vector<std::uint8_t>& bytes,
        const std::vector<std::uint8_t>& file)
    {
        const std::shared_ptr<MphCollisionInfo> packPointer = GetCollision(bytes);
        MphCollisionInfo& pack = RequireReference(packPointer);
        REPACK_COLLISION_DEBUG_ASSERT(pack.Header.Type == info.Header.Type);
        REPACK_COLLISION_DEBUG_ASSERT(pack.Header.PointCount == info.Header.PointCount);
        REPACK_COLLISION_DEBUG_ASSERT(pack.Header.PointOffset == info.Header.PointOffset);
        REPACK_COLLISION_DEBUG_ASSERT(pack.Header.PlaneCount == info.Header.PlaneCount);
        REPACK_COLLISION_DEBUG_ASSERT(pack.Header.PlaneOffset == info.Header.PlaneOffset);
        REPACK_COLLISION_DEBUG_ASSERT(pack.Header.PointIndexCount == info.Header.PointIndexCount);
        REPACK_COLLISION_DEBUG_ASSERT(pack.Header.PointIndexOffset == info.Header.PointIndexOffset);
        REPACK_COLLISION_DEBUG_ASSERT(pack.Header.DataCount == info.Header.DataCount);
        REPACK_COLLISION_DEBUG_ASSERT(pack.Header.DataOffset == info.Header.DataOffset);
        REPACK_COLLISION_DEBUG_ASSERT(pack.Header.DataIndexCount == info.Header.DataIndexCount);
        REPACK_COLLISION_DEBUG_ASSERT(pack.Header.DataIndexOffset == info.Header.DataIndexOffset);
        REPACK_COLLISION_DEBUG_ASSERT(pack.Header.PartsX == info.Header.PartsX);
        REPACK_COLLISION_DEBUG_ASSERT(pack.Header.PartsY == info.Header.PartsY);
        REPACK_COLLISION_DEBUG_ASSERT(pack.Header.PartsZ == info.Header.PartsZ);
        REPACK_COLLISION_DEBUG_ASSERT(pack.Header.MinPosition.X.Value == info.Header.MinPosition.X.Value);
        REPACK_COLLISION_DEBUG_ASSERT(pack.Header.MinPosition.Y.Value == info.Header.MinPosition.Y.Value);
        REPACK_COLLISION_DEBUG_ASSERT(pack.Header.MinPosition.Z.Value == info.Header.MinPosition.Z.Value);
        REPACK_COLLISION_DEBUG_ASSERT(pack.Header.EntryCount == info.Header.EntryCount);
        REPACK_COLLISION_DEBUG_ASSERT(pack.Header.EntryOffset == info.Header.EntryOffset);
        REPACK_COLLISION_DEBUG_ASSERT(pack.Header.PortalCount == info.Header.PortalCount);
        REPACK_COLLISION_DEBUG_ASSERT(pack.Header.PortalOffset == info.Header.PortalOffset);

        const auto& packPoints = RequireReference(pack.Points);
#if defined(DEBUG)
        REPACK_COLLISION_DEBUG_ASSERT(
            packPoints.size() == RequireReference(info.Points).size());
#endif
        for (std::int32_t i = 0; i < ToIntCount(packPoints.size()); ++i)
        {
            const Vector3 left = ManagedListAt(packPoints, i);
            const Vector3 right = ManagedListAt(RequireReference(info.Points), i);
            REPACK_COLLISION_DEBUG_ASSERT(left.X == right.X);
            REPACK_COLLISION_DEBUG_ASSERT(left.Y == right.Y);
            REPACK_COLLISION_DEBUG_ASSERT(left.Z == right.Z);
        }
        const auto& packPlanes = RequireReference(pack.Planes);
#if defined(DEBUG)
        REPACK_COLLISION_DEBUG_ASSERT(
            packPlanes.size() == RequireReference(info.Planes).size());
#endif
        for (std::int32_t i = 0; i < ToIntCount(packPlanes.size()); ++i)
        {
            const Vector4 left = ManagedListAt(packPlanes, i);
            const Vector4 right = ManagedListAt(RequireReference(info.Planes), i);
            REPACK_COLLISION_DEBUG_ASSERT(left.X == right.X);
            REPACK_COLLISION_DEBUG_ASSERT(left.Y == right.Y);
            REPACK_COLLISION_DEBUG_ASSERT(left.Z == right.Z);
            REPACK_COLLISION_DEBUG_ASSERT(left.W == right.W);
        }
        REPACK_COLLISION_DEBUG_ASSERT(
            RequireReference(pack.PointIndices).size() == RequireReference(info.PointIndices).size());
        REPACK_COLLISION_DEBUG_ASSERT(RequireReference(pack.PointIndices) == RequireReference(info.PointIndices));

        const auto& packData = RequireReference(pack.Data);
#if defined(DEBUG)
        const auto& infoData = RequireReference(info.Data);
        REPACK_COLLISION_DEBUG_ASSERT(packData.size() == infoData.size());
#endif
        for (std::int32_t i = 0; i < ToIntCount(packData.size()); ++i)
        {
            const CollisionData& data = ManagedListAt(packData, i);
#if defined(DEBUG)
            const CollisionData& other = ManagedListAt(infoData, i);
#else
            const CollisionData& other = ManagedListAt(RequireReference(info.Data), i);
#endif
            REPACK_COLLISION_DEBUG_ASSERT(data.Counter == other.Counter);
            REPACK_COLLISION_DEBUG_ASSERT(data.PlaneIndex == other.PlaneIndex);
            REPACK_COLLISION_DEBUG_ASSERT(data.Flags == other.Flags);
            REPACK_COLLISION_DEBUG_ASSERT(data.LayerMask == other.LayerMask);
            REPACK_COLLISION_DEBUG_ASSERT(data.PaddingA == other.PaddingA);
            REPACK_COLLISION_DEBUG_ASSERT(data.PointIndexCount == other.PointIndexCount);
            REPACK_COLLISION_DEBUG_ASSERT(data.PointStartIndex == other.PointStartIndex);
        }
        REPACK_COLLISION_DEBUG_ASSERT(
            RequireReference(pack.DataIndices).size() == RequireReference(info.DataIndices).size());
        REPACK_COLLISION_DEBUG_ASSERT(RequireReference(pack.DataIndices) == RequireReference(info.DataIndices));

        const auto& packEntries = RequireReference(pack.Entries);
#if defined(DEBUG)
        REPACK_COLLISION_DEBUG_ASSERT(
            packEntries.size() == RequireReference(info.Entries).size());
#endif
        for (std::int32_t i = 0; i < ToIntCount(packEntries.size()); ++i)
        {
            const CollisionEntry& left = ManagedListAt(packEntries, i);
            const CollisionEntry& right = ManagedListAt(RequireReference(info.Entries), i);
            REPACK_COLLISION_DEBUG_ASSERT(left.DataCount == right.DataCount);
            REPACK_COLLISION_DEBUG_ASSERT(left.DataStartIndex == right.DataStartIndex);
        }

        const auto portals = MphRead::Read::DoOffsets<RawCollisionPortal>(
            Span(bytes), pack.Header.PortalOffset, pack.Header.PortalCount);
        const auto otherPortals = MphRead::Read::DoOffsets<RawCollisionPortal>(
            Span(bytes), info.Header.PortalOffset, info.Header.PortalCount);
        const auto& portalValues = RequireReference(portals);
        for (std::int32_t i = 0; i < ToIntCount(portalValues.size()); ++i)
        {
            const RawCollisionPortal& portal = ManagedListAt(portalValues, i);
            const RawCollisionPortal& other = ManagedListAt(RequireReference(otherPortals), i);
            REPACK_COLLISION_DEBUG_ASSERT(portal.Name == other.Name);
            REPACK_COLLISION_DEBUG_ASSERT(portal.NodeName1 == other.NodeName1);
            REPACK_COLLISION_DEBUG_ASSERT(portal.NodeName2 == other.NodeName2);
            REPACK_COLLISION_DEBUG_ASSERT(portal.Point1.X.Value == other.Point1.X.Value);
            REPACK_COLLISION_DEBUG_ASSERT(portal.Point1.Y.Value == other.Point1.Y.Value);
            REPACK_COLLISION_DEBUG_ASSERT(portal.Point1.Z.Value == other.Point1.Z.Value);
            REPACK_COLLISION_DEBUG_ASSERT(portal.Point2.X.Value == other.Point2.X.Value);
            REPACK_COLLISION_DEBUG_ASSERT(portal.Point2.Y.Value == other.Point2.Y.Value);
            REPACK_COLLISION_DEBUG_ASSERT(portal.Point2.Z.Value == other.Point2.Z.Value);
            REPACK_COLLISION_DEBUG_ASSERT(portal.Point3.X.Value == other.Point3.X.Value);
            REPACK_COLLISION_DEBUG_ASSERT(portal.Point3.Y.Value == other.Point3.Y.Value);
            REPACK_COLLISION_DEBUG_ASSERT(portal.Point3.Z.Value == other.Point3.Z.Value);
            REPACK_COLLISION_DEBUG_ASSERT(portal.Point4.X.Value == other.Point4.X.Value);
            REPACK_COLLISION_DEBUG_ASSERT(portal.Point4.Y.Value == other.Point4.Y.Value);
            REPACK_COLLISION_DEBUG_ASSERT(portal.Point4.Z.Value == other.Point4.Z.Value);
            REPACK_COLLISION_DEBUG_ASSERT(portal.Plane1.X.Value == other.Plane1.X.Value);
            REPACK_COLLISION_DEBUG_ASSERT(portal.Plane1.Y.Value == other.Plane1.Y.Value);
            REPACK_COLLISION_DEBUG_ASSERT(portal.Plane1.Z.Value == other.Plane1.Z.Value);
            REPACK_COLLISION_DEBUG_ASSERT(portal.Plane1.W.Value == other.Plane1.W.Value);
            REPACK_COLLISION_DEBUG_ASSERT(portal.Plane2.X.Value == other.Plane2.X.Value);
            REPACK_COLLISION_DEBUG_ASSERT(portal.Plane2.Y.Value == other.Plane2.Y.Value);
            REPACK_COLLISION_DEBUG_ASSERT(portal.Plane2.Z.Value == other.Plane2.Z.Value);
            REPACK_COLLISION_DEBUG_ASSERT(portal.Plane2.W.Value == other.Plane2.W.Value);
            REPACK_COLLISION_DEBUG_ASSERT(portal.Plane3.X.Value == other.Plane3.X.Value);
            REPACK_COLLISION_DEBUG_ASSERT(portal.Plane3.Y.Value == other.Plane3.Y.Value);
            REPACK_COLLISION_DEBUG_ASSERT(portal.Plane3.Z.Value == other.Plane3.Z.Value);
            REPACK_COLLISION_DEBUG_ASSERT(portal.Plane3.W.Value == other.Plane3.W.Value);
            REPACK_COLLISION_DEBUG_ASSERT(portal.Plane4.X.Value == other.Plane4.X.Value);
            REPACK_COLLISION_DEBUG_ASSERT(portal.Plane4.Y.Value == other.Plane4.Y.Value);
            REPACK_COLLISION_DEBUG_ASSERT(portal.Plane4.Z.Value == other.Plane4.Z.Value);
            REPACK_COLLISION_DEBUG_ASSERT(portal.Plane4.W.Value == other.Plane4.W.Value);
            REPACK_COLLISION_DEBUG_ASSERT(portal.Plane.X.Value == other.Plane.X.Value);
            REPACK_COLLISION_DEBUG_ASSERT(portal.Plane.Y.Value == other.Plane.Y.Value);
            REPACK_COLLISION_DEBUG_ASSERT(portal.Plane.Z.Value == other.Plane.Z.Value);
            REPACK_COLLISION_DEBUG_ASSERT(portal.Plane.W.Value == other.Plane.W.Value);
            REPACK_COLLISION_DEBUG_ASSERT(portal.Flags == other.Flags);
            REPACK_COLLISION_DEBUG_ASSERT(portal.LayerMask == other.LayerMask);
            REPACK_COLLISION_DEBUG_ASSERT(portal.PointCount == other.PointCount);
            REPACK_COLLISION_DEBUG_ASSERT(portal.UnusedDE == other.UnusedDE);
            REPACK_COLLISION_DEBUG_ASSERT(portal.UnusedDF == other.UnusedDF);
        }
        REPACK_COLLISION_DEBUG_ASSERT(bytes.size() == file.size());
        REPACK_COLLISION_DEBUG_ASSERT(bytes == file);
    }

    void Nop()
    {
    }
}

namespace MphRead::Utility
{
    bool CollisionDataEditor::Damaging() const noexcept
    {
        return Check(CollisionFlags::Damaging);
    }

    void CollisionDataEditor::Damaging(bool value) noexcept
    {
        Update(CollisionFlags::Damaging, value);
    }

    bool CollisionDataEditor::Reflect() const noexcept
    {
        return Check(CollisionFlags::ReflectBeams);
    }

    void CollisionDataEditor::Reflect(bool value) noexcept
    {
        Update(CollisionFlags::ReflectBeams, value);
    }

    bool CollisionDataEditor::Players() const noexcept
    {
        return Check(CollisionFlags::IgnorePlayers);
    }

    void CollisionDataEditor::Players(bool value) noexcept
    {
        Update(CollisionFlags::IgnorePlayers, !value);
    }

    bool CollisionDataEditor::Beams() const noexcept
    {
        return Check(CollisionFlags::IgnoreBeams);
    }

    void CollisionDataEditor::Beams(bool value) noexcept
    {
        Update(CollisionFlags::IgnoreBeams, !value);
    }

    bool CollisionDataEditor::Scan() const noexcept
    {
        return Check(CollisionFlags::IgnoreScan);
    }

    void CollisionDataEditor::Scan(bool value) noexcept
    {
        Update(CollisionFlags::IgnoreScan, !value);
    }

    std::int32_t CollisionDataEditor::Slipperiness() const noexcept
    {
        return (static_cast<std::uint16_t>(Flags) & 0x18U) >> 3;
    }

    void CollisionDataEditor::Slipperiness(std::int32_t value)
    {
        if (value < 0 || value > 3)
        {
            throw ProgramException(
                "Invalid slipperiness value " + std::to_string(value) + ".");
        }
        Flags = static_cast<CollisionFlags>(
            (static_cast<std::uint16_t>(Flags) & 0xFFE7U)
            | static_cast<std::uint16_t>(value << 3));
    }

    MphRead::Terrain CollisionDataEditor::Terrain() const noexcept
    {
        return static_cast<MphRead::Terrain>(
            (static_cast<std::uint16_t>(Flags) & 0x01E0U) >> 5);
    }

    void CollisionDataEditor::Terrain(MphRead::Terrain value)
    {
        const std::uint8_t raw = static_cast<std::uint8_t>(value);
        if (raw > static_cast<std::uint8_t>(MphRead::Terrain::All))
        {
            throw ProgramException(
                "Invalid terrain type " + std::to_string(static_cast<unsigned int>(raw)) + ".");
        }
        Flags = static_cast<CollisionFlags>(
            (static_cast<std::uint16_t>(Flags) & 0xFE1FU)
            | static_cast<std::uint16_t>(static_cast<std::uint16_t>(raw) << 5));
    }

    bool CollisionDataEditor::Check(CollisionFlags flag) const noexcept
    {
        return MphRead::TypeExtensions::TestFlag(Flags, flag);
    }

    void CollisionDataEditor::Update(CollisionFlags flag, bool value) noexcept
    {
        if (value)
        {
            Flags |= flag;
        }
        else
        {
            Flags &= ~flag;
        }
    }

    std::vector<std::uint8_t> RepackCollision::RepackMphRoom(const std::string& room)
    {
        RoomMetadata& meta = GetRoomMetadata(room);
        const std::shared_ptr<CollisionInstance> collision
            = Collision::GetCollision(std::addressof(meta), -1);
        const auto editors = GetEditors(collision);
        CollisionInstance& collisionValue = RequireReference(collision);
        CollisionInfo& info = RequireReference(collisionValue.Info);
        return RepackMphCollision(editors, info.Portals);
    }

    std::vector<std::uint8_t> RepackCollision::RepackFhRoom(
        const std::string& room, RepackFilter filter)
    {
        RoomMetadata& meta = GetRoomMetadata(room);
        std::int32_t roomLayerMask = -1;
        if (filter != RepackFilter::All)
        {
            roomLayerMask = filter == RepackFilter::Multiplayer
                ? SceneSetup::GetNodeLayer(GameMode::Battle, 0, 2)
                : SceneSetup::GetNodeLayer(GameMode::SinglePlayer, meta.NodeLayer, 1);
        }
        const std::shared_ptr<CollisionInstance> collision
            = Collision::GetCollision(std::addressof(meta), roomLayerMask);
        const auto editors = GetEditors(collision);
        CollisionInstance& collisionValue = RequireReference(collision);
        CollisionInfo& info = RequireReference(collisionValue.Info);
        return RepackFhCollision(editors, info.Portals);
    }

    void RepackCollision::TestCollision(std::optional<std::string> room)
    {
        std::vector<std::pair<std::shared_ptr<CollisionInstance>, std::string>> allCollision;
        if (!room.has_value())
        {
            for (const auto& pair : Metadata::RoomMetadata)
            {
                RoomMetadata& meta = RequireReference(pair.second);
                if (!meta.FirstHunt && !meta.Hybrid)
                {
                    bool alreadyAdded = false;
                    for (const auto& collision : allCollision)
                    {
                        if (collision.second == meta.CollisionPath)
                        {
                            alreadyAdded = true;
                            break;
                        }
                    }
                    if (!alreadyAdded)
                    {
                        allCollision.emplace_back(
                            Collision::GetCollision(std::addressof(meta), -1), meta.CollisionPath);
                    }
                }
            }
            for (const auto& pair : Metadata::ModelMetadata)
            {
                const ModelMetadata& meta = pair.second;
                if (meta.CollisionPath.has_value() && !meta.FirstHunt)
                {
                    allCollision.emplace_back(
                        Collision::GetCollision(std::addressof(meta)), *meta.CollisionPath);
                    if (meta.ExtraCollisionPath.has_value())
                    {
                        allCollision.emplace_back(
                            Collision::GetCollision(std::addressof(meta), true),
                            *meta.ExtraCollisionPath);
                    }
                }
            }
        }
        else
        {
            RoomMetadata& meta = GetRoomMetadata(*room);
            allCollision.emplace_back(
                Collision::GetCollision(std::addressof(meta), -1), meta.CollisionPath);
        }

        for (const auto& item : allCollision)
        {
            const std::shared_ptr<CollisionInstance>& collision = item.first;
            const std::string& path = item.second;
            const auto editors = GetEditors(collision);
            CollisionInstance& collisionValue = RequireReference(collision);
            CollisionInfo& info = RequireReference(collisionValue.Info);
            const std::vector<std::uint8_t> bytes
                = RepackMphCollision(editors, info.Portals);
            const std::string outPath = Paths::Combine(
                Paths::Export(), "_pack",
                "out_" + ::MphRead::NativeRuntime::PathGetFileName(path));
            FileWriteAllBytes(outPath, bytes);
        }
        Nop();
    }
}

namespace MphRead::Utility
{
    std::vector<std::uint8_t> RepackCollision::RepackMphCollisionFrom(
        std::span<CollisionDataEditor* const> data,
        std::span<Formats::Collision::Portal* const> portals)
    {
        // The caller owns both lists; these references do not.
        std::vector<std::shared_ptr<CollisionDataEditor>> editors;
        editors.reserve(data.size());
        for (CollisionDataEditor* const editor : data)
        {
            editors.push_back(
                std::shared_ptr<CollisionDataEditor>(editor, [](CollisionDataEditor*) {}));
        }
        auto portalList
            = std::make_shared<std::vector<std::shared_ptr<Formats::Collision::Portal>>>();
        portalList->reserve(portals.size());
        for (Formats::Collision::Portal* const portal : portals)
        {
            portalList->push_back(std::shared_ptr<Formats::Collision::Portal>(
                portal, [](Formats::Collision::Portal*) {}));
        }
        return RepackMphCollision(editors, portalList);
    }
}
