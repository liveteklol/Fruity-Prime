#include "CollisionDetection.hpp"

#include "../Scene.hpp"
#include "../Entities/RoomEntity.hpp"

#include "../Entities/EntityBase.hpp"
#include "../Renderer.hpp"
#include "../Mods/Network/NetLog.hpp"
#include "Types.hpp"
#include "../NativeRuntime/System/Managed.hpp"
#include "../NativeRuntime/OpenTK/Mathematics.hpp"
#include "NativeRuntime/System/Globalization.hpp"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

using ::MphRead::NativeRuntime::HasFlag;
using ::MphRead::NativeRuntime::MathClamp;
using ::MphRead::NativeRuntime::MathMax;
using ::MphRead::NativeRuntime::MathMin;
using ::MphRead::NativeRuntime::RequireReference;
using ::MphRead::NativeRuntime::UncheckedAdd;
using ::MphRead::NativeRuntime::UncheckedIncrement;
using ::MphRead::NativeRuntime::UncheckedMultiply;
using ::OpenTK::Mathematics::Add;
using ::OpenTK::Mathematics::Divide;
using ::OpenTK::Mathematics::Equal;
using ::OpenTK::Mathematics::Length;
using ::OpenTK::Mathematics::LengthSquared;
using ::OpenTK::Mathematics::Multiply;
using ::OpenTK::Mathematics::Negate;
using ::OpenTK::Mathematics::Normalize;
using ::OpenTK::Mathematics::Subtract;

namespace
{
    using MphRead::CollisionVolume;
    using MphRead::Fixed;
    using MphRead::Matrix;
    using MphRead::Scene;
    using MphRead::VolumeType;
    using MphRead::Formats::CollisionCandidate;
    using MphRead::Formats::CollisionResult;
    using MphRead::Formats::TestFlags;
    using MphRead::Formats::Collision::CollisionData;
    using MphRead::Formats::Collision::CollisionEntry;
    using MphRead::Formats::Collision::CollisionFlags;
    using MphRead::Formats::Collision::CollisionInfo;
    using MphRead::Formats::Collision::CollisionInstance;
    using MphRead::Formats::Collision::EntityCollision;
    using MphRead::Formats::Collision::MphCollisionInfo;
    using MphRead::Formats::Collision::Portal;
    using OpenTK::Mathematics::Vector3;
    using OpenTK::Mathematics::Vector4;

    [[nodiscard]] constexpr Vector4 AddW(Vector4 value, float amount) noexcept
    {
        value.W += amount;
        return value;
    }

    [[nodiscard]] constexpr std::int32_t UncheckedPostIncrement(
        std::int32_t& value) noexcept
    {
        const std::int32_t previous = value;
        value = UncheckedIncrement(value);
        return previous;
    }

    [[nodiscard]] constexpr std::uint16_t ToBits(CollisionFlags value) noexcept
    {
        return static_cast<std::uint16_t>(value);
    }

    [[nodiscard]] bool CollisionDataEqual(
        const CollisionData& left,
        const CollisionData& right) noexcept
    {
        return left.Counter == right.Counter
            && left.PlaneIndex == right.PlaneIndex
            && left.Flags == right.Flags
            && left.LayerMask == right.LayerMask
            && left.PaddingA == right.PaddingA
            && left.PointIndexCount == right.PointIndexCount
            && left.PointStartIndex == right.PointStartIndex;
    }

    [[nodiscard]] bool SeenContains(
        const std::vector<CollisionData>& seen,
        const CollisionData& value)
    {
        for (const CollisionData& existing : seen)
        {
            if (CollisionDataEqual(existing, value))
            {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] MphCollisionInfo& GetMphInfo(
        const std::shared_ptr<CollisionInstance>& instance)
    {
        CollisionInstance& inst = RequireReference(instance);
        CollisionInfo& info = RequireReference(inst.Info);
        auto* mphInfo = dynamic_cast<MphCollisionInfo*>(&info);
        if (mphInfo == nullptr)
        {
            throw std::bad_cast();
        }
        return *mphInfo;
    }

    [[nodiscard]] CollisionResult& ResultAt(
        MphRead::ManagedArray<CollisionResult>* results,
        std::int32_t index)
    {
        auto& array = RequireReference(results);
        return array[static_cast<std::size_t>(index)];
    }

    [[nodiscard]] std::int32_t FloatToInt32(float value) noexcept
    {
        // Fixed::ToInt is the shared .NET-compatible float-to-Int32
        // conversion.  Feed it the original value after its fixed-point
        // scaling so this cast has the same source scale as C#.
        return Fixed::ToInt(value / 4096.0F);
    }

    [[nodiscard]] Vector4 MovePlane(
        Vector4 plane,
        Vector3 translation) noexcept
    {
        if (Equal(translation, Vector3::Zero))
        {
            return plane;
        }
        return AddW(
            plane,
            Vector3::Dot(plane.Xyz(), translation));
    }
}
namespace MphRead::Formats
{
    std::vector<std::shared_ptr<CollisionCandidate>>
        CollisionDetection::_activeItems = []()
        {
            std::vector<std::shared_ptr<CollisionCandidate>> values;
            values.reserve(2048);
            return values;
        }();

    std::deque<std::shared_ptr<CollisionCandidate>>
        CollisionDetection::_inactiveItems{};

    std::vector<std::shared_ptr<CollisionCandidate>>
        CollisionDetection::_tempItems = []()
        {
            std::vector<std::shared_ptr<CollisionCandidate>> values;
            values.reserve(2048);
            return values;
        }();

    std::vector<CollisionData>
        CollisionDetection::_seenData = []()
        {
            std::vector<CollisionData> values;
            values.reserve(64);
            return values;
        }();

    CollisionCandidate::CollisionCandidate(
        std::shared_ptr<CollisionInstance> collision,
        CollisionEntry entry)
        : Collision(std::move(collision)),
          Entry(entry)
    {
    }

    std::int32_t CollisionResult::Slipperiness() const noexcept
    {
        return (ToBits(Flags) & 0x18U) >> 3;
    }

    MphRead::Terrain CollisionResult::Terrain() const noexcept
    {
        return static_cast<MphRead::Terrain>(
            (ToBits(Flags) & 0x1E0U) >> 5);
    }

    void CollisionDetection::Init()
    {
        // Called by every scene's load: the pool starts again rather than
        // growing by another 2048 each time, and lets go of the candidates
        // still pointing at the last room's collision.
        _activeItems.clear();
        _tempItems.clear();
        _seenData.clear();
        _inactiveItems.clear();
        for (std::int32_t i = 0; i < 2048; i++)
        {
            _inactiveItems.push_back(
                std::make_shared<CollisionCandidate>(
                    nullptr,
                    CollisionEntry{}));
        }
    }

    bool CollisionDetection::CheckBetweenPoints(
        const std::vector<std::shared_ptr<CollisionCandidate>>* candidates,
        Vector3 point1,
        Vector3 point2,
        TestFlags flags,
        Scene* scene,
        CollisionResult& result)
    {
        return CheckBetweenPoints(
            candidates,
            point1,
            point2,
            flags,
            scene,
            result,
            true);
    }

    bool CollisionDetection::CheckBetweenPoints(
        Vector3 point1,
        Vector3 point2,
        TestFlags flags,
        Scene* scene,
        CollisionResult& result)
    {
        return CheckBetweenPoints(
            nullptr,
            point1,
            point2,
            flags,
            scene,
            result,
            false);
    }

    bool CollisionDetection::CheckBetweenPoints(
        const std::vector<std::shared_ptr<CollisionCandidate>>* candidates,
        Vector3 point1,
        Vector3 point2,
        TestFlags flags,
        Scene* scene,
        CollisionResult& result,
        bool hasCandidates)
    {
        _seenData.clear();

        bool collided = false;
        std::uint16_t mask = 0;
        const bool includeEntities = !HasFlag(flags, TestFlags::Scan);

        if (HasFlag(flags, TestFlags::Players))
        {
            mask |= ToBits(CollisionFlags::IgnorePlayers);
        }
        if (HasFlag(flags, TestFlags::Beams))
        {
            mask |= ToBits(CollisionFlags::IgnoreBeams);
        }

        std::shared_ptr<EntityCollision> lastEntCol{};
        Vector3 transPoint1 = point1;
        Vector3 transPoint2 = point2;
        float minDist = std::numeric_limits<float>::max();

        if (!hasCandidates)
        {
            candidates = &GetCandidatesForPoints(
                point1,
                point2,
                0.0F,
                includeEntities,
                scene);
        }

        const auto& candidateList = RequireReference(candidates);

        for (std::size_t i = 0; i < candidateList.size(); i++)
        {
            const std::shared_ptr<CollisionCandidate>& candidatePtr
                = candidateList[i];
            CollisionCandidate& candidate = RequireReference(candidatePtr);
            CollisionInstance& inst = RequireReference(candidate.Collision);
            MphCollisionInfo& info = GetMphInfo(candidate.Collision);

            assert(candidate.Entry.DataCount > 0);

            transPoint1 = Subtract(point1, inst.Translation);
            transPoint2 = Subtract(point2, inst.Translation);

            if (candidate.EntityCollision != lastEntCol
                && candidate.EntityCollision != nullptr)
            {
                transPoint1 = Matrix::Vec3MultMtx4(
                    point1,
                    candidate.EntityCollision->Inverse1);
                transPoint2 = Matrix::Vec3MultMtx4(
                    point2,
                    candidate.EntityCollision->Inverse1);
            }

            for (std::int32_t j = 0;
                j < candidate.Entry.DataCount;
                j++)
            {
                const std::size_t dataIndexListIndex
                    = static_cast<std::size_t>(
                        candidate.Entry.DataStartIndex + j);
                const std::uint16_t dataIndex
                    = RequireReference(info.DataIndices).at(dataIndexListIndex);
                const CollisionData data
                    = RequireReference(info.Data).at(dataIndex);

                if ((ToBits(data.Flags) & mask) != 0
                    || SeenContains(_seenData, data))
                {
                    continue;
                }

                if (candidate.EntityCollision == nullptr)
                {
                    _seenData.push_back(data);
                }

                Vector4 plane = RequireReference(info.Planes).at(data.PlaneIndex);
                const float dot1
                    = Vector3::Dot(transPoint1, plane.Xyz())
                    - plane.W;

                if (dot1 > 0.0F)
                {
                    const float dot2
                        = Vector3::Dot(transPoint2, plane.Xyz())
                        - plane.W;

                    if (dot2 <= 0.0F)
                    {
                        float dist = dot1 / (dot1 - dot2);

                        if (dist > 1.0F)
                        {
                            dist = 1.0F;
                        }
                        else if (dist < 0.0F)
                        {
                            dist = 0.0F;
                        }

                        if (dist < minDist)
                        {
                            const Vector3 pos(
                                transPoint1.X
                                    + (transPoint2.X - transPoint1.X) * dist,
                                transPoint1.Y
                                    + (transPoint2.Y - transPoint1.Y) * dist,
                                transPoint1.Z
                                    + (transPoint2.Z - transPoint1.Z) * dist);

                            if (CheckPointOnFace(pos, info, data))
                            {
                                if (candidate.EntityCollision != nullptr)
                                {
                                    result.Position
                                        = Matrix::Vec3MultMtx4(
                                            pos,
                                            candidate.EntityCollision->Transform);

                                    const Vector3 normal
                                        = Matrix::Vec3MultMtx3(
                                            plane.Xyz(),
                                            candidate.EntityCollision->Transform);

                                    const float w
                                        = Vector3::Dot(
                                            result.Position,
                                            normal);

                                    result.Plane = Vector4(normal, w);
                                }
                                else
                                {
                                    const Vector3 translation
                                        = candidate.Collision->Translation;

                                    if (!Equal(
                                        translation,
                                        Vector3::Zero))
                                    {
                                        result.Position
                                            = Add(pos, translation);
                                        result.Plane = AddW(
                                            plane,
                                            Vector3::Dot(
                                                plane.Xyz(),
                                                translation));
                                    }
                                    else
                                    {
                                        result.Position = pos;
                                        result.Plane = plane;
                                    }
                                }

                                minDist = dist;
                                result.Field0 = 0;
                                result.Field14 = 0.0F;
                                result.Flags = data.Flags;
                                result.Distance = dist;
                                result.EntityCollision
                                    = candidate.EntityCollision;
                                collided = true;
                            }
                        }
                    }
                }
            }
        }

        return collided;
    }

    bool CollisionDetection::CheckPointOnFace(
        Vector3 point,
        const MphCollisionInfo& info,
        CollisionData data)
    {
        const std::int32_t axis = data.LayerMask & 3;
        assert(axis >= 0 && axis <= 2);

        const auto firstCoordinate = [axis](Vector3 value) noexcept
        {
            if (axis == 0)
            {
                return value.Y;
            }
            return value.X;
        };

        const auto secondCoordinate = [axis](Vector3 value) noexcept
        {
            if (axis == 0 || axis == 1)
            {
                return value.Z;
            }
            return value.Y;
        };

        const float pointA = firstCoordinate(point);
        const float pointB = secondCoordinate(point);

        Vector3 curVert
            = RequireReference(info.Points).at(
                RequireReference(info.PointIndices).at(data.PointStartIndex));
        const Vector3 firstVert = curVert;

        std::int32_t v8 = 0;

        const auto quadrant =
            [&](Vector3 vertex) noexcept -> std::int32_t
        {
            const float a = firstCoordinate(vertex);
            const float b = secondCoordinate(vertex);

            return a <= pointA
                ? (b <= pointB ? 2 : 1)
                : (b <= pointB ? 3 : 0);
        };

        std::int32_t v1 = quadrant(curVert);
        std::int32_t v39 = 0;
        Vector3 nextVert{};

        do
        {
            if (++v8 == data.PointIndexCount)
            {
                v8 = 0;
            }

            nextVert = RequireReference(info.Points).at(
                RequireReference(info.PointIndices).at(
                    static_cast<std::size_t>(
                        data.PointStartIndex + v8)));

            const std::int32_t v13 = quadrant(nextVert);
            std::int32_t v14 = v13 - v1;

            switch (v14)
            {
            case -2:
            case 2:
            {
                const float curA = firstCoordinate(curVert);
                const float curB = secondCoordinate(curVert);
                const float nextA = firstCoordinate(nextVert);
                const float nextB = secondCoordinate(nextVert);

                const float v15
                    = (curA - nextA)
                    / (curB - nextB);

                if (nextA - (nextB - pointB) * v15 > pointA)
                {
                    v14 = -v14;
                }
                break;
            }

            case -3:
                v14 = 1;
                break;

            case 3:
                v14 = -1;
                break;

            default:
                break;
            }

            v39 += v14;
            v1 = v13;
            curVert = nextVert;
        }
        while (!Equal(nextVert, firstVert));

        return v39 == 4 || v39 == -4;
    }

    void CollisionDetection::ClearCandidates()
    {
        while (!_activeItems.empty())
        {
            std::shared_ptr<CollisionCandidate> item
                = _activeItems.front();
            _activeItems.erase(_activeItems.begin());
            _inactiveItems.push_back(std::move(item));
        }
    }

    std::int32_t CollisionDetection::CheckSphereBetweenPoints(
        const std::vector<std::shared_ptr<CollisionCandidate>>* candidates,
        Vector3 point1,
        Vector3 point2,
        float radius,
        std::int32_t limit,
        bool includeOffset,
        TestFlags flags,
        Scene* scene,
        MphRead::ManagedArray<CollisionResult>* results)
    {
        return CheckSphereBetweenPoints(
            candidates,
            point1,
            point2,
            radius,
            limit,
            includeOffset,
            flags,
            scene,
            results,
            true);
    }

    std::int32_t CollisionDetection::CheckSphereBetweenPoints(
        Vector3 point1,
        Vector3 point2,
        float radius,
        std::int32_t limit,
        bool includeOffset,
        TestFlags flags,
        Scene* scene,
        MphRead::ManagedArray<CollisionResult>* results)
    {
        return CheckSphereBetweenPoints(
            nullptr,
            point1,
            point2,
            radius,
            limit,
            includeOffset,
            flags,
            scene,
            results,
            false);
    }

    std::int32_t CollisionDetection::CheckSphereBetweenPoints(
        const std::vector<std::shared_ptr<CollisionCandidate>>* candidates,
        Vector3 point1,
        Vector3 point2,
        float radius,
        std::int32_t limit,
        bool includeOffset,
        TestFlags flags,
        Scene* scene,
        MphRead::ManagedArray<CollisionResult>* results,
        bool hasCandidates)
    {
        _seenData.clear();

        std::int32_t count = 0;
        std::uint16_t mask = 0;

        const bool includeEntities
            = !HasFlag(flags, TestFlags::Scan);
        (void)includeEntities;

        if (HasFlag(flags, TestFlags::Players))
        {
            mask |= ToBits(CollisionFlags::IgnorePlayers);
        }
        if (HasFlag(flags, TestFlags::Beams))
        {
            mask |= ToBits(CollisionFlags::IgnoreBeams);
        }

        std::shared_ptr<EntityCollision> lastEntCol{};
        Vector3 transPoint1 = point1;
        Vector3 transPoint2 = point2;

        if (!hasCandidates)
        {
            candidates = &GetCandidatesForLimits(
                point1,
                point2,
                radius,
                std::nullopt,
                Vector3::Zero,
                false,
                scene);
        }

        const auto& candidateList = RequireReference(candidates);

        for (std::size_t i = 0;
            i < candidateList.size();
            i++)
        {
            CollisionCandidate& candidate
                = RequireReference(candidateList[i]);

            CollisionInstance& inst
                = RequireReference(candidate.Collision);

            MphCollisionInfo& info
                = GetMphInfo(candidate.Collision);

            assert(candidate.Entry.DataCount > 0);

            transPoint1 = Subtract(point1, inst.Translation);
            transPoint2 = Subtract(point2, inst.Translation);

            if (candidate.EntityCollision != lastEntCol
                && candidate.EntityCollision != nullptr)
            {
                transPoint1 = Matrix::Vec3MultMtx4(
                    point1,
                    candidate.EntityCollision->Inverse1);
                transPoint2 = Matrix::Vec3MultMtx4(
                    point2,
                    candidate.EntityCollision->Inverse1);
            }

            for (std::int32_t j = 0;
                j < candidate.Entry.DataCount;
                j++)
            {
                if (count == limit)
                {
                    break;
                }

                const std::uint16_t dataIndex
                    = RequireReference(info.DataIndices).at(
                        static_cast<std::size_t>(
                            candidate.Entry.DataStartIndex + j));

                const CollisionData data
                    = RequireReference(info.Data).at(dataIndex);

                if ((ToBits(data.Flags) & mask) != 0
                    || SeenContains(_seenData, data))
                {
                    continue;
                }

                if (candidate.EntityCollision == nullptr)
                {
                    _seenData.push_back(data);
                }

                const Vector4 plane
                    = RequireReference(info.Planes).at(data.PlaneIndex);

                const float dot1
                    = Vector3::Dot(
                        transPoint1,
                        plane.Xyz())
                    - plane.W;

                if (dot1 <= 0.0F)
                {
                    continue;
                }

                const float dot2
                    = Vector3::Dot(
                        transPoint2,
                        plane.Xyz())
                    - plane.W;

                if (dot2 > radius)
                {
                    continue;
                }

                float pct = 1.0F;

                if (std::fabs(dot1 - dot2)
                    >= 1.0F / 4096.0F)
                {
                    pct = MathClamp(
                        dot1 / (dot1 - dot2), 0.0F, 1.0F);
                }

                const Vector3 vec = Add(
                    transPoint1,
                    Multiply(
                        Subtract(
                            transPoint2,
                            transPoint1),
                        pct));

                const auto getEdgeDotDifference =
                    [&](std::int32_t pIndex) -> float
                {
                    const std::size_t index
                        = static_cast<std::size_t>(
                            data.PointStartIndex + pIndex);

                    const Vector3 dataPoint1
                        = RequireReference(info.Points).at(
                            RequireReference(info.PointIndices).at(index));

                    const Vector3 dataPoint2
                        = RequireReference(info.Points).at(
                            RequireReference(info.PointIndices).at(index + 1));

                    const Vector3 edgeDir
                        = Normalize(
                            Subtract(
                                dataPoint1,
                                dataPoint2));

                    const Vector3 cross
                        = Vector3::Cross(
                            edgeDir,
                            plane.Xyz());

                    const float crossDot1
                        = Vector3::Dot(
                            cross,
                            dataPoint2);

                    const float crossDot2
                        = Vector3::Dot(
                            vec,
                            cross);

                    return crossDot2 - crossDot1;
                };

                assert(data.PointIndexCount > 0);

                bool fullCollision = true;

                for (std::int32_t p1 = 0;
                    p1 < data.PointIndexCount;
                    p1++)
                {
                    const float dotDiff
                        = getEdgeDotDifference(p1);

                    if (dotDiff < -0.03125F)
                    {
                        fullCollision = false;

                        if (includeOffset
                            && dotDiff >= -radius)
                        {
                            const std::size_t epIndex
                                = static_cast<std::size_t>(
                                    data.PointStartIndex + p1);

                            const Vector3 edgePoint1
                                = RequireReference(info.Points).at(
                                    RequireReference(info.PointIndices).at(epIndex));

                            const Vector3 edgePoint2
                                = RequireReference(info.Points).at(
                                    RequireReference(info.PointIndices).at(
                                        epIndex + 1));

                            CollisionResult result
                                = ResultAt(results, count);

                            result.Field0 = 1;
                            result.EntityCollision
                                = candidate.EntityCollision;
                            result.Flags = data.Flags;
                            result.Field14 = dot2;
                            result.Distance = pct;

                            if (candidate.EntityCollision
                                != nullptr)
                            {
                                const Vector3 normal
                                    = Matrix::Vec3MultMtx3(
                                        plane.Xyz(),
                                        candidate.EntityCollision
                                            ->Transform);

                                const Vector3 wVec
                                    = Matrix::Vec3MultMtx4(
                                        Multiply(
                                            plane.Xyz(),
                                            plane.W),
                                        candidate.EntityCollision
                                            ->Transform);

                                const float w
                                    = Vector3::Dot(
                                        wVec,
                                        normal);

                                result.Plane
                                    = Vector4(normal, w);

                                result.Position
                                    = Matrix::Vec3MultMtx4(
                                        vec,
                                        candidate.EntityCollision
                                            ->Transform);

                                result.EdgePoint1
                                    = Matrix::Vec3MultMtx4(
                                        edgePoint1,
                                        candidate.EntityCollision
                                            ->Transform);

                                result.EdgePoint2
                                    = Matrix::Vec3MultMtx4(
                                        edgePoint2,
                                        candidate.EntityCollision
                                            ->Transform);
                            }
                            else
                            {
                                const Vector3 translation
                                    = candidate.Collision
                                        ->Translation;

                                if (!Equal(
                                    translation,
                                    Vector3::Zero))
                                {
                                    result.Plane
                                        = AddW(
                                            plane,
                                            Vector3::Dot(
                                                plane.Xyz(),
                                                translation));

                                    result.Position
                                        = Add(vec, translation);

                                    result.EdgePoint1
                                        = Add(
                                            edgePoint1,
                                            translation);

                                    result.EdgePoint2
                                        = Add(
                                            edgePoint2,
                                            translation);
                                }
                                else
                                {
                                    result.Plane = plane;
                                    result.Position = vec;
                                    result.EdgePoint1
                                        = edgePoint1;
                                    result.EdgePoint2
                                        = edgePoint2;
                                }
                            }

                            ResultAt(results, count)
                                = result;
                            count++;
                        }

                        break;
                    }
                }

                if (fullCollision)
                {
                    CollisionResult result
                        = ResultAt(results, count);

                    result.Field0 = 0;
                    result.EntityCollision
                        = candidate.EntityCollision;
                    result.Flags = data.Flags;
                    result.Field14 = dot2;
                    result.Distance = pct;

                    if (candidate.EntityCollision
                        != nullptr)
                    {
                        const Vector3 normal
                            = Matrix::Vec3MultMtx3(
                                plane.Xyz(),
                                candidate.EntityCollision
                                    ->Transform);

                        const Vector3 wVec
                            = Matrix::Vec3MultMtx4(
                                Multiply(
                                    plane.Xyz(),
                                    plane.W),
                                candidate.EntityCollision
                                    ->Transform);

                        const float w
                            = Vector3::Dot(
                                wVec,
                                normal);

                        result.Plane
                            = Vector4(normal, w);

                        result.Position
                            = Matrix::Vec3MultMtx4(
                                vec,
                                candidate.EntityCollision
                                    ->Transform);
                    }
                    else
                    {
                        const Vector3 translation
                            = candidate.Collision
                                ->Translation;

                        if (!Equal(
                            translation,
                            Vector3::Zero))
                        {
                            result.Plane
                                = AddW(
                                    plane,
                                    Vector3::Dot(
                                        plane.Xyz(),
                                        translation));

                            result.Position
                                = Add(vec, translation);
                        }
                        else
                        {
                            result.Plane = plane;
                            result.Position = vec;
                        }
                    }

                    ResultAt(results, count)
                        = result;
                    count++;
                }
            }

            if (count == limit)
            {
                break;
            }
        }

        return count;
    }

    bool CollisionDetection::CheckCylinderBetweenPoints(
        Vector3 point1,
        Vector3 point2,
        Vector3 cylPos,
        float cylHeight,
        float radii,
        CollisionResult& result)
    {
        Vector3 travel
            = Subtract(point2, point1);

        const float length = Length(travel);

        travel = Divide(travel, length);

        const Vector3 vec1
            = Subtract(cylPos, point1);

        const float dot
            = Vector3::Dot(travel, vec1);

        if (dot < -radii
            || dot > length + radii)
        {
            return false;
        }

        const Vector3 vec2
            = Subtract(
                vec1,
                Multiply(travel, dot));

        if (vec2.Y > 0.0F
            || vec2.Y < -cylHeight)
        {
            return false;
        }

        if (vec2.X * vec2.X
                + vec2.Z * vec2.Z
            <= radii * radii)
        {
            result.Field0 = 0;
            result.EntityCollision.reset();
            result.Flags = CollisionFlags::None;
            result.Position
                = Subtract(cylPos, vec2);
            result.Distance
                = MathClamp(dot / length, 0.0F, 1.0F);
            result.Plane
                = Vector4(
                    Negate(travel),
                    0.0F);

            return true;
        }

        return false;
    }

    std::int32_t CollisionDetection::CheckInRadius(
        Vector3 point,
        float radius,
        std::int32_t limit,
        bool getSimpleNormal,
        TestFlags flags,
        Scene* scene,
        MphRead::ManagedArray<CollisionResult>* results)
    {
        _seenData.clear();

        std::int32_t count = 0;
        std::uint16_t mask = 0;

        if (HasFlag(flags, TestFlags::Players))
        {
            mask |= ToBits(
                CollisionFlags::IgnorePlayers);
        }

        if (HasFlag(flags, TestFlags::Beams))
        {
            mask |= ToBits(
                CollisionFlags::IgnoreBeams);
        }

        const Vector3 limitMin(
            point.X - radius,
            point.Y - radius,
            point.Z - radius);

        const Vector3 limitMax(
            point.X + radius,
            point.Y + radius,
            point.Z + radius);

        const auto& candidates
            = GetCandidatesForLimits(
                std::nullopt,
                Vector3::Zero,
                0.0F,
                limitMin,
                limitMax,
                false,
                scene);

        for (std::size_t i = 0;
            i < candidates.size();
            i++)
        {
            CollisionCandidate& candidate
                = RequireReference(candidates[i]);

            CollisionInstance& inst
                = RequireReference(candidate.Collision);

            MphCollisionInfo& info
                = GetMphInfo(candidate.Collision);

            const Vector3 transPoint
                = Subtract(
                    point,
                    inst.Translation);

            assert(candidate.Entry.DataCount > 0);

            for (std::int32_t j = 0;
                j < candidate.Entry.DataCount;
                j++)
            {
                if (count == limit)
                {
                    break;
                }

                const std::uint16_t dataIndex
                    = RequireReference(info.DataIndices).at(
                        static_cast<std::size_t>(
                            candidate.Entry.DataStartIndex + j));

                const CollisionData data
                    = RequireReference(info.Data).at(dataIndex);

                if ((ToBits(data.Flags) & mask) != 0
                    || SeenContains(_seenData, data))
                {
                    continue;
                }

                if (candidate.EntityCollision
                    == nullptr)
                {
                    _seenData.push_back(data);
                }

                const Vector4 plane
                    = RequireReference(info.Planes).at(
                        data.PlaneIndex);

                const float dot
                    = Vector3::Dot(
                        transPoint,
                        plane.Xyz())
                    - plane.W;

                float resDot = dot;

                if (dot <= 0.0F
                    || dot > radius)
                {
                    continue;
                }

                const auto getEdgeDotDifference =
                    [&](std::int32_t pIndex) -> float
                {
                    const std::size_t index
                        = static_cast<std::size_t>(
                            data.PointStartIndex
                            + pIndex);

                    const Vector3 point1
                        = RequireReference(info.Points).at(
                            RequireReference(info.PointIndices).at(index));

                    const Vector3 point2
                        = RequireReference(info.Points).at(
                            RequireReference(info.PointIndices).at(
                                index + 1));

                    const Vector3 edgeDir
                        = Normalize(
                            Subtract(
                                point1,
                                point2));

                    const Vector3 cross
                        = Vector3::Cross(
                            edgeDir,
                            plane.Xyz());

                    const float dot1
                        = Vector3::Dot(
                            cross,
                            point2);

                    const float dot2
                        = Vector3::Dot(
                            transPoint,
                            cross);

                    return dot2 - dot1;
                };

                assert(data.PointIndexCount > 0);

                bool noNegCos = true;
                bool foundBlocker = false;
                std::int32_t p1 = 0;

                for (;
                    p1 < data.PointIndexCount;
                    p1++)
                {
                    const float dotDiff
                        = getEdgeDotDifference(p1);

                    if (dotDiff < -0.03125F)
                    {
                        noNegCos = false;
                        break;
                    }
                }

                if (noNegCos)
                {
                    CollisionResult& output
                        = ResultAt(results, count);

                    output.Field0 = 0;
                    output.Plane
                        = MovePlane(
                            plane,
                            inst.Translation);

                    foundBlocker = true;
                }

                if (!foundBlocker)
                {
                    assert(data.PointIndexCount > 0);

                    const float dotDiff
                        = getEdgeDotDifference(p1);

                    if (dotDiff < 0.0F
                        && dotDiff >= -radius)
                    {
                        for (std::int32_t p2 = 0;
                            p2 < data.PointIndexCount;
                            p2++)
                        {
                            const std::size_t index
                                = static_cast<std::size_t>(
                                    data.PointStartIndex
                                    + p2);

                            const Vector3 point1
                                = RequireReference(info.Points).at(
                                    RequireReference(info.PointIndices).at(index));

                            const Vector3 point2
                                = RequireReference(info.Points).at(
                                    RequireReference(info.PointIndices).at(
                                        index + 1));

                            const Vector3 edge
                                = Subtract(
                                    point2,
                                    point1);

                            const float dot1
                                = Vector3::Dot(
                                    edge,
                                    edge);

                            const float dot2
                                = Vector3::Dot(
                                    edge,
                                    Subtract(
                                        transPoint,
                                        point1));

                            const float div
                                = dot2 / dot1;

                            if (div < 0.0F
                                || div >= 1.0F)
                            {
                                const Vector3 vec1
                                    = Subtract(
                                        transPoint,
                                        point1);

                                const float mag1
                                    = Length(vec1);

                                if (mag1 > radius)
                                {
                                    continue;
                                }

                                foundBlocker = true;
                                resDot = mag1;

                                CollisionResult& output
                                    = ResultAt(
                                        results,
                                        count);

                                output.Field0 = 2;

                                if (getSimpleNormal)
                                {
                                    output.Plane
                                        = MovePlane(
                                            plane,
                                            inst.Translation);
                                }
                                else
                                {
                                    output.Plane
                                        = MovePlane(
                                            Vector4(
                                                Divide(
                                                    vec1,
                                                    mag1),
                                                plane.W),
                                            inst.Translation);
                                }

                                break;
                            }

                            const Vector3 vec2
                                = Subtract(
                                    transPoint,
                                    Add(
                                        point1,
                                        Multiply(
                                            edge,
                                            div)));

                            const float mag2
                                = Length(vec2);

                            if (mag2 <= radius)
                            {
                                foundBlocker = true;
                                resDot = mag2;

                                CollisionResult& output
                                    = ResultAt(
                                        results,
                                        count);

                                output.Field0 = 1;

                                if (getSimpleNormal)
                                {
                                    output.Plane
                                        = MovePlane(
                                            plane,
                                            inst.Translation);
                                }
                                else
                                {
                                    output.Plane
                                        = MovePlane(
                                            Vector4(
                                                Divide(
                                                    vec2,
                                                    mag2),
                                                plane.W),
                                            inst.Translation);
                                }

                                break;
                            }
                        }
                    }
                }

                if (foundBlocker)
                {
                    CollisionResult& output
                        = ResultAt(results, count);

                    output.Flags = data.Flags;
                    output.EntityCollision.reset();
                    output.Field14 = resDot;
                    count++;
                }
            }

            if (count == limit)
            {
                break;
            }
        }

        return count;
    }

    const std::vector<std::shared_ptr<CollisionCandidate>>&
    CollisionDetection::GetCandidatesForLimits(
        std::optional<Vector3> point1,
        Vector3 point2,
        float margin,
        std::optional<Vector3> limitMin,
        Vector3 limitMax,
        bool includeEntities,
        Scene* scene)
    {
        ClearCandidates();

        if (!limitMin.has_value())
        {
            assert(point1.has_value());

            if (!point1.has_value())
            {
                throw std::runtime_error(
                    "Nullable object must have a value.");
            }

            const Vector3 p1 = *point1;

            limitMin = Vector3(
                MathMin(
                    MathMin(
                        std::numeric_limits<float>::max(),
                        p1.X),
                    point2.X) - margin,
                MathMin(
                    MathMin(
                        std::numeric_limits<float>::max(),
                        p1.Y),
                    point2.Y) - margin,
                MathMin(
                    MathMin(
                        std::numeric_limits<float>::max(),
                        p1.Z),
                    point2.Z) - margin);

            limitMax = Vector3(
                MathMax(
                    MathMax(
                        std::numeric_limits<float>::lowest(),
                        p1.X),
                    point2.X) + margin,
                MathMax(
                    MathMax(
                        std::numeric_limits<float>::lowest(),
                        p1.Y),
                    point2.Y) + margin,
                MathMax(
                    MathMax(
                        std::numeric_limits<float>::lowest(),
                        p1.Z),
                    point2.Z) + margin);
        }

        GetRoomCandidatesForLimits(
            *limitMin,
            limitMax,
            scene);

        if (includeEntities
            && point1.has_value())
        {
            GetEntityCandidates(
                *limitMin,
                limitMax,
                scene);
        }

        return _activeItems;
    }

    void CollisionDetection::GetRoomCandidatesForLimits(
        Vector3 limitMin,
        Vector3 limitMax,
        Scene* scene)
    {
        Scene& sceneRef = RequireReference(scene);

        if (sceneRef.Room() == nullptr)
        {
            return;
        }

        for (std::size_t i = 0;
            i < sceneRef.Room()->RoomCollision().size();
            i++)
        {
            const std::shared_ptr<CollisionInstance>& instPtr
                = sceneRef.Room()->RoomCollision()[i];

            CollisionInstance& inst = RequireReference(instPtr);
            CollisionInfo& baseInfo = RequireReference(inst.Info);

            if (baseInfo.FirstHunt
                || !inst.Active)
            {
                continue;
            }

            MphCollisionInfo& info
                = GetMphInfo(instPtr);

            constexpr float size = 4.0F;

            const std::int32_t partsX
                = info.Header.PartsX;
            const std::int32_t partsY
                = info.Header.PartsY;
            const std::int32_t partsZ
                = info.Header.PartsZ;

            const Vector3 minPos
                = Add(
                    info.MinPosition,
                    inst.Translation);

            std::int32_t minXPart
                = FloatToInt32(
                    (limitMin.X - minPos.X)
                    / size);

            std::int32_t maxXPart
                = FloatToInt32(
                    (limitMax.X - minPos.X)
                    / size);

            std::int32_t minYPart
                = FloatToInt32(
                    (limitMin.Y - minPos.Y)
                    / size);

            std::int32_t maxYPart
                = FloatToInt32(
                    (limitMax.Y - minPos.Y)
                    / size);

            std::int32_t minZPart
                = FloatToInt32(
                    (limitMin.Z - minPos.Z)
                    / size);

            std::int32_t maxZPart
                = FloatToInt32(
                    (limitMax.Z - minPos.Z)
                    / size);

            if (maxXPart >= 0
                && minXPart <= partsX
                && maxYPart >= 0
                && minYPart <= partsY
                && maxZPart >= 0
                && minZPart <= partsZ)
            {
                minXPart
                    = std::max(minXPart, 0);
                minYPart
                    = std::max(minYPart, 0);
                minZPart
                    = std::max(minZPart, 0);

                maxXPart
                    = std::min(
                        maxXPart,
                        UncheckedAdd(partsX, -1));
                maxYPart
                    = std::min(
                        maxYPart,
                        UncheckedAdd(partsY, -1));
                maxZPart
                    = std::min(
                        maxZPart,
                        UncheckedAdd(partsZ, -1));

                std::int32_t xIndex = minXPart;
                std::int32_t yIndex = minYPart;
                std::int32_t zIndex = minZPart;

                while (yIndex <= maxYPart)
                {
                    while (zIndex <= maxZPart)
                    {
                        while (xIndex <= maxXPart)
                        {
                            std::int32_t entryIndex
                                = UncheckedAdd(
                                    UncheckedAdd(
                                        UncheckedMultiply(
                                            UncheckedMultiply(
                                                yIndex,
                                                partsX),
                                            partsZ),
                                        UncheckedMultiply(
                                            zIndex,
                                            partsX)),
                                    xIndex);

                            const CollisionEntry entry
                                = RequireReference(info.Entries).at(
                                    static_cast<std::size_t>(
                                        UncheckedPostIncrement(entryIndex)));

                            if (entry.DataCount > 0)
                            {
                                if (_inactiveItems.empty())
                                {
                                    std::string message
                                        = "collision pool exhausted: x="
                                        + std::to_string(minXPart)
                                        + ".."
                                        + std::to_string(maxXPart)
                                        + " y="
                                        + std::to_string(minYPart)
                                        + ".."
                                        + std::to_string(maxYPart)
                                        + " z="
                                        + std::to_string(minZPart)
                                        + ".."
                                        + std::to_string(maxZPart)
                                        + " limits=("
                                        + ::MphRead::NativeRuntime::ToString(limitMin.X, "0.0")
                                        + ","
                                        + ::MphRead::NativeRuntime::ToString(limitMin.Y, "0.0")
                                        + ","
                                        + ::MphRead::NativeRuntime::ToString(limitMin.Z, "0.0")
                                        + ")..("
                                        + ::MphRead::NativeRuntime::ToString(limitMax.X, "0.0")
                                        + ","
                                        + ::MphRead::NativeRuntime::ToString(limitMax.Y, "0.0")
                                        + ","
                                        + ::MphRead::NativeRuntime::ToString(limitMax.Z, "0.0")
                                        + ")";

                                    MphRead::Mods::Network::NetLog::Event(
                                        message);

                                    return;
                                }

                                std::shared_ptr<CollisionCandidate> item
                                    = _inactiveItems.front();
                                _inactiveItems.pop_front();

                                item->Collision = instPtr;
                                item->Entry = entry;
                                item->EntityCollision.reset();

                                _tempItems.push_back(
                                    std::move(item));
                            }

                            xIndex = UncheckedIncrement(xIndex);
                        }

                        xIndex = minXPart;
                        zIndex = UncheckedIncrement(zIndex);
                    }

                    xIndex = minXPart;
                    zIndex = minZPart;
                    yIndex = UncheckedIncrement(yIndex);
                }
            }
        }

        while (!_tempItems.empty())
        {
            _activeItems.push_back(
                _tempItems.back());
            _tempItems.pop_back();
        }
    }

    const std::vector<std::shared_ptr<CollisionCandidate>>&
    CollisionDetection::GetCandidatesForPoints(
        Vector3 point1,
        Vector3 point2,
        float margin,
        bool includeEntities,
        Scene* scene)
    {
        ClearCandidates();

        GetRoomCandidatesForPoints(
            point1,
            point2,
            scene);

        if (includeEntities)
        {
            const Vector3 limitMin(
                MathMin(
                    MathMin(
                        std::numeric_limits<float>::max(),
                        point1.X),
                    point2.X) - margin,
                MathMin(
                    MathMin(
                        std::numeric_limits<float>::max(),
                        point1.Y),
                    point2.Y) - margin,
                MathMin(
                    MathMin(
                        std::numeric_limits<float>::max(),
                        point1.Z),
                    point2.Z) - margin);

            const Vector3 limitMax(
                MathMax(
                    MathMax(
                        std::numeric_limits<float>::lowest(),
                        point1.X),
                    point2.X) + margin,
                MathMax(
                    MathMax(
                        std::numeric_limits<float>::lowest(),
                        point1.Y),
                    point2.Y) + margin,
                MathMax(
                    MathMax(
                        std::numeric_limits<float>::lowest(),
                        point1.Z),
                    point2.Z) + margin);

            GetEntityCandidates(
                limitMin,
                limitMax,
                scene);
        }

        return _activeItems;
    }

    void CollisionDetection::GetEntityCandidates(
        Vector3 limitMin,
        Vector3 limitMax,
        Scene* scene)
    {
        Scene& sceneRef = RequireReference(scene);

        auto entityEnumerator = sceneRef.Entities().GetEnumerator();
        while (entityEnumerator.MoveNext())
        {
            const std::shared_ptr<MphRead::Entities::EntityBase> entityPtr = entityEnumerator.Current();
            MphRead::Entities::EntityBase& entity
                = RequireReference(entityPtr);

            if (entity.Type != MphRead::EntityType::Object
                && entity.Type != MphRead::EntityType::Platform)
            {
                continue;
            }

            for (std::int32_t i = 0;
                i < 2;
                i++)
            {
                std::shared_ptr<EntityCollision> entCol
                    = entity.EntityCollision[
                        static_cast<std::size_t>(i)];

                if (entCol == nullptr
                    || entCol->Collision == nullptr
                    || !entCol->Collision->Active
                    || RequireReference(
                        entCol->Collision->Info)
                        .FirstHunt)
                {
                    continue;
                }

                const Vector3 entMin(
                    MathMin(
                        std::numeric_limits<float>::max(),
                        entCol->CurrentCenter.X)
                        - entCol->MaxDistance,
                    MathMin(
                        std::numeric_limits<float>::max(),
                        entCol->CurrentCenter.Y)
                        - entCol->MaxDistance,
                    MathMin(
                        std::numeric_limits<float>::max(),
                        entCol->CurrentCenter.Z)
                        - entCol->MaxDistance);

                const Vector3 entMax(
                    MathMax(
                        std::numeric_limits<float>::lowest(),
                        entCol->CurrentCenter.X)
                        + entCol->MaxDistance,
                    MathMax(
                        std::numeric_limits<float>::lowest(),
                        entCol->CurrentCenter.Y)
                        + entCol->MaxDistance,
                    MathMax(
                        std::numeric_limits<float>::lowest(),
                        entCol->CurrentCenter.Z)
                        + entCol->MaxDistance);

                if (entMin.X <= limitMax.X
                    && entMax.X >= limitMin.X
                    && entMin.Y <= limitMax.Y
                    && entMax.Y >= limitMin.Y
                    && entMin.Z <= limitMax.Z
                    && entMax.Z >= limitMin.Z)
                {
                    const std::shared_ptr<CollisionInstance> inst
                        = entCol->Collision;

                    MphCollisionInfo& info
                        = GetMphInfo(inst);

                    std::int32_t entryIndex = 0;
                    std::int32_t xIndex = 0;
                    std::int32_t yIndex = 0;
                    std::int32_t zIndex = 0;

                    while (yIndex < info.Header.PartsY)
                    {
                        while (zIndex < info.Header.PartsZ)
                        {
                            while (xIndex < info.Header.PartsX)
                            {
                                const CollisionEntry entry
                                    = RequireReference(info.Entries).at(
                                        static_cast<std::size_t>(
                                            UncheckedPostIncrement(entryIndex)));

                                if (entry.DataCount > 0)
                                {
                                    if (_inactiveItems.empty())
                                    {
                                        throw std::runtime_error(
                                            "Queue empty.");
                                    }

                                    std::shared_ptr<CollisionCandidate> item
                                        = _inactiveItems.front();

                                    _inactiveItems.pop_front();

                                    item->Collision = inst;
                                    item->Entry = entry;
                                    item->EntityCollision = entCol;

                                    _tempItems.push_back(
                                        std::move(item));
                                }

                                xIndex = UncheckedIncrement(xIndex);
                            }

                            xIndex = 0;
                            zIndex = UncheckedIncrement(zIndex);
                        }

                        xIndex = 0;
                        zIndex = 0;
                        yIndex = UncheckedIncrement(yIndex);
                    }
                }
            }
        }

        while (!_tempItems.empty())
        {
            _activeItems.push_back(
                _tempItems.back());
            _tempItems.pop_back();
        }
    }

    void CollisionDetection::GetRoomCandidatesForPoints(
        Vector3 point1,
        Vector3 point2,
        Scene* scene)
    {
        Scene& sceneRef = RequireReference(scene);

        if (sceneRef.Room() == nullptr)
        {
            return;
        }

        for (std::size_t roomIndex = 0;
            roomIndex < sceneRef.Room()->RoomCollision().size();
            roomIndex++)
        {
            const std::shared_ptr<CollisionInstance>& instPtr
                = sceneRef.Room()->RoomCollision()[roomIndex];

            CollisionInstance& inst = RequireReference(instPtr);
            CollisionInfo& baseInfo = RequireReference(inst.Info);

            if (baseInfo.FirstHunt
                || !inst.Active)
            {
                continue;
            }

            MphCollisionInfo& info
                = GetMphInfo(instPtr);

            constexpr float size = 4.0F;

            const std::int32_t partsX
                = info.Header.PartsX;
            const std::int32_t partsY
                = info.Header.PartsY;
            const std::int32_t partsZ
                = info.Header.PartsZ;

            const Vector3 minPos
                = Add(
                    info.MinPosition,
                    inst.Translation);

            const Vector3 maxPos(
                minPos.X
                    + partsX * size
                    - Fixed::ToFloat(20),
                minPos.Y
                    + partsY * size
                    - Fixed::ToFloat(20),
                minPos.Z
                    + partsZ * size
                    - Fixed::ToFloat(20));

            const auto testBounds =
                [&](Vector3 point) noexcept
                -> std::int32_t
            {
                std::int32_t bits = 0;

                if (point.X < minPos.X)
                {
                    bits |= 0x1;
                }
                if (point.X > maxPos.X)
                {
                    bits |= 0x2;
                }
                if (point.Y < minPos.Y)
                {
                    bits |= 0x4;
                }
                if (point.Y > maxPos.Y)
                {
                    bits |= 0x8;
                }
                if (point.Z < minPos.Z)
                {
                    bits |= 0x10;
                }
                if (point.Z > maxPos.Z)
                {
                    bits |= 0x20;
                }

                return bits;
            };

            const std::int32_t test1
                = testBounds(point1);

            const std::int32_t test2
                = testBounds(point2);

            if ((test1 & test2) != 0)
            {
                continue;
            }

            bool inside = false;

            if (test1 == 0)
            {
                inside = true;
            }
            else
            {
                if ((test1 & 0x3) != 0)
                {
                    float v7;
                    float v8;

                    if ((test1 & 0x1) != 0)
                    {
                        v7 = minPos.X - point1.X;
                        v8 = minPos.X - point2.X;
                    }
                    else
                    {
                        v7 = point1.X - maxPos.X;
                        v8 = point2.X - maxPos.X;
                    }

                    if (v7 >= 0.0F
                        && v8 < 0.0F)
                    {
                        const float div
                            = v7 / (v7 - v8);

                        const Vector3 newPoint(
                            (point2.X - point1.X)
                                * div + point1.X,
                            (point2.Y - point1.Y)
                                * div + point1.Y,
                            (point2.Z - point1.Z)
                                * div + point1.Z);

                        if (newPoint.Y >= minPos.Y
                            && newPoint.Y <= maxPos.Y
                            && newPoint.Z >= minPos.Z
                            && newPoint.Z <= maxPos.Z)
                        {
                            point1 = newPoint;
                            inside = true;
                        }
                    }
                }

                if (!inside
                    && (test1 & 0xC) != 0)
                {
                    float v7;
                    float v8;

                    if ((test1 & 0x4) != 0)
                    {
                        v7 = minPos.Y - point1.Y;
                        v8 = minPos.Y - point2.Y;
                    }
                    else
                    {
                        v7 = point1.Y - maxPos.Y;
                        v8 = point2.Y - maxPos.Y;
                    }

                    if (v7 >= 0.0F
                        && v8 < 0.0F)
                    {
                        const float div
                            = v7 / (v7 - v8);

                        const Vector3 newPoint(
                            (point2.X - point1.X)
                                * div + point1.X,
                            (point2.Y - point1.Y)
                                * div + point1.Y,
                            (point2.Z - point1.Z)
                                * div + point1.Z);

                        if (newPoint.X >= minPos.X
                            && newPoint.X <= maxPos.X
                            && newPoint.Z >= minPos.Z
                            && newPoint.Z <= maxPos.Z)
                        {
                            point1 = newPoint;
                            inside = true;
                        }
                    }
                }

                if (!inside
                    && (test1 & 0x30) != 0)
                {
                    float v7;
                    float v8;

                    if ((test1 & 0x10) != 0)
                    {
                        v7 = minPos.Z - point1.Z;
                        v8 = minPos.Z - point2.Z;
                    }
                    else
                    {
                        v7 = point1.Z - maxPos.Z;
                        v8 = point2.Z - maxPos.Z;
                    }

                    if (v7 >= 0.0F
                        && v8 < 0.0F)
                    {
                        const float div
                            = v7 / (v7 - v8);

                        const Vector3 newPoint(
                            (point2.X - point1.X)
                                * div + point1.X,
                            (point2.Y - point1.Y)
                                * div + point1.Y,
                            (point2.Z - point1.Z)
                                * div + point1.Z);

                        if (newPoint.X >= minPos.X
                            && newPoint.X <= maxPos.X
                            && newPoint.Y >= minPos.Y
                            && newPoint.Y <= maxPos.Y)
                        {
                            point1 = newPoint;
                            inside = true;
                        }
                    }
                }
            }

            if (!inside)
            {
                continue;
            }

            Vector3 dir
                = Normalize(
                    Subtract(
                        point2,
                        point1));

            dir.X = Fixed::ToFloat(
                Fixed::ToInt(dir.X));
            dir.Y = Fixed::ToFloat(
                Fixed::ToInt(dir.Y));
            dir.Z = Fixed::ToFloat(
                Fixed::ToInt(dir.Z));

            const float step
                = Fixed::ToFloat(0x400);

            std::int32_t curX
                = FloatToInt32(
                    (point1.X - minPos.X)
                    * step);
            std::int32_t curY
                = FloatToInt32(
                    (point1.Y - minPos.Y)
                    * step);
            std::int32_t curZ
                = FloatToInt32(
                    (point1.Z - minPos.Z)
                    * step);

            const std::int32_t endX
                = FloatToInt32(
                    (point2.X - minPos.X)
                    * step);
            const std::int32_t endY
                = FloatToInt32(
                    (point2.Y - minPos.Y)
                    * step);
            const std::int32_t endZ
                = FloatToInt32(
                    (point2.Z - minPos.Z)
                    * step);

            const std::int32_t xSign
                = dir.X <= 0.0F ? -1 : 1;
            const std::int32_t ySign
                = dir.Y <= 0.0F ? -1 : 1;
            const std::int32_t zSign
                = dir.Z <= 0.0F ? -1 : 1;

            const std::int32_t xLimit
                = dir.X <= 0.0F
                    ? -1
                    : partsX;
            const std::int32_t yLimit
                = dir.Y <= 0.0F
                    ? -1
                    : partsY;
            const std::int32_t zLimit
                = dir.Z <= 0.0F
                    ? -1
                    : partsZ;

            const float xStart
                = (dir.X <= 0.0F
                    ? curX * size
                    : UncheckedAdd(curX, 1) * size)
                + minPos.X;

            const float yStart
                = (dir.Y <= 0.0F
                    ? curY * size
                    : UncheckedAdd(curY, 1) * size)
                + minPos.Y;

            const float zStart
                = (dir.Z <= 0.0F
                    ? curZ * size
                    : UncheckedAdd(curZ, 1) * size)
                + minPos.Z;

            float xNext = 1000000.0F;
            float yNext = 1000000.0F;
            float zNext = 1000000.0F;

            float xInc = 0.0F;
            float yInc = 0.0F;
            float zInc = 0.0F;

            if (dir.X != 0.0F)
            {
                const float div = 1.0F / dir.X;
                xNext = (xStart - point1.X) * div;
                xInc = xSign * div * size;
            }

            if (dir.Y != 0.0F)
            {
                const float div = 1.0F / dir.Y;
                yNext = (yStart - point1.Y) * div;
                yInc = ySign * div * size;
            }

            if (dir.Z != 0.0F)
            {
                const float div = 1.0F / dir.Z;
                zNext = (zStart - point1.Z) * div;
                zInc = zSign * div * size;
            }

            while (true)
            {
                if (curX >= 0
                    && curX < partsX
                    && curY >= 0
                    && curY < partsY
                    && curZ >= 0
                    && curZ < partsZ)
                {
                    const std::int32_t entryIndex
                        = UncheckedAdd(
                            curX,
                            UncheckedMultiply(
                                partsX,
                                UncheckedAdd(
                                    curZ,
                                    UncheckedMultiply(
                                        curY,
                                        partsZ))));

                    const CollisionEntry entry
                        = RequireReference(info.Entries).at(
                            static_cast<std::size_t>(
                                entryIndex));

                    if (entry.DataCount > 0)
                    {
                        if (_inactiveItems.empty())
                        {
                            throw std::runtime_error(
                                "Queue empty.");
                        }

                        std::shared_ptr<CollisionCandidate> item
                            = _inactiveItems.front();

                        _inactiveItems.pop_front();

                        item->Collision = instPtr;
                        item->Entry = entry;
                        item->EntityCollision.reset();

                        _tempItems.push_back(
                            std::move(item));
                    }
                }

                if (curX == endX
                    && curY == endY
                    && curZ == endZ)
                {
                    break;
                }

                if (xNext >= yNext)
                {
                    if (yNext >= zNext)
                    {
                        curZ = UncheckedAdd(curZ, zSign);

                        if (curZ == zLimit)
                        {
                            break;
                        }

                        zNext += zInc;
                    }
                    else
                    {
                        curY = UncheckedAdd(curY, ySign);

                        if (curY == yLimit)
                        {
                            break;
                        }

                        yNext += yInc;
                    }
                }
                else if (xNext >= zNext)
                {
                    curZ = UncheckedAdd(curZ, zSign);

                    if (curZ == zLimit)
                    {
                        break;
                    }

                    zNext += zInc;
                }
                else
                {
                    curX = UncheckedAdd(curX, xSign);

                    if (curX == xLimit)
                    {
                        break;
                    }

                    xNext += xInc;
                }
            }
        }

        while (!_tempItems.empty())
        {
            _activeItems.push_back(
                _tempItems.back());

            _tempItems.pop_back();
        }
    }

    bool CollisionDetection::CheckSphereOverlapVolume(
        const CollisionVolume* other,
        Vector3 position,
        float radius,
        CollisionResult& result)
    {
        const CollisionVolume& volume
            = RequireReference(other);

        if (volume.Type == VolumeType::Cylinder)
        {
            Vector3 between
                = Subtract(
                    position,
                    volume.CylinderPosition);

            const float dot
                = Vector3::Dot(
                    volume.CylinderVector,
                    between);

            if (dot >= -radius
                && dot <= volume.CylinderDot + radius)
            {
                between = Subtract(
                    between,
                    Multiply(
                        volume.CylinderVector,
                        dot));

                const float radii
                    = radius
                    + volume.CylinderRadius;

                if (LengthSquared(between)
                    <= radii * radii)
                {
                    result.Field0 = 2;
                    result.EntityCollision.reset();
                    result.Flags
                        = CollisionFlags::None;

                    if (dot < 0.0F)
                    {
                        result.Plane
                            = Vector4(
                                volume.CylinderVector,
                                0.0F);

                        result.Field14 = -dot;
                    }
                    else if (dot
                        <= volume.CylinderDot)
                    {
                        const float magnitude
                            = Length(between);

                        result.Plane
                            = Vector4(
                                Divide(
                                    Negate(between),
                                    magnitude),
                                0.0F);

                        result.Field14
                            = magnitude - radius;
                    }
                    else
                    {
                        result.Plane
                            = Vector4(
                                Negate(
                                    volume.CylinderVector),
                                0.0F);

                        result.Field14
                            = dot
                            - volume.CylinderDot;
                    }

                    return true;
                }
            }

            return false;
        }

        if (volume.Type == VolumeType::Sphere)
        {
            const Vector3 between
                = Subtract(
                    volume.SpherePosition,
                    position);

            const float radii
                = radius
                + volume.SphereRadius;

            if (LengthSquared(between)
                <= radii * radii)
            {
                result.Field0 = 2;
                result.EntityCollision.reset();
                result.Flags
                    = CollisionFlags::None;

                const float magnitude
                    = Length(between);

                result.Plane
                    = Vector4(
                        Divide(
                            between,
                            magnitude),
                        0.0F);

                result.Field14
                    = magnitude - radius;

                return true;
            }

            return false;
        }

        if (volume.Type == VolumeType::Box)
        {
            const Vector3 between
                = Subtract(
                    position,
                    volume.BoxPosition);

            const float dot1
                = Vector3::Dot(
                    volume.BoxVector1,
                    between);

            if (dot1 >= -radius
                && dot1 <= volume.BoxDot1 + radius)
            {
                const float dot2
                    = Vector3::Dot(
                        volume.BoxVector2,
                        between);

                if (dot2 >= -radius
                    && dot2 <= volume.BoxDot2 + radius)
                {
                    const float dot3
                        = Vector3::Dot(
                            volume.BoxVector3,
                            between);

                    if (dot3 >= -radius
                        && dot3 <= volume.BoxDot3 + radius)
                    {
                        return true;
                    }
                }
            }

            return false;
        }

        return false;
    }

    bool CollisionDetection::CheckCylinderOverlapVolume(
        const CollisionVolume* other,
        Vector3 cylinderBottom,
        Vector3 cylinderTop,
        float radius,
        CollisionResult& result)
    {
        const CollisionVolume& volume
            = RequireReference(other);

        if (volume.Type == VolumeType::Cylinder)
        {
            return CheckCylindersOverlap(
                cylinderBottom,
                cylinderTop,
                volume.CylinderPosition,
                volume.CylinderVector,
                volume.CylinderDot,
                radius + volume.CylinderRadius,
                result);
        }

        if (volume.Type == VolumeType::Sphere)
        {
            return CheckCylinderOverlapSphere(
                cylinderBottom,
                cylinderTop,
                volume.SpherePosition,
                radius + volume.SphereRadius,
                result);
        }

        return false;
    }

    bool CollisionDetection::CheckCylindersOverlap(
        Vector3 oneBottom,
        Vector3 oneTop,
        Vector3 twoBottom,
        Vector3 twoVector,
        float twoDot,
        float radii,
        CollisionResult& result)
    {
        float v9 = 0.0F;
        float v10 = 1.0F;

        const Vector3 a
            = Subtract(
                oneBottom,
                twoBottom);

        const Vector3 b
            = Subtract(
                oneTop,
                twoBottom);

        const float v11
            = Vector3::Dot(a, twoVector);

        const float v12
            = Vector3::Dot(b, twoVector);

        if (v11 >= 0.0F)
        {
            if (v11 > twoDot)
            {
                if (v12 > twoDot)
                {
                    return false;
                }

                if (v12 <= v11)
                {
                    v9 = (v11 - twoDot)
                        / (v11 - v12);
                }
                else
                {
                    v9 = (v11 - twoDot)
                        / (v12 - v11);
                }
            }
        }
        else
        {
            if (v12 < 0.0F)
            {
                return false;
            }

            if (v12 <= v11)
            {
                v9 = -v11 / (v11 - v12);
            }
            else
            {
                v9 = -v11 / (v12 - v11);
            }
        }

        if (v12 >= 0.0F)
        {
            if (v12 > twoDot)
            {
                if (v12 <= v11)
                {
                    v10 = 1.0F
                        - (v12 - twoDot)
                            / (v11 - v12);
                }
                else
                {
                    v10 = 1.0F
                        - (v12 - twoDot)
                            / (v12 - v11);
                }
            }
        }
        else if (v12 <= v11)
        {
            v10 = 1.0F
                - (-v12 / (v11 - v12));
        }
        else
        {
            v10 = 1.0F
                - (-v12 / (v12 - v11));
        }

        Vector3 c
            = Multiply(
                twoVector,
                v12 - v11);

        const Vector3 d
            = Subtract(
                oneTop,
                oneBottom);

        c = Subtract(d, c);

        const Vector3 e
            = Add(
                twoBottom,
                Multiply(
                    twoVector,
                    v11));

        const float v15
            = Vector3::Dot(c, c);

        const Vector3 f
            = Subtract(
                e,
                oneBottom);

        const float v16
            = Vector3::Dot(c, f);

        float v17
            = v16 / v15;

        if (v17 >= v9)
        {
            if (v17 > v10)
            {
                v17 = v10;
            }
        }
        else
        {
            v17 = v9;
        }

        const Vector3 g
            = Add(
                oneBottom,
                Multiply(c, v17));

        const Vector3 h
            = Subtract(g, e);

        const float v19
            = Vector3::Dot(h, h);

        if (v19 > radii * radii)
        {
            return false;
        }

        const float v20
            = std::sqrt(v19);

        const float v21
            = std::sqrt(v15);

        float v22
            = v17
            - (radii - v20) / v21;

        if (v22 >= v9)
        {
            if (v22 > v10)
            {
                v22 = v10;
            }
        }
        else
        {
            v22 = v9;
        }

        result.Field0 = 0;
        result.EntityCollision.reset();
        result.Flags = CollisionFlags::None;

        result.Position
            = Add(
                oneBottom,
                Multiply(d, v22));

        result.Distance = v22;

        Vector3 normalDirection = d;

        if (normalDirection.X != 0.0F
            || normalDirection.Y != 0.0F
            || normalDirection.Z != 0.0F)
        {
            normalDirection
                = Normalize(normalDirection);
        }
        else
        {
            normalDirection.X = 1.0F;
        }

        result.Plane.X = -normalDirection.X;
        result.Plane.Y = -normalDirection.Y;
        result.Plane.Z = -normalDirection.Z;

        return true;
    }

    bool CollisionDetection::CheckCylinderOverlapVolumeHelper(
        const CollisionVolume* other,
        Vector3 cylinderBottom,
        Vector3 cylinderVector,
        float cylinderDot,
        float radius,
        CollisionResult& result)
    {
        const CollisionVolume& volume
            = RequireReference(other);

        if (volume.Type == VolumeType::Cylinder)
        {
            CollisionResult discard{};

            const Vector3 cylinderTop
                = Add(
                    cylinderBottom,
                    Multiply(
                        cylinderVector,
                        cylinderDot));

            return CheckCylindersOverlap(
                cylinderBottom,
                cylinderTop,
                volume.CylinderPosition,
                volume.CylinderVector,
                volume.CylinderDot,
                radius + volume.CylinderRadius,
                discard);
        }

        if (volume.Type == VolumeType::Sphere)
        {
            Vector3 between
                = Subtract(
                    volume.SpherePosition,
                    cylinderBottom);

            const float dot1
                = Vector3::Dot(
                    between,
                    cylinderVector);

            if (dot1 <= cylinderDot
                    + volume.SphereRadius
                && dot1 >= -volume.SphereRadius)
            {
                between
                    = Subtract(
                        between,
                        Multiply(
                            cylinderVector,
                            dot1));

                const float radii
                    = volume.SphereRadius
                    + radius;

                const float dot2
                    = Vector3::Dot(
                        between,
                        between);

                if (dot2 <= radii * radii)
                {
                    result.Field0 = 2;
                    result.EntityCollision.reset();
                    result.Flags
                        = CollisionFlags::None;

                    if (cylinderDot < 0.0F)
                    {
                        result.Plane
                            = Vector4(
                                Negate(
                                    volume.SpherePosition),
                                0.0F);

                        result.Field14 = -dot1;
                    }
                    else if (dot1 <= cylinderDot)
                    {
                        const float magnitude
                            = Length(between);

                        result.Plane
                            = Vector4(
                                Divide(
                                    between,
                                    magnitude),
                                0.0F);

                        result.Field14
                            = magnitude - radius;
                    }
                    else
                    {
                        result.Plane
                            = Vector4(
                                volume.SpherePosition,
                                0.0F);

                        result.Field14
                            = dot1 - cylinderDot;
                    }

                    return true;
                }
            }
        }

        return false;
    }

    bool CollisionDetection::CheckVolumesOverlap(
        const CollisionVolume* one,
        const CollisionVolume* two,
        CollisionResult& result)
    {
        const CollisionVolume& first
            = RequireReference(one);
        const CollisionVolume& second
            = RequireReference(two);

        if (second.Type == VolumeType::Box)
        {
            if (first.Type == VolumeType::Sphere)
            {
                return CheckSphereOverlapVolume(
                    two,
                    first.SpherePosition,
                    first.SphereRadius,
                    result);
            }

            if (first.Type == VolumeType::Cylinder)
            {
                return CheckCylinderOverlapVolumeHelper(
                    two,
                    first.CylinderPosition,
                    first.CylinderVector,
                    first.CylinderDot,
                    first.CylinderRadius,
                    result);
            }

            return false;
        }

        if (second.Type == VolumeType::Cylinder)
        {
            return CheckCylinderOverlapVolumeHelper(
                one,
                second.CylinderPosition,
                second.CylinderVector,
                second.CylinderDot,
                second.CylinderRadius,
                result);
        }

        if (second.Type == VolumeType::Sphere)
        {
            return CheckSphereOverlapVolume(
                one,
                second.SpherePosition,
                second.SphereRadius,
                result);
        }

        return false;
    }

    bool CollisionDetection::CheckCylinderOverlapSphere(
        Vector3 cylinderBottom,
        Vector3 cylinderTop,
        Vector3 spherePosition,
        float radii,
        CollisionResult& result)
    {
        Vector3 a
            = Subtract(
                cylinderTop,
                cylinderBottom);

        const float v7 = Length(a);

        const Vector3 b
            = Subtract(
                spherePosition,
                cylinderBottom);

        if (v7 <= 0.0F)
        {
            if (LengthSquared(b)
                <= radii * radii)
            {
                result.Field0 = 0;
                result.EntityCollision.reset();
                result.Flags
                    = CollisionFlags::None;
                result.Distance = 0.0F;
                result.Position
                    = cylinderBottom;
                result.Plane.X = 1.0F;
                result.Plane.Y = 0.0F;
                result.Plane.Z = 0.0F;
                return true;
            }

            return false;
        }

        a = Divide(a, v7);

        const float v12
            = Vector3::Dot(a, b);

        if (v12 >= -radii
            && v12 <= v7 + radii)
        {
            const Vector3 c
                = Subtract(
                    b,
                    Multiply(a, v12));

            if (LengthSquared(c)
                <= radii * radii)
            {
                result.Field0 = 0;
                result.EntityCollision.reset();
                result.Flags
                    = CollisionFlags::None;

                Vector3 position
                    = Subtract(
                        spherePosition,
                        c);

                const float v15
                    = Vector3::Dot(c, c);

                const float v16
                    = std::sqrt(
                        radii * radii - v15);

                const Vector3 d
                    = Multiply(a, v16);

                position
                    = Subtract(position, d);

                result.Position = position;

                float distance
                    = v12
                    / (v7 + 2.0F * radii);

                if (distance > 1.0F)
                {
                    distance = 1.0F;
                }
                else if (distance < 0.0F)
                {
                    distance = 0.0F;
                }

                result.Distance = distance;

                const Vector3 normal
                    = Normalize(
                        Subtract(
                            position,
                            spherePosition));

                result.Plane.X = normal.X;
                result.Plane.Y = normal.Y;
                result.Plane.Z = normal.Z;

                return true;
            }
        }

        return false;
    }

    bool CollisionDetection::CheckCylinderIntersectPlane(
        Vector3 cylinderBottom,
        Vector3 cylinderTop,
        Vector4 plane,
        CollisionResult& result)
    {
        const float sum
            = plane.X
                * (cylinderTop.X - cylinderBottom.X)
            + plane.Y
                * (cylinderTop.Y - cylinderBottom.Y)
            + plane.Z
                * (cylinderTop.Z - cylinderBottom.Z);

        if (sum == 0.0F)
        {
            return false;
        }

        const float distance
            = (plane.W
                - Vector3::Dot(
                    plane.Xyz(),
                    cylinderBottom))
            / sum;

        if (distance < 0.0F
            || distance > 1.0F)
        {
            return false;
        }

        result.Position
            = Add(
                cylinderBottom,
                Multiply(
                    Subtract(
                        cylinderTop,
                        cylinderBottom),
                    distance));

        result.Distance = distance;
        result.EntityCollision.reset();

        return true;
    }

    bool CollisionDetection::CheckPortBetweenPoints(
        const Portal* portal,
        Vector3 point1,
        Vector3 point2,
        bool otherSide)
    {
        const Portal& value = RequireReference(portal);

        float dotPrev
            = Vector3::Dot(
                point1,
                value.Plane.Xyz())
            - value.Plane.W;

        float dotCur
            = Vector3::Dot(
                point2,
                value.Plane.Xyz())
            - value.Plane.W;

        if (otherSide)
        {
            dotPrev *= -1.0F;
            dotCur *= -1.0F;
        }

        if (dotPrev > 0.0F
            && dotCur <= 0.0F)
        {
            const float div
                = dotPrev
                / (dotPrev - dotCur);

            const Vector3 vec
                = Add(
                    point1,
                    Multiply(
                        Subtract(
                            point2,
                            point1),
                        div));

            assert(
                RequireReference(value.Points).size()
                == RequireReference(value.Planes).size());

            assert(!RequireReference(value.Planes).empty());

            for (std::size_t i = 0;
                i < RequireReference(value.Planes).size();
                i++)
            {
                const Vector4 sidePlane
                    = RequireReference(value.Planes)[i];

                if (Vector3::Dot(
                        vec,
                        sidePlane.Xyz())
                        - sidePlane.W
                    < Fixed::ToFloat(-4224))
                {
                    return false;
                }
            }

            return true;
        }

        return false;
    }
}
