// SPDX-License-Identifier: GPL-3.0-or-later
//
// OpenScatter core - include and exclude areas.
//
// Each area is compiled into two small indexes so that a query does not walk all its edges:
//   - a grid over everything the area can affect; each cell lists the edges close enough for their
//     distance to matter there, and a cell with no such edge has one answer for all its points;
//   - bands in Y listing the closed edges that cross them, for the inside test (a ray towards +X only
//     meets edges that span the point's Y).
// All of it runs in double precision on the float coordinates, with plain arithmetic and square roots.

#include "osc/area.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace osc
{

namespace
{

// The indexes may hold this many edge references per edge (plus a little); beyond that they are made
// coarser, down to a single cell that lists every edge.
constexpr std::uint64_t kReferencesPerEdge = 32;
constexpr double kMaxCellsPerAxis = 2048.0;

constexpr double kInfinity = std::numeric_limits<double>::infinity();

// Which of "count" bands of a grid starting at "origin" a coordinate falls in. Never out of range.
std::size_t bandOf(double v, double origin, double scale, std::size_t count)
{
    const double c = (v - origin) * scale;
    if (!(c > 0.0))
        return 0;
    if (c >= static_cast<double>(count))
        return count - 1;
    return static_cast<std::size_t>(c);
}

std::uint64_t referenceBudget(std::size_t edges)
{
    return std::min<std::uint64_t>(kReferencesPerEdge * edges + 4096, std::numeric_limits<std::uint32_t>::max());
}

template <class EdgeT>
double crossingX(const EdgeT& e, double y)
{
    return e.ax + (y - e.ay) * (static_cast<double>(e.bx) - e.ax) / (static_cast<double>(e.by) - e.ay);
}

// Whether a ray from a point at height y towards +X can meet the edge. Half-open, so a ray through a
// vertex counts the two edges that share it once in total.
template <class EdgeT>
bool spans(const EdgeT& e, double y)
{
    return (e.ay <= y) != (e.by <= y);
}

template <class EdgeT>
double distanceSquared(const EdgeT& e, double x, double y)
{
    const double ex = static_cast<double>(e.bx) - e.ax;
    const double ey = static_cast<double>(e.by) - e.ay;
    const double px = x - e.ax;
    const double py = y - e.ay;
    const double len2 = ex * ex + ey * ey;
    double t = len2 > 0.0 ? (px * ex + py * ey) / len2 : 0.0;
    t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
    const double dx = px - t * ex;
    const double dy = py - t * ey;
    return dx * dx + dy * dy;
}

float outwardDown(double v)
{
    const double limit = std::numeric_limits<float>::max();
    return std::nextafter(static_cast<float>(std::clamp(v, -limit, limit)), -std::numeric_limits<float>::infinity());
}

float outwardUp(double v)
{
    const double limit = std::numeric_limits<float>::max();
    return std::nextafter(static_cast<float>(std::clamp(v, -limit, limit)), std::numeric_limits<float>::infinity());
}

} // namespace

void AreaSet::Compiled::build(const Area& area)
{
    mode = area.mode;
    halfWidth = (std::isfinite(area.width) && area.width > 0.0f) ? area.width * 0.5 : 0.0;
    nearDistance = std::isfinite(area.falloffNear) ? area.falloffNear : 0.0;
    farDistance = std::isfinite(area.falloffFar) ? area.falloffFar : 0.0;
    if (farDistance < nearDistance)
        farDistance = nearDistance;

    // 1. Edges. Points that are not numbers and repeated points are dropped; a closed shape needs
    //    three points to enclose anything.
    const std::size_t maxEdges = std::numeric_limits<std::uint32_t>::max() / 2;
    std::vector<Vec2> pts;
    for (const Polyline& shape : area.shapes)
    {
        pts.clear();
        for (const Vec2& p : shape.points)
        {
            if (!isFinite(p))
                continue;
            if (!pts.empty() && pts.back().x == p.x && pts.back().y == p.y)
                continue;
            pts.push_back(p);
        }
        if (shape.closed && pts.size() >= 2 && pts.front().x == pts.back().x && pts.front().y == pts.back().y)
            pts.pop_back();
        if (pts.empty() || edges.size() + pts.size() > maxEdges)
            continue;

        if (shape.closed)
        {
            if (pts.size() < 3)
                continue;
            for (std::size_t i = 0; i < pts.size(); ++i)
            {
                const Vec2& a = pts[i];
                const Vec2& b = pts[(i + 1) % pts.size()];
                edges.push_back({ a.x, a.y, b.x, b.y, 1u });
            }
            hasClosed = true;
        }
        else
        {
            if (pts.size() == 1)
                edges.push_back({ pts[0].x, pts[0].y, pts[0].x, pts[0].y, 0u });
            for (std::size_t i = 0; i + 1 < pts.size(); ++i)
                edges.push_back({ pts[i].x, pts[i].y, pts[i + 1].x, pts[i + 1].y, 0u });
            hasOpen = true;
        }
    }
    if (edges.empty())
        return;
    needDistance = hasOpen || nearDistance != 0.0 || farDistance != 0.0;

    // 2. What the area can affect: its edges, grown by how far outside them the effect reaches.
    const double band = hasOpen ? halfWidth : 0.0;
    const double outer = std::max(0.0, farDistance) + band;
    // Up to this distance from an edge the exact distance decides the weight; beyond it only the side.
    const double reach = std::max(std::fabs(nearDistance), std::fabs(farDistance)) + band;

    minX = minY = kInfinity;
    maxX = maxY = -kInfinity;
    for (const Edge& e : edges)
    {
        minX = std::min({ minX, static_cast<double>(e.ax), static_cast<double>(e.bx) });
        minY = std::min({ minY, static_cast<double>(e.ay), static_cast<double>(e.by) });
        maxX = std::max({ maxX, static_cast<double>(e.ax), static_cast<double>(e.bx) });
        maxY = std::max({ maxY, static_cast<double>(e.ay), static_cast<double>(e.by) });
    }
    minX -= outer;
    minY -= outer;
    maxX += outer;
    maxY += outer;
    const double extentX = maxX - minX;
    const double extentY = maxY - minY;

    // 3. The grid: about four cells per edge, coarser while the edges would need too many references.
    const double wanted = std::clamp(4.0 * static_cast<double>(edges.size()), 64.0, 1048576.0);
    if (extentX > 0.0 && extentY > 0.0)
    {
        const double cell = std::sqrt(extentX * extentY / wanted);
        nx = static_cast<std::size_t>(std::clamp(std::ceil(extentX / cell), 1.0, kMaxCellsPerAxis));
        ny = static_cast<std::size_t>(std::clamp(std::ceil(extentY / cell), 1.0, kMaxCellsPerAxis));
    }
    else
    {
        // Everything on one line: a row or a column of cells.
        const std::size_t along = static_cast<std::size_t>(std::min(wanted, kMaxCellsPerAxis));
        nx = extentX > 0.0 ? along : 1;
        ny = extentY > 0.0 ? along : 1;
    }

    struct CellRange
    {
        std::size_t x0, x1, y0, y1;
    };
    const auto rangeOf = [&](const Edge& e) {
        return CellRange{ bandOf(std::min<double>(e.ax, e.bx) - reach, minX, scaleX, nx),
                          bandOf(std::max<double>(e.ax, e.bx) + reach, minX, scaleX, nx),
                          bandOf(std::min<double>(e.ay, e.by) - reach, minY, scaleY, ny),
                          bandOf(std::max<double>(e.ay, e.by) + reach, minY, scaleY, ny) };
    };
    const std::uint64_t budget = referenceBudget(edges.size());
    for (;;)
    {
        scaleX = extentX > 0.0 ? static_cast<double>(nx) / extentX : 0.0;
        scaleY = extentY > 0.0 ? static_cast<double>(ny) / extentY : 0.0;
        std::uint64_t references = 0;
        for (const Edge& e : edges)
        {
            const CellRange r = rangeOf(e);
            references += static_cast<std::uint64_t>(r.x1 - r.x0 + 1) * (r.y1 - r.y0 + 1);
        }
        if (references <= budget || (nx == 1 && ny == 1))
            break;
        nx = std::max<std::size_t>(1, nx / 2);
        ny = std::max<std::size_t>(1, ny / 2);
    }

    cellStart.assign(nx * ny + 1, 0);
    for (const Edge& e : edges)
    {
        const CellRange r = rangeOf(e);
        for (std::size_t y = r.y0; y <= r.y1; ++y)
            for (std::size_t x = r.x0; x <= r.x1; ++x)
                ++cellStart[y * nx + x + 1];
    }
    for (std::size_t i = 1; i < cellStart.size(); ++i)
        cellStart[i] += cellStart[i - 1];
    cellEdges.resize(cellStart.back());
    {
        std::vector<std::uint32_t> fill(cellStart.begin(), cellStart.end() - 1);
        for (std::size_t i = 0; i < edges.size(); ++i)
        {
            const CellRange r = rangeOf(edges[i]);
            for (std::size_t y = r.y0; y <= r.y1; ++y)
                for (std::size_t x = r.x0; x <= r.x1; ++x)
                    cellEdges[fill[y * nx + x]++] = static_cast<std::uint32_t>(i);
        }
    }

    cellInside.assign(nx * ny, 0);
    if (!hasClosed)
        return;

    // 4. The bands for the inside test, with the same kind of budget.
    rows = ny;
    for (;;)
    {
        rowScale = extentY > 0.0 ? static_cast<double>(rows) / extentY : 0.0;
        std::uint64_t references = 0;
        for (const Edge& e : edges)
            if (e.closed)
                references += bandOf(std::max<double>(e.ay, e.by), minY, rowScale, rows)
                            - bandOf(std::min<double>(e.ay, e.by), minY, rowScale, rows) + 1;
        if (references <= budget || rows == 1)
            break;
        rows = std::max<std::size_t>(1, rows / 2);
    }
    rowStart.assign(rows + 1, 0);
    for (const Edge& e : edges)
        if (e.closed)
            for (std::size_t r = bandOf(std::min<double>(e.ay, e.by), minY, rowScale, rows),
                             last = bandOf(std::max<double>(e.ay, e.by), minY, rowScale, rows);
                 r <= last; ++r)
                ++rowStart[r + 1];
    for (std::size_t i = 1; i < rowStart.size(); ++i)
        rowStart[i] += rowStart[i - 1];
    rowEdges.resize(rowStart.back());
    {
        std::vector<std::uint32_t> fill(rowStart.begin(), rowStart.end() - 1);
        for (std::size_t i = 0; i < edges.size(); ++i)
        {
            const Edge& e = edges[i];
            if (!e.closed)
                continue;
            for (std::size_t r = bandOf(std::min<double>(e.ay, e.by), minY, rowScale, rows),
                             last = bandOf(std::max<double>(e.ay, e.by), minY, rowScale, rows);
                 r <= last; ++r)
                rowEdges[fill[r]++] = static_cast<std::uint32_t>(i);
        }
    }

    // 5. Whether the centre of each cell is inside: one sweep per row of cells over the sorted
    //    crossings of that row's centre line. Only the cells with no edge nearby rely on it.
    std::vector<double> crossings;
    for (std::size_t iy = 0; iy < ny; ++iy)
    {
        const double cy = scaleY > 0.0 ? minY + (static_cast<double>(iy) + 0.5) / scaleY : minY;
        const std::size_t row = bandOf(cy, minY, rowScale, rows);
        crossings.clear();
        for (std::uint32_t k = rowStart[row]; k < rowStart[row + 1]; ++k)
        {
            const Edge& e = edges[rowEdges[k]];
            if (spans(e, cy))
                crossings.push_back(crossingX(e, cy));
        }
        std::sort(crossings.begin(), crossings.end());
        std::size_t passed = 0;
        for (std::size_t ix = 0; ix < nx; ++ix)
        {
            const double cx = scaleX > 0.0 ? minX + (static_cast<double>(ix) + 0.5) / scaleX : minX;
            while (passed < crossings.size() && crossings[passed] <= cx)
                ++passed;
            cellInside[iy * nx + ix] = static_cast<std::uint8_t>((crossings.size() - passed) & 1u);
        }
    }
}

bool AreaSet::Compiled::inside(double x, double y) const
{
    if (!hasClosed)
        return false;
    const std::size_t row = bandOf(y, minY, rowScale, rows);
    bool in = false;
    for (std::uint32_t k = rowStart[row]; k < rowStart[row + 1]; ++k)
    {
        const Edge& e = edges[rowEdges[k]];
        if (spans(e, y) && x < crossingX(e, y))
            in = !in;
    }
    return in;
}

float AreaSet::Compiled::weight(double x, double y) const
{
    if (edges.empty())
        return 0.0f;
    if (!(x >= minX && x <= maxX && y >= minY && y <= maxY))
        return 0.0f;   // out of reach, or not a number

    const std::size_t cell = bandOf(y, minY, scaleY, ny) * nx + bandOf(x, minX, scaleX, nx);
    const std::uint32_t begin = cellStart[cell];
    const std::uint32_t end = cellStart[cell + 1];
    if (begin == end)
        return cellInside[cell] ? 1.0f : 0.0f;   // no edge near: deep inside or far outside

    const bool in = inside(x, y);
    if (!needDistance)
        return in ? 1.0f : 0.0f;

    double closed2 = kInfinity;
    double open2 = kInfinity;
    for (std::uint32_t k = begin; k < end; ++k)
    {
        const Edge& e = edges[cellEdges[k]];
        const double d2 = distanceSquared(e, x, y);
        if (e.closed)
            closed2 = std::min(closed2, d2);
        else
            open2 = std::min(open2, d2);
    }

    // Distance to the edge of the area's inside: negative within it.
    double signedDistance = kInfinity;
    if (hasClosed)
    {
        const double d = std::sqrt(closed2);
        signedDistance = in ? -d : d;
    }
    if (hasOpen)
        signedDistance = std::min(signedDistance, std::sqrt(open2) - halfWidth);

    if (signedDistance <= nearDistance)
        return 1.0f;
    if (signedDistance >= farDistance)
        return 0.0f;
    return static_cast<float>((farDistance - signedDistance) / (farDistance - nearDistance));
}

AreaSet::AreaSet(const std::vector<Area>& areas)
{
    m_areas.reserve(areas.size());
    for (const AreaMode mode : { AreaMode::Include, AreaMode::Exclude })
    {
        for (const Area& area : areas)
        {
            if (area.mode != mode)
                continue;
            m_areas.emplace_back();
            m_areas.back().build(area);
        }
        if (mode == AreaMode::Include)
            m_includes = m_areas.size();
    }
    for (std::size_t i = 0; i < m_includes; ++i)
    {
        const Compiled& c = m_areas[i];
        if (c.edges.empty())
            continue;
        m_includeBounds = unite(m_includeBounds, Rect{ outwardDown(c.minX), outwardDown(c.minY),
                                                       outwardUp(c.maxX), outwardUp(c.maxY) });
    }
}

float AreaSet::weight(float x, float y) const
{
    float include = 1.0f;
    if (m_includes > 0)
    {
        include = 0.0f;
        for (std::size_t i = 0; i < m_includes && include < 1.0f; ++i)
            include = std::max(include, m_areas[i].weight(x, y));
        if (!(include > 0.0f))
            return 0.0f;
    }
    float exclude = 0.0f;
    for (std::size_t i = m_includes; i < m_areas.size(); ++i)
    {
        exclude = std::max(exclude, m_areas[i].weight(x, y));
        if (exclude >= 1.0f)
            return 0.0f;
    }
    return include * (1.0f - exclude);
}

} // namespace osc
