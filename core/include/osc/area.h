// SPDX-License-Identifier: GPL-3.0-or-later
//
// OpenScatter core - include and exclude areas.
//
// An area is a set of shapes drawn in the XY plane (the host flattens its splines to polylines and
// projects them there). Instances live in the union of the include areas - everywhere when there is
// none - minus the union of the exclude areas. Areas only decide where the pattern shows: editing one
// never moves the instances that remain.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "osc/math.h"

namespace osc
{

struct Polyline
{
    std::vector<Vec2> points;
    bool closed = true;   // closed: the last point joins the first and the shape has an inside
};

enum class AreaMode
{
    Include,
    Exclude,
};

// The inside of an area is what its closed shapes enclose (even-odd: a shape inside another makes a
// hole) plus a band of "width" centred on its open shapes.
//
// Its effect fades with the distance to the edge of that inside, negative within it and positive
// outside: the effect is full up to "falloffNear" and gone from "falloffFar" on, linear in between.
// Near = far = 0 is a hard edge; near = 0, far = 5 fades out over 5 units outside the edge;
// near = -5, far = 0 fades over the last 5 units inside it.
struct Area
{
    AreaMode mode = AreaMode::Include;
    std::vector<Polyline> shapes;
    float width = 0.0f;
    float falloffNear = 0.0f;
    float falloffFar = 0.0f;
};

// Areas prepared for millions of queries. Immutable once built: weight() can be called from any
// number of threads.
class AreaSet
{
public:
    AreaSet() = default;
    explicit AreaSet(const std::vector<Area>& areas);

    // No areas at all: weight() is 1 everywhere.
    bool empty() const { return m_areas.empty(); }

    bool hasIncludes() const { return m_includes > 0; }

    // A rectangle outside which no include area has any effect. Empty when hasIncludes() is false,
    // and also when the include areas enclose nothing (then nothing is included anywhere).
    const Rect& includeBounds() const { return m_includeBounds; }

    // The share of the density that remains at (x, y): 0 = no instances, 1 = untouched.
    float weight(float x, float y) const;

private:
    struct Edge
    {
        float ax, ay, bx, by;
        std::uint32_t closed;   // 1: part of a closed shape; 0: part of an open one
    };

    struct Compiled
    {
        AreaMode mode = AreaMode::Include;
        bool hasClosed = false;
        bool hasOpen = false;
        bool needDistance = false;
        double halfWidth = 0.0;
        double nearDistance = 0.0;
        double farDistance = 0.0;

        // The grid: everything the area can affect. Outside it the weight is 0.
        double minX = 0.0, minY = 0.0, maxX = 0.0, maxY = 0.0;
        std::size_t nx = 0, ny = 0;
        double scaleX = 0.0, scaleY = 0.0;        // cells per unit
        std::vector<Edge> edges;
        std::vector<std::uint32_t> cellStart;     // per cell, the edges close enough to matter there
        std::vector<std::uint32_t> cellEdges;
        std::vector<std::uint8_t> cellInside;     // whether the centre of the cell is inside

        // Bands in Y for the inside test: the closed edges that cross each band.
        std::size_t rows = 0;
        double rowScale = 0.0;
        std::vector<std::uint32_t> rowStart;
        std::vector<std::uint32_t> rowEdges;

        void build(const Area& area);
        bool inside(double x, double y) const;
        float weight(double x, double y) const;
    };

    std::vector<Compiled> m_areas;   // the include areas first
    std::size_t m_includes = 0;
    Rect m_includeBounds;
};

} // namespace osc
