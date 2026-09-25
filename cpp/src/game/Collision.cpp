#include "Collision.h"

#include "formats/Enums.h"
#include "formats/Model.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <stdexcept>

namespace fp {

namespace {

template <typename T>
T readAt(const std::vector<uint8_t>& bytes, size_t offset)
{
    if (offset + sizeof(T) > bytes.size()) {
        throw std::runtime_error("collision file truncated at " + std::to_string(offset));
    }
    T value;
    std::memcpy(&value, bytes.data() + offset, sizeof(T));
    return value;
}

Vec3 readVec3(const std::vector<uint8_t>& b, size_t o)
{
    return {fxToFloat(readAt<int32_t>(b, o)), fxToFloat(readAt<int32_t>(b, o + 4)), fxToFloat(readAt<int32_t>(b, o + 8))};
}

Vec3 normalized(Vec3 v)
{
    const float len = std::sqrt(dot(v, v));
    return len > 0 ? v * (1.0f / len) : v;
}

Vec3 cross(const Vec3& a, const Vec3& b)
{
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

} // namespace

RoomCollision RoomCollision::load(const std::filesystem::path& file, int layerMask)
{
    const std::vector<uint8_t> b = readFile(file);
    if (b.size() < 84 || std::memcmp(b.data(), "wc01", 4) != 0) {
        throw std::runtime_error("not an MPH collision file (First Hunt collision is not supported)");
    }
    RoomCollision c;
    const auto pointCount = readAt<uint32_t>(b, 4), pointOffset = readAt<uint32_t>(b, 8);
    const auto planeCount = readAt<uint32_t>(b, 12), planeOffset = readAt<uint32_t>(b, 16);
    const auto pointIndexCount = readAt<uint32_t>(b, 20), pointIndexOffset = readAt<uint32_t>(b, 24);
    const auto dataCount = readAt<uint32_t>(b, 28), dataOffset = readAt<uint32_t>(b, 32);
    const auto dataIndexCount = readAt<uint32_t>(b, 36), dataIndexOffset = readAt<uint32_t>(b, 40);
    c.m_partsX = readAt<int32_t>(b, 44);
    c.m_partsY = readAt<int32_t>(b, 48);
    c.m_partsZ = readAt<int32_t>(b, 52);
    c.m_minPosition = readVec3(b, 56);
    const auto entryCount = readAt<uint32_t>(b, 68), entryOffset = readAt<uint32_t>(b, 72);

    for (uint32_t i = 0; i < pointCount; i++) {
        c.m_points.push_back(readVec3(b, pointOffset + i * 12));
    }
    for (uint32_t i = 0; i < planeCount; i++) {
        const size_t o = planeOffset + i * 16;
        c.m_planes.push_back({fxToFloat(readAt<int32_t>(b, o)), fxToFloat(readAt<int32_t>(b, o + 4)),
            fxToFloat(readAt<int32_t>(b, o + 8)), fxToFloat(readAt<int32_t>(b, o + 12))});
    }
    for (uint32_t i = 0; i < pointIndexCount; i++) {
        c.m_pointIndices.push_back(readAt<uint16_t>(b, pointIndexOffset + i * 2));
    }
    std::vector<CollisionData> data;
    for (uint32_t i = 0; i < dataCount; i++) {
        const size_t o = dataOffset + i * 16;
        data.push_back(CollisionData{readAt<uint16_t>(b, o + 4), readAt<uint16_t>(b, o + 6), readAt<uint16_t>(b, o + 8),
            readAt<uint16_t>(b, o + 12), readAt<uint16_t>(b, o + 14)});
    }
    std::vector<uint16_t> dataIndices;
    for (uint32_t i = 0; i < dataIndexCount; i++) {
        dataIndices.push_back(readAt<uint16_t>(b, dataIndexOffset + i * 2));
    }
    // Collision.ReadMphCollision: keep faces in the layer, renumbered, entries rebuilt.
    std::map<uint16_t, uint16_t> indexMap;
    for (uint32_t i = 0; i < entryCount; i++) {
        const auto count = readAt<uint16_t>(b, entryOffset + i * 4);
        const auto start = readAt<uint16_t>(b, entryOffset + i * 4 + 2);
        if (count == 0) {
            c.m_entries.push_back({count, start});
            continue;
        }
        uint16_t newCount = 0;
        const auto newStart = static_cast<uint16_t>(c.m_dataIndices.size());
        for (uint16_t j = 0; j < count; j++) {
            const uint16_t oldIndex = dataIndices.at(start + j);
            if (auto it = indexMap.find(oldIndex); it != indexMap.end()) {
                c.m_dataIndices.push_back(it->second);
                newCount++;
                continue;
            }
            const CollisionData& item = data.at(oldIndex);
            if ((item.layerMask & 4) != 0 || (item.layerMask & layerMask) != 0) {
                const auto newIndex = static_cast<uint16_t>(c.m_data.size());
                c.m_dataIndices.push_back(newIndex);
                c.m_data.push_back(item);
                indexMap[oldIndex] = newIndex;
                newCount++;
            }
        }
        c.m_entries.push_back({newCount, newStart});
    }
    return c;
}

std::vector<CollisionEntry> RoomCollision::candidates(const Vec3& limitMin, const Vec3& limitMax) const
{
    std::vector<CollisionEntry> out;
    constexpr float size = 4;
    int minX = static_cast<int>((limitMin[0] - m_minPosition[0]) / size);
    int maxX = static_cast<int>((limitMax[0] - m_minPosition[0]) / size);
    int minY = static_cast<int>((limitMin[1] - m_minPosition[1]) / size);
    int maxY = static_cast<int>((limitMax[1] - m_minPosition[1]) / size);
    int minZ = static_cast<int>((limitMin[2] - m_minPosition[2]) / size);
    int maxZ = static_cast<int>((limitMax[2] - m_minPosition[2]) / size);
    if (maxX < 0 || minX > m_partsX || maxY < 0 || minY > m_partsY || maxZ < 0 || minZ > m_partsZ) {
        return out;
    }
    minX = std::max(minX, 0);
    minY = std::max(minY, 0);
    minZ = std::max(minZ, 0);
    maxX = std::min(maxX, m_partsX - 1);
    maxY = std::min(maxY, m_partsY - 1);
    maxZ = std::min(maxZ, m_partsZ - 1);
    // The game walks its linked list backwards: collect, then reverse.
    for (int y = minY; y <= maxY; y++) {
        for (int z = minZ; z <= maxZ; z++) {
            for (int x = minX; x <= maxX; x++) {
                const size_t index = static_cast<size_t>(y) * m_partsX * m_partsZ + static_cast<size_t>(z) * m_partsX + x;
                if (index < m_entries.size() && m_entries[index].dataCount > 0) {
                    out.push_back(m_entries[index]);
                }
            }
        }
    }
    std::reverse(out.begin(), out.end());
    return out;
}

bool RoomCollision::pointOnFace(const Vec3& point, const CollisionData& data) const
{
    // CollisionDetection.CheckPointOnFace: winding count in the plane of the two
    // axes other than the normal's dominant one.
    const int axis = data.axis();
    const int a = axis == 0 ? 1 : 0; // "horizontal" coordinate
    const int b = axis == 2 ? 1 : 2; // "vertical" coordinate
    auto quadrant = [&](const Vec3& v) {
        return v[a] <= point[a] ? (v[b] <= point[b] ? 2 : 1) : (v[b] <= point[b] ? 3 : 0);
    };
    auto vertex = [&](int i) -> const Vec3& { return m_points.at(m_pointIndices.at(data.pointStartIndex + i)); };
    Vec3 curVert = vertex(0);
    const Vec3 firstVert = curVert;
    int index = 0;
    int q1 = quadrant(curVert);
    int winding = 0;
    Vec3 nextVert;
    int guard = 0;
    do {
        if (++index == data.pointIndexCount) {
            index = 0;
        }
        nextVert = vertex(index);
        const int q2 = quadrant(nextVert);
        int delta = q2 - q1;
        switch (delta) {
        case -2:
        case 2: {
            const float slope = (curVert[a] - nextVert[a]) / (curVert[b] - nextVert[b]);
            if (nextVert[a] - (nextVert[b] - point[b]) * slope > point[a]) {
                delta = -delta;
            }
            break;
        }
        case -3:
            delta = 1;
            break;
        case 3:
            delta = -1;
            break;
        default:
            break;
        }
        winding += delta;
        q1 = q2;
        curVert = nextVert;
    } while (nextVert != firstVert && ++guard < 1024);
    return winding == 4 || winding == -4;
}

bool RoomCollision::checkBetweenPoints(const Vec3& point1, const Vec3& point2, uint32_t testFlags, CollisionResult& result) const
{
    const Vec3 lo{std::min(point1[0], point2[0]), std::min(point1[1], point2[1]), std::min(point1[2], point2[2])};
    const Vec3 hi{std::max(point1[0], point2[0]), std::max(point1[1], point2[1]), std::max(point1[2], point2[2])};
    return checkBetweenPoints(candidates(lo, hi), point1, point2, testFlags, result);
}

bool RoomCollision::checkBetweenPoints(const std::vector<CollisionEntry>& candidates, const Vec3& point1, const Vec3& point2,
    uint32_t testFlags, CollisionResult& result) const
{
    const auto mask = static_cast<uint16_t>(testFlags & (TestFlags::Players | TestFlags::Beams));
    std::vector<bool> seen(m_data.size(), false);
    bool collided = false;
    float minDist = std::numeric_limits<float>::max();
    for (const CollisionEntry& entry : candidates) {
        for (int j = 0; j < entry.dataCount; j++) {
            const uint16_t dataIndex = m_dataIndices.at(entry.dataStartIndex + j);
            const CollisionData& data = m_data.at(dataIndex);
            if ((data.flags & mask) != 0 || seen[dataIndex]) {
                continue;
            }
            seen[dataIndex] = true;
            const Vec4& plane = m_planes.at(data.planeIndex);
            const float dot1 = dot(point1, xyz(plane)) - plane[3];
            if (dot1 <= 0) {
                continue;
            }
            const float dot2 = dot(point2, xyz(plane)) - plane[3];
            if (dot2 > 0) {
                continue;
            }
            const float dist = std::clamp(dot1 / (dot1 - dot2), 0.0f, 1.0f);
            if (dist >= minDist) {
                continue;
            }
            const Vec3 pos = point1 + (point2 - point1) * dist;
            if (pointOnFace(pos, data)) {
                result = {};
                result.position = pos;
                result.plane = plane;
                result.flags = data.flags;
                result.distance = dist;
                minDist = dist;
                collided = true;
            }
        }
    }
    return collided;
}

int RoomCollision::checkSphereBetweenPoints(const std::vector<CollisionEntry>& candidates, const Vec3& point1, const Vec3& point2,
    float radius, int limit, bool includeOffset, uint32_t testFlags, CollisionResult* results) const
{
    const auto mask = static_cast<uint16_t>(testFlags & (TestFlags::Players | TestFlags::Beams));
    std::vector<bool> seen(m_data.size(), false);
    int count = 0;
    for (const CollisionEntry& entry : candidates) {
        for (int j = 0; j < entry.dataCount && count < limit; j++) {
            const uint16_t dataIndex = m_dataIndices.at(entry.dataStartIndex + j);
            const CollisionData& data = m_data.at(dataIndex);
            if ((data.flags & mask) != 0 || seen[dataIndex]) {
                continue;
            }
            seen[dataIndex] = true;
            const Vec4& plane = m_planes.at(data.planeIndex);
            const Vec3 normal = xyz(plane);
            const float dot1 = dot(point1, normal) - plane[3];
            if (dot1 <= 0) {
                continue; // plane is behind the starting point
            }
            const float dot2 = dot(point2, normal) - plane[3];
            if (dot2 > radius) {
                continue; // more than radius ahead of the end point
            }
            float pct = 1;
            if (std::fabs(dot1 - dot2) >= 1 / 4096.0f) {
                pct = std::clamp(dot1 / (dot1 - dot2), 0.0f, 1.0f);
            }
            const Vec3 vec = point1 + (point2 - point1) * pct;
            auto point = [&](int i) -> const Vec3& { return m_points.at(m_pointIndices.at(data.pointStartIndex + i)); };
            bool fullCollision = true;
            for (int p1 = 0; p1 < data.pointIndexCount; p1++) {
                // index + 1 may be past the count: that is the copy of the first index.
                const Vec3& dataPoint1 = point(p1);
                const Vec3& dataPoint2 = point(p1 + 1);
                const Vec3 edgeCross = cross(normalized(dataPoint1 - dataPoint2), normal);
                const float dotDiff = dot(vec, edgeCross) - dot(edgeCross, dataPoint2);
                if (dotDiff < -0.03125f) {
                    fullCollision = false;
                    // As in the game, the first edge the sphere is outside of decides.
                    if (includeOffset && dotDiff >= -radius) {
                        CollisionResult& r = results[count++];
                        r = {};
                        r.field0 = 1;
                        r.flags = data.flags;
                        r.field14 = dot2;
                        r.distance = pct;
                        r.plane = plane;
                        r.position = vec;
                        r.edgePoint1 = dataPoint1;
                        r.edgePoint2 = dataPoint2;
                    }
                    break;
                }
            }
            if (fullCollision) {
                CollisionResult& r = results[count++];
                r = {};
                r.field0 = 0;
                r.flags = data.flags;
                r.field14 = dot2;
                r.distance = pct;
                r.plane = plane;
                r.position = vec;
            }
        }
        if (count == limit) {
            break;
        }
    }
    return count;
}

bool checkCylinderIntersectPlane(const Vec3& cylBot, const Vec3& cylTop, const Vec4& plane, CollisionResult& result)
{
    const float sum = plane[0] * (cylTop[0] - cylBot[0]) + plane[1] * (cylTop[1] - cylBot[1]) + plane[2] * (cylTop[2] - cylBot[2]);
    if (sum == 0) {
        return false;
    }
    const float dist = (plane[3] - (plane[0] * cylBot[0] + plane[1] * cylBot[1] + plane[2] * cylBot[2])) / sum;
    if (dist < 0 || dist > 1) {
        return false;
    }
    result.position = cylBot + (cylTop - cylBot) * dist;
    result.distance = dist;
    return true;
}

bool checkCylinderOverlapSphere(const Vec3& cylBot, const Vec3& cylTop, const Vec3& spherePos, float radii, CollisionResult& result)
{
    Vec3 a = cylTop - cylBot;
    const float v7 = std::sqrt(dot(a, a));
    const Vec3 b = spherePos - cylBot;
    if (v7 <= 0) {
        if (dot(b, b) <= radii * radii) {
            result.field0 = 0;
            result.flags = 0;
            result.distance = 0;
            result.position = cylBot;
            result.plane[0] = 1;
            result.plane[1] = 0;
            result.plane[2] = 0;
            return true;
        }
        return false;
    }
    a = a * (1.0f / v7);
    const float v12 = dot(a, b);
    if (v12 < -radii || v12 > v7 + radii) {
        return false;
    }
    const Vec3 c = b - a * v12;
    const float v15 = dot(c, c);
    if (v15 > radii * radii) {
        return false;
    }
    result.field0 = 0;
    result.flags = 0;
    const Vec3 pos = spherePos - c - a * std::sqrt(radii * radii - v15);
    result.position = pos;
    result.distance = std::clamp(v12 / (v7 + 2 * radii), 0.0f, 1.0f);
    Vec3 normal = pos - spherePos;
    const float len = std::sqrt(dot(normal, normal));
    if (len > 0) {
        normal = normal * (1.0f / len);
    }
    result.plane[0] = normal[0];
    result.plane[1] = normal[1];
    result.plane[2] = normal[2];
    return true;
}

bool checkCylindersOverlap(const Vec3& oneBottom, const Vec3& oneTop, const Vec3& twoBottom, const Vec3& twoVector, float twoDot,
    float radii, CollisionResult& result)
{
    float v9 = 0;
    float v10 = 1;
    const float v11 = dot(oneBottom - twoBottom, twoVector);
    const float v12 = dot(oneTop - twoBottom, twoVector);
    if (v11 >= 0) {
        if (v11 > twoDot) {
            if (v12 > twoDot) {
                return false;
            }
            v9 = v12 <= v11 ? (v11 - twoDot) / (v11 - v12) : (v11 - twoDot) / (v12 - v11);
        }
    } else {
        if (v12 < 0) {
            return false;
        }
        v9 = v12 <= v11 ? -v11 / (v11 - v12) : -v11 / (v12 - v11);
    }
    if (v12 >= 0) {
        if (v12 > twoDot) {
            v10 = v12 <= v11 ? 1 - (v12 - twoDot) / (v11 - v12) : 1 - (v12 - twoDot) / (v12 - v11);
        }
    } else if (v12 <= v11) {
        v10 = 1 - (-v12 / (v11 - v12));
    } else {
        v10 = 1 - (-v12 / (v12 - v11));
    }
    Vec3 d = oneTop - oneBottom;
    const Vec3 c = d - twoVector * (v12 - v11);
    const Vec3 e = twoBottom + twoVector * v11;
    const float v15 = dot(c, c);
    const float v16 = dot(c, e - oneBottom);
    float v17 = v16 / v15;
    if (v17 >= v9) {
        if (v17 > v10) {
            v17 = v10;
        }
    } else {
        v17 = v9;
    }
    const Vec3 h = oneBottom + c * v17 - e;
    const float v19 = dot(h, h);
    if (v19 > radii * radii) {
        return false;
    }
    float v22 = v17 - (radii - std::sqrt(v19)) / std::sqrt(v15);
    if (v22 >= v9) {
        if (v22 > v10) {
            v22 = v10;
        }
    } else {
        v22 = v9;
    }
    result.field0 = 0;
    result.flags = 0;
    result.position = oneBottom + d * v22;
    result.distance = v22;
    const float len = std::sqrt(dot(d, d));
    if (len > 0) {
        d = d * (1.0f / len);
    } else {
        d = {1, 0, 0};
    }
    result.plane[0] = -d[0];
    result.plane[1] = -d[1];
    result.plane[2] = -d[2];
    return true;
}

} // namespace fp
