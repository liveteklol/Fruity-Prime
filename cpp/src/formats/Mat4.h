#pragma once

#include <array>
#include <cmath>

namespace fp {

// A 4x4 matrix with OpenTK's conventions: row-major storage, row vectors,
// translation in row 3, and A * B meaning "A, then B". Keeping the C#
// semantics lets transform code be ported line for line. The memory layout is
// what the GL renderer uploaded as-is, so GLSL reads it the same way here.
struct Mat4 {
    float m[4][4]{};

    static Mat4 identity()
    {
        Mat4 r;
        for (int i = 0; i < 4; i++) {
            r.m[i][i] = 1.0f;
        }
        return r;
    }

    static Mat4 scale(float x, float y, float z)
    {
        Mat4 r = identity();
        r.m[0][0] = x;
        r.m[1][1] = y;
        r.m[2][2] = z;
        return r;
    }

    static Mat4 scale(float s) { return scale(s, s, s); }

    static Mat4 translation(float x, float y, float z)
    {
        Mat4 r = identity();
        r.m[3][0] = x;
        r.m[3][1] = y;
        r.m[3][2] = z;
        return r;
    }

    static Mat4 rotationX(float a)
    {
        Mat4 r = identity();
        const float c = std::cos(a), s = std::sin(a);
        r.m[1][1] = c;
        r.m[1][2] = s;
        r.m[2][1] = -s;
        r.m[2][2] = c;
        return r;
    }

    static Mat4 rotationY(float a)
    {
        Mat4 r = identity();
        const float c = std::cos(a), s = std::sin(a);
        r.m[0][0] = c;
        r.m[0][2] = -s;
        r.m[2][0] = s;
        r.m[2][2] = c;
        return r;
    }

    static Mat4 rotationZ(float a)
    {
        Mat4 r = identity();
        const float c = std::cos(a), s = std::sin(a);
        r.m[0][0] = c;
        r.m[0][1] = s;
        r.m[1][0] = -s;
        r.m[1][1] = c;
        return r;
    }

    // EntityBase.GetTransformMatrix
    static Mat4 fromVectors(std::array<float, 3> facing, std::array<float, 3> up, std::array<float, 3> position)
    {
        auto cross = [](const std::array<float, 3>& a, const std::array<float, 3>& b) {
            return std::array<float, 3>{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
        };
        std::array<float, 3> right = cross(up, facing);
        const float len = std::sqrt(right[0] * right[0] + right[1] * right[1] + right[2] * right[2]);
        if (len > 0) {
            for (float& v : right) {
                v /= len;
            }
        }
        up = cross(facing, right);
        Mat4 r;
        for (int i = 0; i < 3; i++) {
            r.m[0][i] = right[i];
            r.m[1][i] = up[i];
            r.m[2][i] = facing[i];
            r.m[3][i] = position[i];
        }
        r.m[3][3] = 1.0f;
        return r;
    }

    Mat4 operator*(const Mat4& b) const
    {
        Mat4 r;
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                float sum = 0;
                for (int k = 0; k < 4; k++) {
                    sum += m[i][k] * b.m[k][j];
                }
                r.m[i][j] = sum;
            }
        }
        return r;
    }

    Mat4& operator*=(const Mat4& b) { return *this = *this * b; }

    // OpenTK ClearRotation: keeps scale magnitudes and translation.
    Mat4 clearRotation() const
    {
        Mat4 r = identity();
        for (int i = 0; i < 3; i++) {
            r.m[i][i] = std::sqrt(m[i][0] * m[i][0] + m[i][1] * m[i][1] + m[i][2] * m[i][2]);
        }
        r.m[3][0] = m[3][0];
        r.m[3][1] = m[3][1];
        r.m[3][2] = m[3][2];
        return r;
    }

    std::array<float, 3> translationPart() const { return {m[3][0], m[3][1], m[3][2]}; }

    const float* data() const { return &m[0][0]; }
};

} // namespace fp
