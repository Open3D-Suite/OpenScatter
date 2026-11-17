// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tests of the OpenScatter core: shared helpers.

#pragma once

#include <bit>
#include <cstdint>
#include <cstring>
#include <ostream>
#include <unordered_map>
#include <vector>

#include "osc/osc.h"

namespace osc
{

// So that a failed check prints what the status was.
inline std::ostream& operator<<(std::ostream& out, ScatterStatus status)
{
    return out << statusMessage(status);
}

} // namespace osc

namespace helpers
{

// A pattern for the tests that do not care which one: built once, 4 096 points.
inline const osc::Pattern& testPattern()
{
    static const osc::Pattern pattern = osc::makeProgressiveBlueNoise(4096, 7);
    return pattern;
}

// The pattern the product uses by default: 16 384 points, seed 2026.
inline const osc::Pattern& defaultPattern()
{
    static const osc::Pattern pattern = osc::makeProgressiveBlueNoise(16384, 2026);
    return pattern;
}

// A rectangle of two triangles at height z, facing up.
inline osc::TriMesh quad(float minX, float minY, float maxX, float maxY, float z = 0.0f)
{
    osc::TriMesh mesh;
    mesh.vertices = { { minX, minY, z }, { maxX, minY, z }, { maxX, maxY, z }, { minX, maxY, z } };
    mesh.indices = { 0, 1, 2, 0, 2, 3 };
    return mesh;
}

// A rectangle whose height is z = z0 + slopeX * x + slopeY * y.
inline osc::TriMesh plane(float minX, float minY, float maxX, float maxY, float z0, float slopeX, float slopeY)
{
    osc::TriMesh mesh = quad(minX, minY, maxX, maxY);
    for (osc::Vec3& v : mesh.vertices)
        v.z = z0 + slopeX * v.x + slopeY * v.y;
    return mesh;
}

inline osc::Polyline rectangle(float minX, float minY, float maxX, float maxY)
{
    osc::Polyline ring;
    ring.points = { { minX, minY }, { maxX, minY }, { maxX, maxY }, { minX, maxY } };
    return ring;
}

inline osc::Area area(osc::AreaMode mode, std::vector<osc::Polyline> shapes, float nearDistance = 0.0f,
                      float farDistance = 0.0f, float width = 0.0f)
{
    osc::Area a;
    a.mode = mode;
    a.shapes = std::move(shapes);
    a.falloffNear = nearDistance;
    a.falloffFar = farDistance;
    a.width = width;
    return a;
}

// Settings for a scatter over a flat rectangle that uses a given share of the test pattern.
inline osc::ScatterSettings flatSettings(const osc::Pattern& pattern, float side, float share = 1.0f,
                                         float tileSize = 20.0f)
{
    osc::ScatterSettings s;
    s.tileSize = tileSize;
    s.density = osc::patternDensity(pattern.size(), tileSize) * share;
    s.domain = { 0.0f, 0.0f, side, side };
    return s;
}

inline std::vector<osc::Instance> flatten(const osc::ScatterResult& result)
{
    std::vector<osc::Instance> all;
    all.reserve(result.count());
    for (const auto& tile : result.tiles)
        all.insert(all.end(), tile.begin(), tile.end());
    return all;
}

inline std::unordered_map<std::uint64_t, osc::Instance> byId(const osc::ScatterResult& result)
{
    std::unordered_map<std::uint64_t, osc::Instance> map;
    map.reserve(result.count());
    for (const auto& tile : result.tiles)
        for (const osc::Instance& inst : tile)
            map.emplace(inst.id, inst);
    return map;
}

inline bool sameBits(osc::Vec3 a, osc::Vec3 b)
{
    return std::bit_cast<std::uint32_t>(a.x) == std::bit_cast<std::uint32_t>(b.x) &&
           std::bit_cast<std::uint32_t>(a.y) == std::bit_cast<std::uint32_t>(b.y) &&
           std::bit_cast<std::uint32_t>(a.z) == std::bit_cast<std::uint32_t>(b.z);
}

// Bit for bit the same instance.
inline bool same(const osc::Instance& a, const osc::Instance& b)
{
    return a.id == b.id && a.model == b.model &&
           std::bit_cast<std::uint32_t>(a.rank) == std::bit_cast<std::uint32_t>(b.rank) &&
           sameBits(a.transform.x, b.transform.x) && sameBits(a.transform.y, b.transform.y) &&
           sameBits(a.transform.z, b.transform.z) && sameBits(a.transform.t, b.transform.t);
}

// How many instances of "before" are still in "after", and how many of those are bit for bit the same.
struct Survivors
{
    std::size_t kept = 0;
    std::size_t identical = 0;
};

inline Survivors survivors(const osc::ScatterResult& before, const osc::ScatterResult& after)
{
    const auto later = byId(after);
    Survivors s;
    for (const auto& tile : before.tiles)
        for (const osc::Instance& inst : tile)
        {
            const auto it = later.find(inst.id);
            if (it == later.end())
                continue;
            ++s.kept;
            if (same(inst, it->second))
                ++s.identical;
        }
    return s;
}

} // namespace helpers
