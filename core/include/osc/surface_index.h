// SPDX-License-Identifier: GPL-3.0-or-later
//
// OpenScatter core - projection onto a surface along Z.
//
// The planar scatter drops every candidate straight down onto the surface. Along a fixed axis that
// needs no ray tracer: the triangles are binned by their XY bounding box into a grid, and a query looks
// only at the triangles of one cell. One code path, plain arithmetic, the same result on every CPU.

#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "osc/math.h"
#include "osc/mesh.h"

namespace osc
{

struct SurfaceHit
{
    float z = 0.0f;
    Vec3 normal{ 0.0f, 0.0f, 1.0f };   // of the triangle, unit length, always facing up (z > 0)
    std::uint32_t triangle = 0;        // index of the triangle in the mesh
    float b1 = 0.0f;                   // weight of the triangle's second corner
    float b2 = 0.0f;                   // weight of its third corner (the first has 1 - b1 - b2)
};

class SurfaceIndex
{
public:
    SurfaceIndex() = default;
    explicit SurfaceIndex(TriMesh mesh) { build(std::move(mesh)); }

    // Takes the mesh (move it in to avoid a copy) and indexes it. Triangles with an index out of
    // range, a vertex that is not finite or no area seen from above are left out.
    void build(TriMesh mesh);

    bool empty() const { return m_usable == 0; }
    std::size_t usableTriangles() const { return m_usable; }
    const TriMesh& mesh() const { return m_mesh; }

    // XY extent and height range of the usable triangles. Empty when there are none.
    const Rect& bounds() const { return m_bounds; }
    float minZ() const { return m_minZ; }
    float maxZ() const { return m_maxZ; }

    // The highest point of the surface above or below (x, y). False when there is none.
    // Safe to call from any number of threads.
    bool project(float x, float y, SurfaceHit& hit) const;

    // Texture coordinates at a hit, from the mesh's corner UVs. (0, 0) when the mesh has none.
    bool hasUVs() const { return m_mesh.hasUVs(); }
    Vec2 uvAt(const SurfaceHit& hit) const;

private:
    std::size_t cellX(float x) const;
    std::size_t cellY(float y) const;

    TriMesh m_mesh;
    Rect m_bounds;
    float m_minZ = 0.0f;
    float m_maxZ = 0.0f;
    std::size_t m_usable = 0;
    std::size_t m_nx = 0;
    std::size_t m_ny = 0;
    double m_scaleX = 0.0;                // cells per unit
    double m_scaleY = 0.0;
    std::vector<std::uint32_t> m_start;   // per cell, where its triangles begin in m_tris
    std::vector<std::uint32_t> m_tris;
};

} // namespace osc
