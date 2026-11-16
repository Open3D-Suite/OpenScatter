// SPDX-License-Identifier: GPL-3.0-or-later
//
// OpenScatter core - the triangle mesh the host hands over.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "osc/math.h"

namespace osc
{

// A triangle mesh in the scatter's local space. Nothing in it is trusted: indices out of range and
// vertices that are not finite are skipped wherever the mesh is used.
struct TriMesh
{
    std::vector<Vec3> vertices;
    std::vector<std::uint32_t> indices;   // three per triangle
    std::vector<Vec2> cornerUVs;          // optional: three per triangle, in the order of "indices"

    std::size_t triangleCount() const { return indices.size() / 3; }
    bool hasUVs() const { return !indices.empty() && cornerUVs.size() == triangleCount() * 3; }
};

} // namespace osc
