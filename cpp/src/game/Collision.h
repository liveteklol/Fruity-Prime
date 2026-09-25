#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Room collision (wc01) and the queries the player uses. Ported from
// Formats/Collision.cs and Formats/CollisionDetection.cs.
namespace fp {

using Vec3 = std::array<float, 3>;
using Vec4 = std::array<float, 4>; // xyz normal, w distance

inline float dot(const Vec3& a, const Vec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
inline Vec3 operator+(const Vec3& a, const Vec3& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
inline Vec3 operator-(const Vec3& a, const Vec3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
inline Vec3 operator*(const Vec3& a, float s) { return {a[0] * s, a[1] * s, a[2] * s}; }
inline Vec3 xyz(const Vec4& p) { return {p[0], p[1], p[2]}; }

struct CollisionData {
    uint16_t planeIndex;
    uint16_t flags;
    uint16_t layerMask;
    uint16_t pointIndexCount;
    uint16_t pointStartIndex;

    int slipperiness() const { return (flags & 0x18) >> 3; }
    int terrain() const { return (flags & 0x1E0) >> 5; }
    int axis() const { return layerMask & 3; }
};

struct CollisionEntry {
    uint16_t dataCount;
    uint16_t dataStartIndex;
};

namespace CollisionFlags {
constexpr uint16_t Damaging = 0x1;
constexpr uint16_t IgnorePlayers = 0x2000;
constexpr uint16_t IgnoreBeams = 0x4000;
} // namespace CollisionFlags

namespace TestFlags {
constexpr uint32_t Players = 0x2000;
constexpr uint32_t Beams = 0x4000;
constexpr uint32_t Scan = 0x8000;
} // namespace TestFlags

struct CollisionResult {
    uint8_t field0 = 0; // 0: face, 1: an edge the sphere overlaps
    uint16_t flags = 0;
    Vec4 plane{};
    float field14 = 0;
    Vec3 position{};
    float distance = 0; // fraction along the segment
    Vec3 edgePoint1{}, edgePoint2{};

    int slipperiness() const { return (flags & 0x18) >> 3; }
    int terrain() const { return (flags & 0x1E0) >> 5; }
};

// CollisionDetection.CheckCylinderIntersectPlane: where the segment bottom-top crosses the plane.
bool checkCylinderIntersectPlane(const Vec3& cylBot, const Vec3& cylTop, const Vec4& plane, CollisionResult& result);
// CollisionDetection.CheckCylinderOverlapSphere: a segment swept by `radii` against a point.
bool checkCylinderOverlapSphere(const Vec3& cylBot, const Vec3& cylTop, const Vec3& spherePos, float radii, CollisionResult& result);
// CollisionDetection.CheckCylindersOverlap: a segment swept by `radii` against the upright
// segment from twoBottom along twoVector for twoDot.
bool checkCylindersOverlap(const Vec3& oneBottom, const Vec3& oneTop, const Vec3& twoBottom, const Vec3& twoVector, float twoDot,
    float radii, CollisionResult& result);

class RoomCollision {
public:
    // layerMask: the room's node layer mask; faces outside it are dropped (bit 2 is always kept).
    static RoomCollision load(const std::filesystem::path& file, int layerMask);

    const std::vector<Vec3>& points() const { return m_points; }
    const std::vector<Vec4>& planes() const { return m_planes; }
    const std::vector<CollisionData>& data() const { return m_data; }
    size_t faceCount() const { return m_data.size(); }

    // CollisionDetection.GetCandidatesForLimits: grid entries overlapping the box.
    std::vector<CollisionEntry> candidates(const Vec3& limitMin, const Vec3& limitMax) const;

    // CollisionDetection.CheckBetweenPoints: nearest face the segment crosses front to back.
    bool checkBetweenPoints(const std::vector<CollisionEntry>& candidates, const Vec3& point1, const Vec3& point2,
        uint32_t testFlags, CollisionResult& result) const;
    bool checkBetweenPoints(const Vec3& point1, const Vec3& point2, uint32_t testFlags, CollisionResult& result) const;

    // CollisionDetection.CheckSphereBetweenPoints: faces a sphere moving from point1 to point2 touches.
    int checkSphereBetweenPoints(const std::vector<CollisionEntry>& candidates, const Vec3& point1, const Vec3& point2,
        float radius, int limit, bool includeOffset, uint32_t testFlags, CollisionResult* results) const;

private:
    bool pointOnFace(const Vec3& point, const CollisionData& data) const;

    std::vector<Vec3> m_points;
    std::vector<Vec4> m_planes;
    std::vector<uint16_t> m_pointIndices;
    std::vector<CollisionData> m_data;
    std::vector<uint16_t> m_dataIndices;
    std::vector<CollisionEntry> m_entries;
    int m_partsX = 0, m_partsY = 0, m_partsZ = 0;
    Vec3 m_minPosition{};
};

} // namespace fp
