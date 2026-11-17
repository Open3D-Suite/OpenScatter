// SPDX-License-Identifier: GPL-3.0-or-later
//
// OpenScatter core - projection onto a surface along Z.

#include "osc/surface_index.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace osc
{

namespace
{

// How far outside a triangle, in barycentric units, a point still counts as on it. The test runs in
// double precision on float coordinates, so this only has to cover points exactly on a shared edge.
constexpr double kEdgeTolerance = 1e-9;

// The grid may hold this many triangle references per usable triangle (plus a little); a mesh whose
// triangles overlap many cells each gets a coarser grid instead of an index that eats the memory.
constexpr std::uint64_t kReferencesPerTriangle = 16;

} // namespace

std::size_t SurfaceIndex::cellX(float x) const
{
    const double c = (static_cast<double>(x) - static_cast<double>(m_bounds.minX)) * m_scaleX;
    const std::size_t i = c > 0.0 ? static_cast<std::size_t>(c) : 0;
    return i < m_nx ? i : m_nx - 1;
}

std::size_t SurfaceIndex::cellY(float y) const
{
    const double c = (static_cast<double>(y) - static_cast<double>(m_bounds.minY)) * m_scaleY;
    const std::size_t i = c > 0.0 ? static_cast<std::size_t>(c) : 0;
    return i < m_ny ? i : m_ny - 1;
}

void SurfaceIndex::build(TriMesh mesh)
{
    m_mesh = std::move(mesh);
    m_bounds = Rect{};
    m_minZ = m_maxZ = 0.0f;
    m_usable = 0;
    m_nx = m_ny = 0;
    m_scaleX = m_scaleY = 0.0;
    m_start.clear();
    m_tris.clear();

    const std::vector<Vec3>& verts = m_mesh.vertices;
    const std::vector<std::uint32_t>& idx = m_mesh.indices;
    const std::size_t numTris = std::min<std::size_t>(idx.size() / 3, std::numeric_limits<std::uint32_t>::max());

    // 1. The triangles that can be hit from above, and their extent.
    std::vector<std::uint32_t> usable;
    usable.reserve(numTris);
    float minX = std::numeric_limits<float>::max();
    float minY = minX;
    float minZ = minX;
    float maxX = -minX;
    float maxY = -minX;
    float maxZ = -minX;
    for (std::size_t t = 0; t < numTris; ++t)
    {
        const std::uint32_t i0 = idx[t * 3];
        const std::uint32_t i1 = idx[t * 3 + 1];
        const std::uint32_t i2 = idx[t * 3 + 2];
        if (i0 >= verts.size() || i1 >= verts.size() || i2 >= verts.size())
            continue;
        const Vec3& a = verts[i0];
        const Vec3& b = verts[i1];
        const Vec3& c = verts[i2];
        if (!isFinite(a) || !isFinite(b) || !isFinite(c))
            continue;
        const double det = (static_cast<double>(b.x) - a.x) * (static_cast<double>(c.y) - a.y) -
                           (static_cast<double>(c.x) - a.x) * (static_cast<double>(b.y) - a.y);
        if (!(det != 0.0))
            continue; // seen from above it is a line or a point
        usable.push_back(static_cast<std::uint32_t>(t));
        for (const Vec3* v : { &a, &b, &c })
        {
            minX = v->x < minX ? v->x : minX;
            minY = v->y < minY ? v->y : minY;
            minZ = v->z < minZ ? v->z : minZ;
            maxX = v->x > maxX ? v->x : maxX;
            maxY = v->y > maxY ? v->y : maxY;
            maxZ = v->z > maxZ ? v->z : maxZ;
        }
    }
    if (usable.empty())
        return;

    m_usable = usable.size();
    m_bounds = Rect{ minX, minY, maxX, maxY };
    m_minZ = minZ;
    m_maxZ = maxZ;

    // 2. A grid with about one cell for every two triangles, made coarser while the triangles would
    //    need more references than the budget allows.
    const double extentX = static_cast<double>(maxX) - minX; // both above 0: some triangle has area
    const double extentY = static_cast<double>(maxY) - minY;
    const double wanted = std::max(1.0, static_cast<double>(m_usable) * 0.5);
    const double cell = std::sqrt(extentX * extentY / wanted);
    const auto cellCount = [cell](double extent) {
        const double n = cell > 0.0 ? std::ceil(extent / cell) : 1.0;
        return static_cast<std::size_t>(std::clamp(n, 1.0, 32768.0));
    };
    m_nx = cellCount(extentX);
    m_ny = cellCount(extentY);

    // The cells each usable triangle's bounding box covers (at most 32 768 a side: 16 bits each),
    // worked out once per grid tried.
    struct CellRange
    {
        std::uint16_t x0, x1, y0, y1;
    };
    std::vector<CellRange> ranges(m_usable);

    const std::uint64_t budget =
        std::min<std::uint64_t>(kReferencesPerTriangle * m_usable + 4096, std::numeric_limits<std::uint32_t>::max());
    for (;;)
    {
        m_scaleX = static_cast<double>(m_nx) / extentX;
        m_scaleY = static_cast<double>(m_ny) / extentY;
        std::uint64_t references = 0;
        for (std::size_t k = 0; k < m_usable; ++k)
        {
            const std::size_t t = usable[k];
            const Vec3& a = verts[idx[t * 3]];
            const Vec3& b = verts[idx[t * 3 + 1]];
            const Vec3& c = verts[idx[t * 3 + 2]];
            const float loX = a.x < b.x ? (a.x < c.x ? a.x : c.x) : (b.x < c.x ? b.x : c.x);
            const float hiX = a.x > b.x ? (a.x > c.x ? a.x : c.x) : (b.x > c.x ? b.x : c.x);
            const float loY = a.y < b.y ? (a.y < c.y ? a.y : c.y) : (b.y < c.y ? b.y : c.y);
            const float hiY = a.y > b.y ? (a.y > c.y ? a.y : c.y) : (b.y > c.y ? b.y : c.y);
            CellRange& r = ranges[k];
            r.x0 = static_cast<std::uint16_t>(cellX(loX));
            r.x1 = static_cast<std::uint16_t>(cellX(hiX));
            r.y0 = static_cast<std::uint16_t>(cellY(loY));
            r.y1 = static_cast<std::uint16_t>(cellY(hiY));
            references += static_cast<std::uint64_t>(r.x1 - r.x0 + 1) * static_cast<std::uint64_t>(r.y1 - r.y0 + 1);
        }
        if (references <= budget || (m_nx == 1 && m_ny == 1))
            break;
        m_nx = std::max<std::size_t>(1, m_nx / 2);
        m_ny = std::max<std::size_t>(1, m_ny / 2);
    }

    // 3. Counting sort of the references into the cells; within a cell, in triangle order.
    m_start.assign(m_nx * m_ny + 1, 0);
    for (const CellRange& r : ranges)
        for (std::size_t y = r.y0; y <= r.y1; ++y)
            for (std::size_t x = r.x0; x <= r.x1; ++x)
                ++m_start[y * m_nx + x + 1];
    for (std::size_t i = 1; i < m_start.size(); ++i)
        m_start[i] += m_start[i - 1];
    m_tris.resize(m_start.back());
    std::vector<std::uint32_t> fill(m_start.begin(), m_start.end() - 1);
    for (std::size_t k = 0; k < m_usable; ++k)
    {
        const CellRange& r = ranges[k];
        for (std::size_t y = r.y0; y <= r.y1; ++y)
            for (std::size_t x = r.x0; x <= r.x1; ++x)
                m_tris[fill[y * m_nx + x]++] = usable[k];
    }
}

bool SurfaceIndex::project(float x, float y, SurfaceHit& hit) const
{
    if (m_usable == 0)
        return false;
    if (!(x >= m_bounds.minX && x <= m_bounds.maxX && y >= m_bounds.minY && y <= m_bounds.maxY))
        return false; // also when x or y is not a number

    const std::vector<Vec3>& verts = m_mesh.vertices;
    const std::vector<std::uint32_t>& idx = m_mesh.indices;
    const std::size_t cell = cellY(y) * m_nx + cellX(x);
    const double px = x;
    const double py = y;

    bool found = false;
    double bestZ = 0.0;
    double bestU = 0.0;
    double bestV = 0.0;
    std::uint32_t bestTriangle = 0;
    for (std::uint32_t k = m_start[cell]; k < m_start[cell + 1]; ++k)
    {
        const std::uint32_t t = m_tris[k];
        const Vec3& a = verts[idx[static_cast<std::size_t>(t) * 3]];
        const Vec3& b = verts[idx[static_cast<std::size_t>(t) * 3 + 1]];
        const Vec3& c = verts[idx[static_cast<std::size_t>(t) * 3 + 2]];
        const double e1x = static_cast<double>(b.x) - a.x;
        const double e1y = static_cast<double>(b.y) - a.y;
        const double e2x = static_cast<double>(c.x) - a.x;
        const double e2y = static_cast<double>(c.y) - a.y;
        const double det = e1x * e2y - e2x * e1y; // never 0: build() left those out
        const double dx = px - a.x;
        const double dy = py - a.y;
        const double u = (dx * e2y - e2x * dy) / det;
        const double v = (e1x * dy - dx * e1y) / det;
        if (!(u >= -kEdgeTolerance && v >= -kEdgeTolerance && u + v <= 1.0 + kEdgeTolerance))
            continue;
        const double z = a.z + u * (static_cast<double>(b.z) - a.z) + v * (static_cast<double>(c.z) - a.z);
        if (!found || z > bestZ)
        {
            found = true;
            bestZ = z;
            bestU = u;
            bestV = v;
            bestTriangle = t;
        }
    }
    if (!found)
        return false;

    const Vec3& a = verts[idx[static_cast<std::size_t>(bestTriangle) * 3]];
    const Vec3& b = verts[idx[static_cast<std::size_t>(bestTriangle) * 3 + 1]];
    const Vec3& c = verts[idx[static_cast<std::size_t>(bestTriangle) * 3 + 2]];
    const double e1x = static_cast<double>(b.x) - a.x;
    const double e1y = static_cast<double>(b.y) - a.y;
    const double e1z = static_cast<double>(b.z) - a.z;
    const double e2x = static_cast<double>(c.x) - a.x;
    const double e2y = static_cast<double>(c.y) - a.y;
    const double e2z = static_cast<double>(c.z) - a.z;
    double nx = e1y * e2z - e1z * e2y;
    double ny = e1z * e2x - e1x * e2z;
    double nz = e1x * e2y - e1y * e2x; // the same "det": never 0
    const double inv = (nz < 0.0 ? -1.0 : 1.0) / std::sqrt(nx * nx + ny * ny + nz * nz);
    nx *= inv;
    ny *= inv;
    nz *= inv;

    hit.z = static_cast<float>(bestZ);
    hit.normal = { static_cast<float>(nx), static_cast<float>(ny), static_cast<float>(nz) };
    hit.triangle = bestTriangle;
    hit.b1 = static_cast<float>(bestU);
    hit.b2 = static_cast<float>(bestV);
    return true;
}

Vec2 SurfaceIndex::uvAt(const SurfaceHit& hit) const
{
    if (!m_mesh.hasUVs() || hit.triangle >= m_mesh.triangleCount())
        return {};
    const Vec2& a = m_mesh.cornerUVs[static_cast<std::size_t>(hit.triangle) * 3];
    const Vec2& b = m_mesh.cornerUVs[static_cast<std::size_t>(hit.triangle) * 3 + 1];
    const Vec2& c = m_mesh.cornerUVs[static_cast<std::size_t>(hit.triangle) * 3 + 2];
    const float b0 = 1.0f - hit.b1 - hit.b2;
    return { a.x * b0 + b.x * hit.b1 + c.x * hit.b2, a.y * b0 + b.y * hit.b1 + c.y * hit.b2 };
}

} // namespace osc
