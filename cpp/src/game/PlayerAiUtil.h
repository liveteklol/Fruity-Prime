#pragma once

// Vector helpers the bot code uses, named after the OpenTK calls it was ported from.
#include "Collision.h"

#include <cmath>

namespace fp::ai {

inline float lengthSquared(const Vec3& v) { return dot(v, v); }
inline float length(const Vec3& v) { return std::sqrt(dot(v, v)); }
inline float distanceSquared(const Vec3& a, const Vec3& b) { return lengthSquared(a - b); }
inline float distance(const Vec3& a, const Vec3& b) { return length(a - b); }
inline bool isZero(const Vec3& v) { return v[0] == 0 && v[1] == 0 && v[2] == 0; }
inline Vec3 normalized(const Vec3& v)
{
    const float len = length(v);
    return len > 0 ? v * (1 / len) : v;
}
inline Vec3 withY(Vec3 v, float y)
{
    v[1] = y;
    return v;
}
inline Vec3 addX(Vec3 v, float x)
{
    v[0] += x;
    return v;
}
inline Vec3 addY(Vec3 v, float y)
{
    v[1] += y;
    return v;
}
inline Vec3 addZ(Vec3 v, float z)
{
    v[2] += z;
    return v;
}
inline Vec3 cross(const Vec3& a, const Vec3& b)
{
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
inline Vec3 operator-(const Vec3& v) { return {-v[0], -v[1], -v[2]}; }
inline Vec3 operator/(const Vec3& v, float s) { return {v[0] / s, v[1] / s, v[2] / s}; }
inline float fx(int32_t raw) { return raw / 4096.0f; }
inline int toFx(float value) { return static_cast<int>(value * 4096); }
inline float sign(float value) { return value > 0 ? 1.0f : value < 0 ? -1.0f : 0.0f; }
inline constexpr Vec3 UnitX{1, 0, 0};
inline constexpr Vec3 UnitY{0, 1, 0};
inline constexpr Vec3 UnitZ{0, 0, 1};

} // namespace fp::ai
