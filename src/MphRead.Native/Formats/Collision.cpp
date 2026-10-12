#include "Collision.hpp"

#include "../NativeRuntime/System/IO.hpp"

#include "../Metadata/Metadata.hpp"

#include "../Read.hpp"
#include "../Scene.hpp"
#include "Types.hpp"
#include "../NativeRuntime/System/Managed.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <ios>
#include <cstdlib>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(DEBUG)
#define MPHREAD_COLLISION_DEBUG_ASSERT(condition) \
    do \
    { \
        if (!(condition)) \
        { \
            std::abort(); \
        } \
    } while (false)
#else
#define MPHREAD_COLLISION_DEBUG_ASSERT(condition) ((void)0)
#endif

using ::MphRead::NativeRuntime::ConvertToInt32Net9;
using ::MphRead::NativeRuntime::HasFlag;
using ::MphRead::NativeRuntime::ManagedAt;
using ::MphRead::NativeRuntime::RequireReference;
using ::MphRead::NativeRuntime::UncheckedAdd;
using ::MphRead::NativeRuntime::UncheckedMultiply;
using ::MphRead::NativeRuntime::UncheckedSubtract;

namespace
{
    // File.ReadAllBytes: the whole file, or an exception when it cannot be read.
}

namespace MphRead::Formats::Collision
{
    namespace
    {
        template <typename T>
        T& ReassignReadonlyValue(
            T* self, const T& other) noexcept
        {
            if (self != &other)
            {
                std::destroy_at(self);
                std::construct_at(self, other);
            }
            return *self;
        }

        template <typename T>
        std::size_t Count(
            const std::shared_ptr<const std::vector<T>>& values)
        {
            if (!values)
            {
                throw System::NullReferenceException();
            }
            return values->size();
        }

        std::shared_ptr<
            const std::vector<OpenTK::Mathematics::Vector3>>
        ConvertPoints(
            const std::shared_ptr<
                const std::vector<MphRead::Vector3Fx>>& values)
        {
            if (!values)
            {
                throw System::ArgumentNullException("source");
            }

            auto result = std::make_shared<
                std::vector<OpenTK::Mathematics::Vector3>>();

            result->reserve(values->size());

            for (const MphRead::Vector3Fx& value : *values)
            {
                result->push_back(value.ToFloatVector());
            }

            return result;
        }

        std::shared_ptr<
            const std::vector<OpenTK::Mathematics::Vector4>>
        ConvertPlanes(
            const std::shared_ptr<
                const std::vector<MphRead::Vector4Fx>>& values)
        {
            if (!values)
            {
                throw System::ArgumentNullException("source");
            }

            auto result = std::make_shared<
                std::vector<OpenTK::Mathematics::Vector4>>();

            result->reserve(values->size());

            for (const MphRead::Vector4Fx& value : *values)
            {
                result->push_back(value.ToFloatVector());
            }

            return result;
        }

        OpenTK::Mathematics::Vector3 Average(
            const std::shared_ptr<
                const std::vector<
                    OpenTK::Mathematics::Vector3>>& points)
        {
            if (!points)
            {
                throw System::ArgumentNullException("source");
            }

            float x = 0.0F;
            float y = 0.0F;
            float z = 0.0F;

            for (const OpenTK::Mathematics::Vector3& point : *points)
            {
                x += point.X;
                y += point.Y;
                z += point.Z;
            }

            const float count
                = static_cast<float>(points->size());

            return OpenTK::Mathematics::Vector3(
                x / count,
                y / count,
                z / count);
        }

        std::int32_t ManagedDivide(
            std::int32_t numerator,
            std::int32_t denominator)
        {
            if (denominator == 0)
            {
                // Exact DivideByZeroException translation belongs to the
                // common managed-exception prerequisite.
                throw std::domain_error(
                    "Attempted to divide by zero.");
            }

            if (numerator
                    == std::numeric_limits<std::int32_t>::min()
                && denominator == -1)
            {
                throw System::OverflowException();
            }

            return numerator / denominator;
        }

        struct CollisionCaches
        {
            std::unordered_map<
                std::string,
                std::shared_ptr<CollisionInfo>> Mph;

            std::unordered_map<
                std::string,
                std::shared_ptr<CollisionInfo>> Fh;
        };

        CollisionCaches& Caches()
        {
            static CollisionCaches caches;
            return caches;
        }
    }

    CollisionHeader& CollisionHeader::operator=(
        const CollisionHeader& other) noexcept
    {
        return ReassignReadonlyValue(this, other);
    }

    CollisionData& CollisionData::operator=(
        const CollisionData& other) noexcept
    {
        return ReassignReadonlyValue(this, other);
    }

    CollisionEntry& CollisionEntry::operator=(
        const CollisionEntry& other) noexcept
    {
        return ReassignReadonlyValue(this, other);
    }

    RawCollisionPortal& RawCollisionPortal::operator=(
        const RawCollisionPortal& other) noexcept
    {
        return ReassignReadonlyValue(this, other);
    }

    FhCollisionPortal& FhCollisionPortal::operator=(
        const FhCollisionPortal& other) noexcept
    {
        return ReassignReadonlyValue(this, other);
    }

    FhCollisionHeader& FhCollisionHeader::operator=(
        const FhCollisionHeader& other) noexcept
    {
        return ReassignReadonlyValue(this, other);
    }

    FhCollisionData& FhCollisionData::operator=(
        const FhCollisionData& other) noexcept
    {
        return ReassignReadonlyValue(this, other);
    }

    FhCollisionVector& FhCollisionVector::operator=(
        const FhCollisionVector& other) noexcept
    {
        return ReassignReadonlyValue(this, other);
    }

    FhCollisionEntry& FhCollisionEntry::operator=(
        const FhCollisionEntry& other) noexcept
    {
        return ReassignReadonlyValue(this, other);
    }

    FhCollisionTreeNode&
    FhCollisionTreeNode::operator=(
        const FhCollisionTreeNode& other) noexcept
    {
        return ReassignReadonlyValue(this, other);
    }

    EntityCollision::EntityCollision(
        std::shared_ptr<CollisionInstance> collision,
        MphRead::Entities::EntityBase* entity)
        : Entity(entity),
          Collision(std::move(collision)),
          DrawPoints(
              std::make_shared<
                  std::vector<
                      OpenTK::Mathematics::Vector3>>())
    {
    }

    std::int32_t CollisionData::Slipperiness() const noexcept
    {
        return (
            static_cast<std::uint16_t>(Flags)
            & 0x0018U
        ) >> 3;
    }

    MphRead::Terrain
    CollisionData::Terrain() const noexcept
    {
        return static_cast<MphRead::Terrain>(
            (
                static_cast<std::uint16_t>(Flags)
                & 0x01E0U
            ) >> 5);
    }

    bool CollisionData::IgnorePlayers() const noexcept
    {
        return HasFlag(
            Flags,
            CollisionFlags::IgnorePlayers);
    }

    bool CollisionData::IgnoreBeams() const noexcept
    {
        return HasFlag(
            Flags,
            CollisionFlags::IgnoreBeams);
    }

    std::int32_t CollisionData::Axis() const noexcept
    {
        return LayerMask & 3;
    }

    struct Portal::Initializer
    {
        std::string Name;
        std::string NodeName1;
        std::string NodeName2;

        std::uint16_t LayerMask = 0;
        bool IsForceField = false;

        std::shared_ptr<
            const std::vector<
                OpenTK::Mathematics::Vector3>> Points;

        std::shared_ptr<
            const std::vector<
                OpenTK::Mathematics::Vector4>> Planes;

        OpenTK::Mathematics::Vector4 Plane{};
        OpenTK::Mathematics::Vector3 Position{};

        std::uint16_t Flags = 0;
        std::uint8_t Unknown00 = 0;
        std::uint8_t Unknown01 = 0;
    };

    Portal::Initializer Portal::Initialize(
        RawCollisionPortal raw)
    {
        MPHREAD_COLLISION_DEBUG_ASSERT(
            raw.PointCount == 4);

        Initializer result{};

        result.Name = ::MphRead::MarshalExtensions::MarshalUtf8(raw.Name);
        result.NodeName1 = ::MphRead::MarshalExtensions::MarshalUtf8(raw.NodeName1);
        result.NodeName2 = ::MphRead::MarshalExtensions::MarshalUtf8(raw.NodeName2);

        result.LayerMask = raw.LayerMask;
        result.IsForceField
            = result.Name.starts_with("pmag");

        auto points = std::make_shared<
            std::vector<
                OpenTK::Mathematics::Vector3>>();

        points->reserve(4);
        points->push_back(raw.Point1.ToFloatVector());
        points->push_back(raw.Point2.ToFloatVector());
        points->push_back(raw.Point3.ToFloatVector());
        points->push_back(raw.Point4.ToFloatVector());

        result.Points = points;
        result.Position = Average(result.Points);

        auto planes = std::make_shared<
            std::vector<
                OpenTK::Mathematics::Vector4>>();

        planes->reserve(4);
        planes->push_back(raw.Plane1.ToFloatVector());
        planes->push_back(raw.Plane2.ToFloatVector());
        planes->push_back(raw.Plane3.ToFloatVector());
        planes->push_back(raw.Plane4.ToFloatVector());

        result.Planes = planes;
        result.Plane = raw.Plane.ToFloatVector();
        result.Flags = raw.Flags;
        result.Unknown00 = raw.UnusedDE;
        result.Unknown01 = raw.UnusedDF;

        return result;
    }

    Portal::Initializer Portal::Initialize(
        FhCollisionPortal raw,
        const std::shared_ptr<
            const std::vector<FhCollisionVector>>& rawVectors,
        const std::shared_ptr<
            const std::vector<MphRead::Vector3Fx>>& rawPoints,
        const std::shared_ptr<
            const std::vector<MphRead::Vector4Fx>>& rawPlanes)
    {
        Initializer result{};

        result.Name = ::MphRead::MarshalExtensions::MarshalUtf8(raw.Name);
        result.NodeName1 = ::MphRead::MarshalExtensions::MarshalUtf8(raw.NodeName1);
        result.NodeName2 = ::MphRead::MarshalExtensions::MarshalUtf8(raw.NodeName2);
        result.LayerMask = 4;

        auto points = std::make_shared<
            std::vector<
                OpenTK::Mathematics::Vector3>>();

        auto planes = std::make_shared<
            std::vector<
                OpenTK::Mathematics::Vector4>>();

        points->reserve(raw.VectorCount);
        planes->reserve(raw.VectorCount);

        for (std::int32_t i = 0;
            i < raw.VectorCount;
            ++i)
        {
            const FhCollisionVector& vector = ManagedAt(
                rawVectors,
                static_cast<std::size_t>(
                    raw.VectorStartIndex) + i);

            points->push_back(
                ManagedAt(
                    rawPoints,
                    vector.Point2Index)
                    .ToFloatVector());

            planes->push_back(
                ManagedAt(
                    rawPlanes,
                    vector.PlaneIndex)
                    .ToFloatVector());
        }

        result.Points = points;
        result.Position = Average(result.Points);
        result.Planes = planes;
        result.Plane = raw.Plane.ToFloatVector();

        result.Unknown00 = raw.Field5C;
        result.Unknown01 = raw.Field5D;

        return result;
    }

    Portal::Portal(
        Initializer initializer)
        : Name(std::move(initializer.Name)),
          NodeName1(std::move(initializer.NodeName1)),
          NodeName2(std::move(initializer.NodeName2)),
          LayerMask(initializer.LayerMask),
          IsForceField(initializer.IsForceField),
          Points(std::move(initializer.Points)),
          Planes(std::move(initializer.Planes)),
          Plane(initializer.Plane),
          Position(initializer.Position),
          Flags(initializer.Flags),
          Unknown00(initializer.Unknown00),
          Unknown01(initializer.Unknown01)
    {
    }

    Portal::Portal(
        RawCollisionPortal raw)
        : Portal(Initialize(raw))
    {
    }

    Portal::Portal(
        FhCollisionPortal raw,
        std::shared_ptr<
            const std::vector<FhCollisionVector>> rawVectors,
        std::shared_ptr<
            const std::vector<MphRead::Vector3Fx>> rawPoints,
        std::shared_ptr<
            const std::vector<MphRead::Vector4Fx>> rawPlanes)
        : Portal(
              Initialize(
                  raw,
                  rawVectors,
                  rawPoints,
                  rawPlanes))
    {
    }

    Portal::Portal(
        std::string nodeName1,
        std::string nodeName2,
        std::shared_ptr<
            const std::vector<
                OpenTK::Mathematics::Vector3>> points,
        std::shared_ptr<
            const std::vector<
                OpenTK::Mathematics::Vector4>> planes,
        OpenTK::Mathematics::Vector4 plane)
        : Name(
              "port_"
              + nodeName1
              + "_"
              + nodeName2),
          NodeName1(std::move(nodeName1)),
          NodeName2(std::move(nodeName2)),
          LayerMask(4),
          IsForceField(false),
          Points(std::move(points)),
          Planes(std::move(planes)),
          Plane(plane),
          Position(Average(Points)),
          Flags(1),
          Unknown00(0),
          Unknown01(0)
    {
    }

    CollisionInstance::CollisionInstance(
        std::string name,
        std::shared_ptr<CollisionInfo> info,
        bool isEntity)
        : Name(std::move(name)),
          Info(std::move(info)),
          IsEntity(isEntity)
    {
    }

    CollisionInfo::CollisionInfo(
        std::shared_ptr<
            const std::vector<MphRead::Vector3Fx>> points,
        std::shared_ptr<
            const std::vector<MphRead::Vector4Fx>> planes,
        std::shared_ptr<
            const std::vector<
                std::shared_ptr<Portal>>> portals,
        bool firstHunt)
        : FirstHunt(firstHunt),
          Points(ConvertPoints(points)),
          Planes(ConvertPlanes(planes)),
          Portals(std::move(portals))
    {
    }

    MphCollisionInfo::MphCollisionInfo(
        CollisionHeader header,
        std::shared_ptr<
            const std::vector<MphRead::Vector3Fx>> points,
        std::shared_ptr<
            const std::vector<MphRead::Vector4Fx>> planes,
        std::shared_ptr<
            const std::vector<std::uint16_t>> ptIdxs,
        std::shared_ptr<
            const std::vector<CollisionData>> data,
        std::shared_ptr<
            const std::vector<std::uint16_t>> dataIdxs,
        std::shared_ptr<
            const std::vector<CollisionEntry>> entries,
        std::shared_ptr<
            const std::vector<
                std::shared_ptr<Portal>>> portals)
        : CollisionInfo(
              std::move(points),
              std::move(planes),
              std::move(portals),
              false),
          Header(header),
          PointIndices(std::move(ptIdxs)),
          Data(std::move(data)),
          DataIndices(std::move(dataIdxs)),
          Entries(std::move(entries)),
          MinPosition(
              header.MinPosition.ToFloatVector())
    {
    }

    const std::array<
        OpenTK::Mathematics::Vector4, 12>&
    MphCollisionInfo::Colors()
    {
        static const std::array<
            OpenTK::Mathematics::Vector4, 12> colors
        {
            OpenTK::Mathematics::Vector4(
                0.69F, 0.69F, 0.69F, 1.0F),
            OpenTK::Mathematics::Vector4(
                1.0F, 0.612F, 0.153F, 1.0F),
            OpenTK::Mathematics::Vector4(
                0.0F, 1.0F, 0.0F, 1.0F),
            OpenTK::Mathematics::Vector4(
                0.0F, 0.0F, 0.858F, 1.0F),
            OpenTK::Mathematics::Vector4(
                0.141F, 1.0F, 1.0F, 1.0F),
            OpenTK::Mathematics::Vector4(
                1.0F, 1.0F, 1.0F, 1.0F),
            OpenTK::Mathematics::Vector4(
                0.964F, 1.0F, 0.058F, 1.0F),
            OpenTK::Mathematics::Vector4(
                0.505F, 0.364F, 0.211F, 1.0F),
            OpenTK::Mathematics::Vector4(
                0.984F, 0.701F, 0.576F, 1.0F),
            OpenTK::Mathematics::Vector4(
                0.988F, 0.463F, 0.824F, 1.0F),
            OpenTK::Mathematics::Vector4(
                0.615F, 0.0F, 0.909F, 1.0F),
            OpenTK::Mathematics::Vector4(
                0.85F, 0.85F, 0.85F, 1.0F)
        };

        return colors;
    }

    void MphCollisionInfo::GetDrawInfo(
        std::shared_ptr<
            const std::vector<
                OpenTK::Mathematics::Vector3>> points,
        OpenTK::Mathematics::Vector3 translation,
        MphRead::EntityType entityType,
        MphRead::Scene* scenePtr)
    {
        MphRead::Scene& scene
            = RequireReference(scenePtr);

        const std::int32_t polygonId
            = scene.GetNextPolygonId();

        if (!Data)
        {
            throw System::NullReferenceException();
        }

        for (std::size_t i = 0;
            i < Data->size();
            ++i)
        {
            const CollisionData& data
                = (*Data)[i];

            if (scene.ColTerDisplay()
                    != MphRead::Terrain::All
                && scene.ColTerDisplay()
                    != data.Terrain())
            {
                continue;
            }

            if ((scene.ColTypeDisplay()
                        == CollisionType::Player
                    && data.IgnorePlayers())
                || (scene.ColTypeDisplay()
                        == CollisionType::Beam
                    && data.IgnoreBeams())
                || (scene.ColTypeDisplay()
                        == CollisionType::Both
                    && (data.IgnorePlayers()
                        || data.IgnoreBeams())))
            {
                continue;
            }

            OpenTK::Mathematics::Vector4 color{};

            if (scene.ColDisplayColor()
                == CollisionColor::Entity)
            {
                if (entityType
                    == MphRead::EntityType::Platform)
                {
                    color
                        = OpenTK::Mathematics::Vector4(
                            0.109F,
                            0.768F,
                            0.850F,
                            1.0F);
                }
                else if (entityType
                    == MphRead::EntityType::Object)
                {
                    color
                        = OpenTK::Mathematics::Vector4(
                            0.952F,
                            0.105F,
                            0.635F,
                            1.0F);
                }
                else
                {
                    color
                        = OpenTK::Mathematics::Vector4(
                            0.952F,
                            0.694F,
                            0.105F,
                            1.0F);
                }
            }
            else if (scene.ColDisplayColor()
                == CollisionColor::Terrain)
            {
                color = Colors().at(
                    static_cast<std::size_t>(
                        data.Terrain()));
            }
            else if (scene.ColDisplayColor()
                == CollisionColor::Type)
            {
                if (data.IgnoreBeams())
                {
                    color
                        = OpenTK::Mathematics::Vector4(
                            0.956F,
                            0.933F,
                            0.203F,
                            1.0F);
                }
                else if (data.IgnorePlayers())
                {
                    color
                        = OpenTK::Mathematics::Vector4(
                            0.250F,
                            0.807F,
                            0.250F,
                            1.0F);
                }
                else
                {
                    color
                        = OpenTK::Mathematics::Vector4(
                            0.807F,
                            0.250F,
                            0.776F,
                            1.0F);
                }
            }
            else
            {
                color
                    = OpenTK::Mathematics::Vector4(
                        1.0F,
                        0.0F,
                        0.0F,
                        1.0F);
            }

            color.W = scene.ColDisplayAlpha();

            MPHREAD_COLLISION_DEBUG_ASSERT(
                data.PointIndexCount >= 3
                && data.PointIndexCount <= 10);

            auto verts = MphRead::NativeRuntime::RentFromSharedArrayPool(
                data.PointIndexCount);

            for (std::int32_t j = 0;
                j < data.PointIndexCount;
                ++j)
            {
                const std::uint16_t pointIndex
                    = ManagedAt(
                        PointIndices,
                        static_cast<std::size_t>(
                            data.PointStartIndex)
                            + j);

                (*verts)[j]
                    = ManagedAt(points, pointIndex)
                    + translation;
            }

            scene.AddRenderItem(
                MphRead::CullingMode::Back,
                polygonId,
                color,
                MphRead::RenderItemType::Ngon,
                verts,
                data.PointIndexCount);
        }
    }

    OpenTK::Mathematics::Vector3i
    MphCollisionInfo::PartIndexFromEntry(
        std::int32_t index) const
    {
        const std::int32_t x
            = Header.PartsX;

        const std::int32_t xz
            = UncheckedMultiply(
                x,
                Header.PartsZ);

        const std::int32_t yInc
            = ManagedDivide(
                index,
                xz);

        const std::int32_t afterY
            = UncheckedSubtract(
                index,
                UncheckedMultiply(
                    yInc,
                    xz));

        const std::int32_t zInc
            = ManagedDivide(
                afterY,
                x);

        const std::int32_t xInc
            = UncheckedSubtract(
                afterY,
                UncheckedMultiply(
                    zInc,
                    x));

        return OpenTK::Mathematics::Vector3i(
            xInc,
            yInc,
            zInc);
    }

    std::int32_t
    MphCollisionInfo::EntryIndexFromPoint(
        OpenTK::Mathematics::Vector3 point) const
    {
        if (point.X < MinPosition.X
            || point.Y < MinPosition.Y
            || point.Z < MinPosition.Z)
        {
            return -1;
        }

        const std::int32_t xInc
            = ConvertToInt32Net9(
                (point.X - MinPosition.X)
                / 4.0F);

        const std::int32_t yInc
            = ConvertToInt32Net9(
                (point.Y - MinPosition.Y)
                / 4.0F);

        const std::int32_t zInc
            = ConvertToInt32Net9(
                (point.Z - MinPosition.Z)
                / 4.0F);

        if (xInc >= Header.PartsX
            || yInc >= Header.PartsY
            || zInc >= Header.PartsZ)
        {
            return -1;
        }

        return UncheckedAdd(
            UncheckedAdd(
                UncheckedMultiply(
                    yInc,
                    UncheckedMultiply(
                        Header.PartsX,
                        Header.PartsZ)),
                UncheckedMultiply(
                    zInc,
                    Header.PartsX)),
            xInc);
    }

    void MphCollisionInfo::GetPartition(
        OpenTK::Mathematics::Vector3 point,
        std::shared_ptr<
            const std::vector<
                OpenTK::Mathematics::Vector3>> points,
        MphRead::EntityType entityType,
        MphRead::Scene* scenePtr) const
    {
        const std::int32_t entryIndex
            = EntryIndexFromPoint(point);

        MphRead::Scene& scene
            = RequireReference(scenePtr);

        std::int32_t polygonId
            = scene.GetNextPolygonId();

        if (entryIndex < 0
            || entryIndex
                > static_cast<std::int32_t>(
                    Count(Entries)))
        {
            return;
        }

        const CollisionEntry& entry
            = ManagedAt(
                Entries,
                static_cast<std::size_t>(
                    entryIndex));

        for (std::int32_t i = 0;
            i < entry.DataCount;
            ++i)
        {
            const std::uint16_t dataIndex
                = ManagedAt(
                    DataIndices,
                    static_cast<std::size_t>(
                        entry.DataStartIndex)
                        + i);

            const CollisionData& data
                = ManagedAt(Data, dataIndex);

            if (scene.ColTerDisplay()
                    != MphRead::Terrain::All
                && scene.ColTerDisplay()
                    != data.Terrain())
            {
                continue;
            }

            if ((scene.ColTypeDisplay()
                        == CollisionType::Player
                    && data.IgnorePlayers())
                || (scene.ColTypeDisplay()
                        == CollisionType::Beam
                    && data.IgnoreBeams())
                || (scene.ColTypeDisplay()
                        == CollisionType::Both
                    && (data.IgnorePlayers()
                        || data.IgnoreBeams())))
            {
                continue;
            }

            OpenTK::Mathematics::Vector4 color{};

            if (scene.ColDisplayColor()
                == CollisionColor::Entity)
            {
                if (entityType
                    == MphRead::EntityType::Platform)
                {
                    color
                        = OpenTK::Mathematics::Vector4(
                            0.109F,
                            0.768F,
                            0.850F,
                            1.0F);
                }
                else if (entityType
                    == MphRead::EntityType::Object)
                {
                    color
                        = OpenTK::Mathematics::Vector4(
                            0.952F,
                            0.105F,
                            0.635F,
                            1.0F);
                }
                else
                {
                    color
                        = OpenTK::Mathematics::Vector4(
                            0.952F,
                            0.694F,
                            0.105F,
                            1.0F);
                }
            }
            else if (scene.ColDisplayColor()
                == CollisionColor::Terrain)
            {
                color = Colors().at(
                    static_cast<std::size_t>(
                        data.Terrain()));
            }
            else if (scene.ColDisplayColor()
                == CollisionColor::Type)
            {
                if (data.IgnoreBeams())
                {
                    color
                        = OpenTK::Mathematics::Vector4(
                            0.956F,
                            0.933F,
                            0.203F,
                            1.0F);
                }
                else if (data.IgnorePlayers())
                {
                    color
                        = OpenTK::Mathematics::Vector4(
                            0.250F,
                            0.807F,
                            0.250F,
                            1.0F);
                }
                else
                {
                    color
                        = OpenTK::Mathematics::Vector4(
                            0.807F,
                            0.250F,
                            0.776F,
                            1.0F);
                }
            }
            else
            {
                color
                    = OpenTK::Mathematics::Vector4(
                        1.0F,
                        0.0F,
                        0.0F,
                        1.0F);
            }

            color.W = scene.ColDisplayAlpha();

            MPHREAD_COLLISION_DEBUG_ASSERT(
                data.PointIndexCount >= 3
                && data.PointIndexCount <= 10);

            auto verts = MphRead::NativeRuntime::RentFromSharedArrayPool(
                data.PointIndexCount);

            for (std::int32_t j = 0;
                j < data.PointIndexCount;
                ++j)
            {
                const std::uint16_t pointIndex
                    = ManagedAt(
                        PointIndices,
                        static_cast<std::size_t>(
                            data.PointStartIndex)
                            + j);

                (*verts)[j]
                    = ManagedAt(points, pointIndex);
            }

            scene.AddRenderItem(
                MphRead::CullingMode::Back,
                polygonId,
                color,
                MphRead::RenderItemType::Ngon,
                verts,
                data.PointIndexCount);
        }

        auto bverts = MphRead::NativeRuntime::RentFromSharedArrayPool(
                8);

        OpenTK::Mathematics::Vector3 point0
            = MinPosition;

        const OpenTK::Mathematics::Vector3i partInc
            = PartIndexFromEntry(entryIndex);

        point0.X += static_cast<float>(
            UncheckedMultiply(partInc.X, 4));

        point0.Y += static_cast<float>(
            UncheckedMultiply(partInc.Y, 4));

        point0.Z += static_cast<float>(
            UncheckedMultiply(partInc.Z, 4));

        const OpenTK::Mathematics::Vector3 sideX(
            4.0F, 0.0F, 0.0F);
        const OpenTK::Mathematics::Vector3 sideY(
            0.0F, 4.0F, 0.0F);
        const OpenTK::Mathematics::Vector3 sideZ(
            0.0F, 0.0F, 4.0F);

        (*bverts)[0] = point0;
        (*bverts)[1] = point0 + sideZ;
        (*bverts)[2] = point0 + sideX;
        (*bverts)[3] = point0 + sideX + sideZ;
        (*bverts)[4] = point0 + sideY;
        (*bverts)[5] = point0 + sideY + sideZ;
        (*bverts)[6] = point0 + sideX + sideY;
        (*bverts)[7]
            = point0 + sideX + sideY + sideZ;

        polygonId = scene.GetNextPolygonId();

        const OpenTK::Mathematics::Vector4 bcolor(
            1.0F,
            0.3F,
            1.0F,
            0.5F);

        scene.AddRenderItem(
            MphRead::CullingMode::Front,
            polygonId,
            bcolor,
            MphRead::RenderItemType::Box,
            bverts,
            8);
    }

    FhCollisionInfo::FhCollisionInfo(
        FhCollisionHeader header,
        std::shared_ptr<
            const std::vector<MphRead::Vector3Fx>> points,
        std::shared_ptr<
            const std::vector<MphRead::Vector4Fx>> planes,
        std::shared_ptr<
            const std::vector<FhCollisionData>> data,
        std::shared_ptr<
            const std::vector<FhCollisionVector>> vectors,
        std::shared_ptr<
            const std::vector<std::uint16_t>> dataIndices,
        std::shared_ptr<
            const std::vector<
                std::shared_ptr<Portal>>> portals,
        std::shared_ptr<
            const std::vector<FhCollisionEntry>> entries,
        std::shared_ptr<
            const std::vector<std::int32_t>> treeNodeIndices,
        std::shared_ptr<
            const std::vector<FhCollisionTreeNode>> treeNodes)
        : CollisionInfo(
              std::move(points),
              std::move(planes),
              std::move(portals),
              true),
          Header(header),
          Data(std::move(data)),
          Vectors(std::move(vectors)),
          DataIndices(std::move(dataIndices)),
          Entries(std::move(entries)),
          TreeNodeIndices(
              std::move(treeNodeIndices)),
          TreeNodes(std::move(treeNodes))
    {
    }

    void FhCollisionInfo::GetDrawInfo(
        std::shared_ptr<
            const std::vector<
                OpenTK::Mathematics::Vector3>> points,
        OpenTK::Mathematics::Vector3 translation,
        MphRead::EntityType entityType,
        MphRead::Scene* scenePtr)
    {
        static_cast<void>(entityType);

        MphRead::Scene& scene
            = RequireReference(scenePtr);

        OpenTK::Mathematics::Vector4 color(
            OpenTK::Mathematics::Vector3(
                1.0F, 0.0F, 0.0F),
            0.5F);

        color.W = scene.ColDisplayAlpha();

        const std::int32_t polygonId
            = scene.GetNextPolygonId();

        const std::size_t portalCount
            = Count(Portals);

        for (std::size_t i = portalCount;
            i < Count(Data);
            ++i)
        {
            const FhCollisionData& data
                = ManagedAt(Data, i);

            MPHREAD_COLLISION_DEBUG_ASSERT(
                data.VectorCount >= 3
                && data.VectorCount <= 8);

            auto verts = MphRead::NativeRuntime::RentFromSharedArrayPool(
                data.VectorCount);

            for (std::int32_t j = 0;
                j < data.VectorCount;
                ++j)
            {
                const FhCollisionVector& vector
                    = ManagedAt(
                        Vectors,
                        static_cast<std::size_t>(
                            data.VectorStartIndex)
                            + j);

                (*verts)[j]
                    = ManagedAt(
                        points,
                        vector.Point2Index)
                    + translation;
            }

            scene.AddRenderItem(
                MphRead::CullingMode::Back,
                polygonId,
                color,
                MphRead::RenderItemType::Ngon,
                verts,
                data.VectorCount);
        }
    }

    void FhCollisionInfo::GetPartition(
        std::shared_ptr<
            std::vector<
                OpenTK::Mathematics::Vector3>> points,
        MphRead::Scene* scenePtr) const
    {
        MphRead::Scene& scene
            = RequireReference(scenePtr);

        const std::int32_t entryIndex
            = static_cast<std::int32_t>(
                scene.ShowVolumes());

        if (entryIndex <= 0)
        {
            return;
        }

        OpenTK::Mathematics::Vector4 color(
            OpenTK::Mathematics::Vector3(
                1.0F, 0.0F, 0.0F),
            0.5F);

        color.W = scene.ColDisplayAlpha();

        std::int32_t polygonId
            = scene.GetNextPolygonId();

        const FhCollisionEntry& entry
            = ManagedAt(
                Entries,
                static_cast<std::size_t>(
                    entryIndex));

        for (std::int32_t i = 0;
            i < entry.DataCount;
            ++i)
        {
            const std::int32_t dataIndex
                = ManagedAt(
                    DataIndices,
                    static_cast<std::size_t>(
                        entry.DataStartIndex)
                        + i);

            const FhCollisionData& data
                = ManagedAt(
                    Data,
                    static_cast<std::size_t>(
                        dataIndex));

            MPHREAD_COLLISION_DEBUG_ASSERT(
                data.VectorCount >= 3
                && data.VectorCount <= 8);

            auto verts = MphRead::NativeRuntime::RentFromSharedArrayPool(
                data.VectorCount);

            for (std::int32_t j = 0;
                j < data.VectorCount;
                ++j)
            {
                const FhCollisionVector& vector
                    = ManagedAt(
                        Vectors,
                        static_cast<std::size_t>(
                            data.VectorStartIndex)
                            + j);

                (*verts)[j]
                    = ManagedAt(
                        points,
                        vector.Point2Index);
            }

            scene.AddRenderItem(
                MphRead::CullingMode::Back,
                polygonId,
                color,
                MphRead::RenderItemType::Ngon,
                verts,
                data.VectorCount);
        }

        auto bverts = MphRead::NativeRuntime::RentFromSharedArrayPool(
                8);

        const OpenTK::Mathematics::Vector3 minPoint
            = entry.MinBounds.ToFloatVector();

        const OpenTK::Mathematics::Vector3 maxPoint
            = entry.MaxBounds.ToFloatVector();

        const OpenTK::Mathematics::Vector3 sideX(
            maxPoint.X - minPoint.X,
            0.0F,
            0.0F);

        const OpenTK::Mathematics::Vector3 sideY(
            0.0F,
            maxPoint.Y - minPoint.Y,
            0.0F);

        const OpenTK::Mathematics::Vector3 sideZ(
            0.0F,
            0.0F,
            maxPoint.Z - minPoint.Z);

        (*bverts)[0] = minPoint;
        (*bverts)[1] = minPoint + sideZ;
        (*bverts)[2] = minPoint + sideX;
        (*bverts)[3] = minPoint + sideX + sideZ;
        (*bverts)[4] = minPoint + sideY;
        (*bverts)[5] = minPoint + sideY + sideZ;
        (*bverts)[6]
            = minPoint + sideX + sideY;
        (*bverts)[7]
            = minPoint + sideX + sideY + sideZ;

        polygonId = scene.GetNextPolygonId();

        const OpenTK::Mathematics::Vector4 bcolor(
            1.0F,
            0.3F,
            1.0F,
            0.5F);

        scene.AddRenderItem(
            MphRead::CullingMode::Front,
            polygonId,
            bcolor,
            MphRead::RenderItemType::Box,
            bverts,
            8);
    }

    std::shared_ptr<CollisionInstance>
    Collision::GetCollision(
        const MphRead::ModelMetadata* meta,
        bool extra)
    {
        if (meta == nullptr)
        {
            throw System::NullReferenceException();
        }

        std::optional<std::string> path
            = extra
            ? meta->ExtraCollisionPath
            : meta->CollisionPath;

        MPHREAD_COLLISION_DEBUG_ASSERT(
            path.has_value());

        std::string name = meta->Name;

        if (name == "AlimbicCapsule"
            && extra)
        {
            name = "AlmbCapsuleShld";
        }

        return GetCollision(
            std::move(path),
            std::move(name),
            meta->FirstHunt,
            -1,
            true);
    }

    std::shared_ptr<CollisionInstance>
    Collision::GetCollision(
        const MphRead::RoomMetadata* meta,
        std::int32_t roomLayerMask)
    {
        if (meta == nullptr)
        {
            throw System::NullReferenceException();
        }

        if (roomLayerMask == 0
            && meta->NodeLayer > 0)
        {
            const std::uint32_t bit
                = 1U
                << (
                    static_cast<std::uint32_t>(
                        meta->NodeLayer)
                    & 31U
                );

            roomLayerMask
                = static_cast<std::int32_t>(
                    (bit & 0xFFU) << 6);
        }

        return GetCollision(
            std::optional<std::string>(
                meta->CollisionPath),
            meta->Name,
            meta->FirstHunt || meta->Hybrid,
            roomLayerMask,
            false);
    }

    std::shared_ptr<CollisionInstance>
    Collision::GetCollision(
        std::optional<std::string> path,
        std::string name,
        bool firstHunt,
        std::int32_t roomLayerMask,
        bool isEntity)
    {
        CollisionCaches& caches
            = Caches();

        auto& cache
            = firstHunt
            ? caches.Fh
            : caches.Mph;

        if (roomLayerMask == -1)
        {
            if (!path)
            {
                throw System::ArgumentNullException(
                    "key");
            }

            const auto existing
                = cache.find(*path);

            if (existing != cache.end())
            {
                return std::make_shared<
                    CollisionInstance>(
                        std::move(name),
                        existing->second,
                        isEntity);
            }
        }

        if (!path)
        {
            // Exact behavior at this point ultimately depends on the
            // prerequisite Paths.Combine contract.
            throw System::ArgumentNullException(
                "path");
        }

        // Exact prerequisite contracts:
        //
        // Paths::FileSystem
        // Paths::FhFileSystem
        // Paths::Combine(root, relativePath)
        // System::IO::File::ReadAllBytes
        // Read::ReadStruct<T>
        // Read::DoOffsets<T>
        // Scene
        //
        // They remain external to this Collision slice.

        const std::string fullPath
            = Paths::Combine(
                firstHunt
                    ? Paths::FhFileSystem()
                    : Paths::FileSystem(),
                *path);

        const std::vector<std::uint8_t> storage
            = MphRead::NativeRuntime::FileReadAllBytes(
                fullPath);

        const std::span<const std::uint8_t> bytes(
            storage.data(),
            storage.size());

        const CollisionHeader header
            = Read::ReadStruct<CollisionHeader>(
                bytes);

        std::shared_ptr<CollisionInfo> info;

        if (::MphRead::MarshalExtensions::MarshalUtf8(header.Type)
            == "wc01"
            || ::MphRead::MarshalExtensions::MarshalUtf8(header.Type)
            == "wc32")
        {
            info = ReadMphCollision(
                header,
                bytes,
                roomLayerMask);
        }
        else
        {
            info = ReadFhCollision(bytes);
        }

        if (roomLayerMask == -1)
        {
            const auto [iterator, inserted]
                = cache.emplace(
                    *path,
                    info);

            static_cast<void>(iterator);

            if (!inserted)
            {
                // Exact Dictionary.Add ArgumentException translation
                // belongs to the managed exception prerequisite.
                throw std::runtime_error(
                    "An item with the same key has already been added.");
            }
        }

        return std::make_shared<
            CollisionInstance>(
                std::move(name),
                std::move(info),
                isEntity);
    }

    std::shared_ptr<CollisionInstance> Collision::GetEntityCollision(const std::string& path)
    {
        return GetCollision(path, path, false, -1, true);
    }

    std::shared_ptr<MphCollisionInfo>
    Collision::ReadMphCollision(
        CollisionHeader header,
        std::span<const std::uint8_t> bytes,
        std::int32_t roomLayerMask)
    {
        const auto points
            = Read::DoOffsets<MphRead::Vector3Fx>(
                bytes,
                header.PointOffset,
                header.PointCount);

        const auto planes
            = Read::DoOffsets<MphRead::Vector4Fx>(
                bytes,
                header.PlaneOffset,
                header.PlaneCount);

        const auto pointIdxs
            = Read::DoOffsets<std::uint16_t>(
                bytes,
                header.PointIndexOffset,
                header.PointIndexCount);

        const auto data
            = Read::DoOffsets<CollisionData>(
                bytes,
                header.DataOffset,
                header.DataCount);

        const auto dataIdxs
            = Read::DoOffsets<std::uint16_t>(
                bytes,
                header.DataIndexOffset,
                header.DataIndexCount);

        // The lists are held by name: a range-for over *DoOffsets(...) would
        // walk a vector its temporary shared_ptr has already freed.
        auto entries = std::make_shared<std::vector<CollisionEntry>>();
        if (::MphRead::MarshalExtensions::MarshalUtf8(header.Type) == "wc32")
        {
            const auto raws = Read::DoOffsets<RawCollisionEntry32>(
                bytes, header.EntryOffset, header.EntryCount);
            for (const RawCollisionEntry32& raw : *raws)
            {
                entries->emplace_back(static_cast<std::uint16_t>(raw.DataCount), raw.DataStartIndex);
            }
        }
        else
        {
            const auto raws = Read::DoOffsets<RawCollisionEntry>(
                bytes, header.EntryOffset, header.EntryCount);
            for (const RawCollisionEntry& raw : *raws)
            {
                entries->emplace_back(raw.DataCount, raw.DataStartIndex);
            }
        }

        auto portals = std::make_shared<
            std::vector<
                std::shared_ptr<Portal>>>();

        const auto rawPortals
            = Read::DoOffsets<
                RawCollisionPortal>(
                    bytes,
                    header.PortalOffset,
                    header.PortalCount);

        for (const RawCollisionPortal& portal
            : *rawPortals)
        {
            if ((portal.LayerMask & 4) != 0
                || roomLayerMask == -1
                || (portal.LayerMask
                    & roomLayerMask) != 0)
            {
                portals->push_back(
                    std::make_shared<Portal>(
                        portal));
            }
        }

        auto finalData = std::make_shared<
            std::vector<CollisionData>>();

        auto finalIndices = std::make_shared<
            std::vector<std::uint16_t>>();

        auto finalEntries = std::make_shared<
            std::vector<CollisionEntry>>();

        if (roomLayerMask == -1)
        {
            finalData->insert(
                finalData->end(),
                data->begin(),
                data->end());

            finalIndices->insert(
                finalIndices->end(),
                dataIdxs->begin(),
                dataIdxs->end());

            finalEntries->insert(
                finalEntries->end(),
                entries->begin(),
                entries->end());
        }
        else
        {
            std::unordered_map<
                std::uint16_t,
                std::uint16_t> indexMap;

            for (const CollisionEntry& entry
                : *entries)
            {
                if (entry.DataCount > 0)
                {
                    std::uint16_t newCount = 0;

                    const std::uint32_t
                        newStartIndex
                        = static_cast<
                            std::uint32_t>(
                                finalIndices
                                    ->size());

                    for (std::int32_t i = 0;
                        i < entry.DataCount;
                        ++i)
                    {
                        const std::uint16_t
                            oldIndex
                            = dataIdxs->at(
                                static_cast<
                                    std::size_t>(
                                        entry
                                            .DataStartIndex)
                                    + i);

                        const auto mapped
                            = indexMap.find(
                                oldIndex);

                        if (mapped
                            != indexMap.end())
                        {
                            finalIndices
                                ->push_back(
                                    mapped->second);

                            newCount
                                = static_cast<
                                    std::uint16_t>(
                                        newCount + 1);
                        }
                        else
                        {
                            const CollisionData&
                                item
                                = data->at(
                                    oldIndex);

                            if ((item.LayerMask & 4)
                                    != 0
                                || (item.LayerMask
                                    & roomLayerMask)
                                    != 0)
                            {
                                const std::uint16_t
                                    newIndex
                                    = static_cast<
                                        std::uint16_t>(
                                            finalData
                                                ->size());

                                finalIndices
                                    ->push_back(
                                        newIndex);

                                finalData
                                    ->push_back(
                                        item);

                                newCount
                                    = static_cast<
                                        std::uint16_t>(
                                            newCount
                                            + 1);

                                indexMap.emplace(
                                    oldIndex,
                                    newIndex);
                            }
                        }
                    }

                    finalEntries
                        ->emplace_back(
                            newCount,
                            newStartIndex);
                }
                else
                {
                    finalEntries
                        ->push_back(entry);
                }
            }
        }

        return std::make_shared<
            MphCollisionInfo>(
                header,
                points,
                planes,
                pointIdxs,
                finalData,
                finalIndices,
                finalEntries,
                portals);
    }

    std::shared_ptr<FhCollisionInfo>
    Collision::ReadFhCollision(
        std::span<const std::uint8_t> bytes)
    {
        const FhCollisionHeader header
            = Read::ReadStruct<
                FhCollisionHeader>(bytes);

        const auto data
            = Read::DoOffsets<
                FhCollisionData>(
                    bytes,
                    header.DataOffset,
                    header.DataCount);

        const auto vectors
            = Read::DoOffsets<
                FhCollisionVector>(
                    bytes,
                    header.VectorOffset,
                    header.VectorCount);

        const auto dataIndices
            = Read::DoOffsets<std::uint16_t>(
                bytes,
                header.DataIndexOffset,
                header.DataIndexCount);

        const auto points
            = Read::DoOffsets<
                MphRead::Vector3Fx>(
                    bytes,
                    header.PointOffset,
                    header.PointCount);

        const auto planes
            = Read::DoOffsets<
                MphRead::Vector4Fx>(
                    bytes,
                    header.PlaneOffset,
                    header.PlaneCount);

        const auto entries
            = Read::DoOffsets<
                FhCollisionEntry>(
                    bytes,
                    header.EntryOffset,
                    header.EntryCount);

        const auto treeNodeIndices
            = Read::DoOffsets<
                std::int32_t>(
                    bytes,
                    header.TreeNodeIndexOffset,
                    header.TreeNodeIndexCount);

        const auto treeNodes
            = Read::DoOffsets<
                FhCollisionTreeNode>(
                    bytes,
                    header.TreeNodeOffset,
                    header.TreeNodeCount);

        auto portals = std::make_shared<
            std::vector<
                std::shared_ptr<Portal>>>();

        const auto rawPortals
            = Read::DoOffsets<
                FhCollisionPortal>(
                    bytes,
                    header.PortalOffset,
                    header.PortalCount);

        for (const FhCollisionPortal& portal
            : *rawPortals)
        {
            portals->push_back(
                std::make_shared<Portal>(
                    portal,
                    vectors,
                    points,
                    planes));
        }

        return std::make_shared<
            FhCollisionInfo>(
                header,
                points,
                planes,
                data,
                vectors,
                dataIndices,
                portals,
                entries,
                treeNodeIndices,
                treeNodes);
    }
}

#undef MPHREAD_COLLISION_DEBUG_ASSERT
