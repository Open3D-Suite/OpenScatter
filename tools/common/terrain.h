// SPDX-License-Identifier: GPL-3.0-or-later
//
// A procedural terrain for the benchmark and the tests: a square grid of triangles whose heights come
// from value noise. Built with hashes and plain arithmetic only, so it is the same on every machine.

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

#include "osc/hash.h"
#include "osc/mesh.h"

namespace osc::tools
{

inline float valueNoise(float x, float y, std::uint64_t seed)
{
    const float fx = std::floor(x);
    const float fy = std::floor(y);
    const std::int64_t ix = static_cast<std::int64_t>(fx);
    const std::int64_t iy = static_cast<std::int64_t>(fy);
    const float tx = x - fx;
    const float ty = y - fy;
    const auto corner = [seed](std::int64_t cx, std::int64_t cy) {
        return unitFloat(
            hashCombine(hashCombine(seed, static_cast<std::uint64_t>(cx)), static_cast<std::uint64_t>(cy)));
    };
    const float sx = tx * tx * (3.0f - 2.0f * tx);
    const float sy = ty * ty * (3.0f - 2.0f * ty);
    const float a = corner(ix, iy) + (corner(ix + 1, iy) - corner(ix, iy)) * sx;
    const float b = corner(ix, iy + 1) + (corner(ix + 1, iy + 1) - corner(ix, iy + 1)) * sx;
    return a + (b - a) * sy;
}

// Five octaves of value noise, in [0, 1).
inline float fbm(float x, float y, std::uint64_t seed)
{
    float sum = 0.0f;
    float amplitude = 0.5f;
    for (std::uint64_t octave = 0; octave < 5; ++octave)
    {
        sum += amplitude * valueNoise(x, y, seed + octave);
        x *= 2.0f;
        y *= 2.0f;
        amplitude *= 0.5f;
    }
    return sum;
}

// A terrain of side "size" starting at the origin, with resolution x resolution x 2 triangles and
// heights up to "height". With UVs, the texture coordinates run from 0 to 1 across it.
inline TriMesh makeTerrain(float size, int resolution, float height, std::uint64_t seed, bool withUVs = false)
{
    TriMesh mesh;
    if (resolution < 1)
        return mesh;
    const std::size_t n = static_cast<std::size_t>(resolution) + 1;
    mesh.vertices.resize(n * n);
    for (std::size_t y = 0; y < n; ++y)
        for (std::size_t x = 0; x < n; ++x)
        {
            const float px = size * static_cast<float>(x) / static_cast<float>(resolution);
            const float py = size * static_cast<float>(y) / static_cast<float>(resolution);
            mesh.vertices[y * n + x] = { px, py, height * fbm(px / size * 6.0f, py / size * 6.0f, seed) };
        }
    const std::size_t cells = static_cast<std::size_t>(resolution);
    mesh.indices.reserve(cells * cells * 6);
    if (withUVs)
        mesh.cornerUVs.reserve(cells * cells * 6);
    for (std::size_t y = 0; y < cells; ++y)
        for (std::size_t x = 0; x < cells; ++x)
        {
            const std::uint32_t a = static_cast<std::uint32_t>(y * n + x);
            const std::uint32_t b = a + 1;
            const std::uint32_t c = a + static_cast<std::uint32_t>(n);
            const std::uint32_t d = c + 1;
            for (const std::uint32_t corner : { a, b, d, a, d, c })
            {
                mesh.indices.push_back(corner);
                if (withUVs)
                {
                    const Vec3& v = mesh.vertices[corner];
                    mesh.cornerUVs.push_back({ v.x / size, v.y / size });
                }
            }
        }
    return mesh;
}

} // namespace osc::tools
