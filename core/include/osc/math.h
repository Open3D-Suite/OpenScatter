// SPDX-License-Identifier: GPL-3.0-or-later
//
// OpenScatter core - small vector types.
//
// Everything in the core works in one space: the local space of the scatter, with Z up. The host (the
// 3ds Max plugin) brings meshes, splines and maps into that space and takes the instances out of it.

#pragma once

#include <cmath>

namespace osc
{

struct Vec2
{
    float x = 0.0f;
    float y = 0.0f;
};

struct Vec3
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

inline Vec3 operator+(Vec3 a, Vec3 b)
{
    return { a.x + b.x, a.y + b.y, a.z + b.z };
}
inline Vec3 operator-(Vec3 a, Vec3 b)
{
    return { a.x - b.x, a.y - b.y, a.z - b.z };
}
inline Vec3 operator*(Vec3 a, float s)
{
    return { a.x * s, a.y * s, a.z * s };
}
inline float dot(Vec3 a, Vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline Vec3 cross(Vec3 a, Vec3 b)
{
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}
inline float length(Vec3 a)
{
    return std::sqrt(dot(a, a));
}

inline bool isFinite(float v)
{
    return std::isfinite(v);
}
inline bool isFinite(Vec2 v)
{
    return std::isfinite(v.x) && std::isfinite(v.y);
}
inline bool isFinite(Vec3 v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// 0 for anything that is not above 0 (including NaN), 1 for anything above 1.
inline float clamp01(float v)
{
    return !(v > 0.0f) ? 0.0f : (v > 1.0f ? 1.0f : v);
}

// An axis-aligned rectangle in the XY plane. Empty when max < min on either axis.
struct Rect
{
    float minX = 0.0f;
    float minY = 0.0f;
    float maxX = -1.0f;
    float maxY = -1.0f;

    bool empty() const
    {
        return !(maxX >= minX && maxY >= minY);
    }
    bool finite() const
    {
        return std::isfinite(minX) && std::isfinite(minY) && std::isfinite(maxX) && std::isfinite(maxY);
    }
};

inline Rect intersect(const Rect& a, const Rect& b)
{
    if (a.empty() || b.empty())
        return Rect{};
    Rect r;
    r.minX = a.minX > b.minX ? a.minX : b.minX;
    r.minY = a.minY > b.minY ? a.minY : b.minY;
    r.maxX = a.maxX < b.maxX ? a.maxX : b.maxX;
    r.maxY = a.maxY < b.maxY ? a.maxY : b.maxY;
    return r;
}

inline Rect unite(const Rect& a, const Rect& b)
{
    if (a.empty())
        return b;
    if (b.empty())
        return a;
    Rect r;
    r.minX = a.minX < b.minX ? a.minX : b.minX;
    r.minY = a.minY < b.minY ? a.minY : b.minY;
    r.maxX = a.maxX > b.maxX ? a.maxX : b.maxX;
    r.maxY = a.maxY > b.maxY ? a.maxY : b.maxY;
    return r;
}

// An affine transform as three axis vectors and a translation: a point p of the model goes to
// x * p.x + y * p.y + z * p.z + t. The axes carry rotation and scale; there is never any shear.
struct Transform
{
    Vec3 x{ 1.0f, 0.0f, 0.0f };
    Vec3 y{ 0.0f, 1.0f, 0.0f };
    Vec3 z{ 0.0f, 0.0f, 1.0f };
    Vec3 t{ 0.0f, 0.0f, 0.0f };
};

inline Vec3 apply(const Transform& m, Vec3 p)
{
    return m.x * p.x + m.y * p.y + m.z * p.z + m.t;
}

} // namespace osc
