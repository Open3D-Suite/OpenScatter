// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tests of the OpenScatter core: include and exclude areas.

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "helpers.h"

using helpers::area;
using helpers::rectangle;
using osc::AreaMode;

namespace
{

// The weight of one area worked out the slow way, straight from the definition: every edge, no index.
float weightBruteForce(const osc::Area& a, double x, double y)
{
    const double inf = std::numeric_limits<double>::infinity();
    bool inside = false;
    double closed2 = inf;
    double open2 = inf;
    bool hasClosed = false;
    bool hasOpen = false;
    const auto distance2 = [&](osc::Vec2 p, osc::Vec2 q) {
        const double ex = double(q.x) - p.x, ey = double(q.y) - p.y;
        const double px = x - p.x, py = y - p.y;
        const double len2 = ex * ex + ey * ey;
        const double t = len2 > 0.0 ? std::clamp((px * ex + py * ey) / len2, 0.0, 1.0) : 0.0;
        return (px - t * ex) * (px - t * ex) + (py - t * ey) * (py - t * ey);
    };
    for (const osc::Polyline& shape : a.shapes)
    {
        const std::size_t n = shape.points.size();
        if (shape.closed)
        {
            if (n < 3)
                continue;
            hasClosed = true;
            for (std::size_t i = 0; i < n; ++i)
            {
                const osc::Vec2 p = shape.points[i];
                const osc::Vec2 q = shape.points[(i + 1) % n];
                closed2 = std::min(closed2, distance2(p, q));
                if ((p.y <= y) != (q.y <= y) && x < p.x + (y - p.y) * (double(q.x) - p.x) / (double(q.y) - p.y))
                    inside = !inside;
            }
        }
        else if (n >= 1)
        {
            hasOpen = true;
            for (std::size_t i = 0; i + 1 < std::max<std::size_t>(n, 2); ++i)
                open2 = std::min(open2, distance2(shape.points[i], shape.points[std::min(i + 1, n - 1)]));
        }
    }
    double signedDistance = inf;
    if (hasClosed)
        signedDistance = inside ? -std::sqrt(closed2) : std::sqrt(closed2);
    if (hasOpen)
        signedDistance = std::min(signedDistance, std::sqrt(open2) - std::max(0.0f, a.width) * 0.5);
    const double nearDistance = a.falloffNear;
    const double farDistance = std::max(a.falloffNear, a.falloffFar);
    if (!hasClosed && !hasOpen)
        return 0.0f;
    if (signedDistance <= nearDistance)
        return 1.0f;
    if (signedDistance >= farDistance)
        return 0.0f;
    return static_cast<float>((farDistance - signedDistance) / (farDistance - nearDistance));
}

// A closed shape with "points" corners around a centre, each at a random distance: star-like, concave.
osc::Polyline star(float cx, float cy, float radius, int points, std::uint64_t seed)
{
    osc::Polyline ring;
    for (int i = 0; i < points; ++i)
    {
        double s = 0.0, c = 1.0;
        osc::sinCosTurns(static_cast<double>(i) / points, s, c);
        const float r = radius * (0.35f + 0.65f * osc::unitFloat(osc::hashCombine(seed, static_cast<std::uint64_t>(i))));
        ring.points.push_back({ cx + r * static_cast<float>(c), cy + r * static_cast<float>(s) });
    }
    return ring;
}

// Points anywhere, joined in the order they come: crosses itself all over.
osc::Polyline tangle(float size, int points, std::uint64_t seed, bool closed)
{
    osc::Polyline line;
    line.closed = closed;
    for (int i = 0; i < points; ++i)
        line.points.push_back({ size * osc::unitFloat(osc::hashCombine(seed, static_cast<std::uint64_t>(i) * 2)),
                                size * osc::unitFloat(osc::hashCombine(seed, static_cast<std::uint64_t>(i) * 2 + 1)) });
    return line;
}

} // namespace

TEST_CASE("areas: with none, everything is in")
{
    const osc::AreaSet none;
    CHECK(none.empty());
    CHECK_FALSE(none.hasIncludes());
    CHECK(none.includeBounds().empty());
    CHECK(none.weight(123.0f, -456.0f) == 1.0f);

    const osc::AreaSet alsoNone{ std::vector<osc::Area>{} };
    CHECK(alsoNone.empty());
    CHECK(alsoNone.weight(0.0f, 0.0f) == 1.0f);
}

TEST_CASE("areas: an include area keeps what is inside it")
{
    const osc::AreaSet set({ area(AreaMode::Include, { rectangle(10.0f, 20.0f, 30.0f, 50.0f) }) });
    CHECK_FALSE(set.empty());
    CHECK(set.hasIncludes());
    CHECK(set.weight(20.0f, 35.0f) == 1.0f);
    CHECK(set.weight(10.5f, 20.5f) == 1.0f);
    CHECK(set.weight(29.5f, 49.5f) == 1.0f);
    CHECK(set.weight(9.5f, 35.0f) == 0.0f);
    CHECK(set.weight(20.0f, 50.5f) == 0.0f);
    CHECK(set.weight(-1000.0f, 1000.0f) == 0.0f);

    const osc::Rect bounds = set.includeBounds();
    CHECK(bounds.minX <= 10.0f);
    CHECK(bounds.minX > 9.99f);
    CHECK(bounds.maxY >= 50.0f);
    CHECK(bounds.maxY < 50.01f);
}

TEST_CASE("areas: an exclude area removes what is inside it")
{
    const osc::AreaSet set({ area(AreaMode::Exclude, { rectangle(10.0f, 20.0f, 30.0f, 50.0f) }) });
    CHECK_FALSE(set.hasIncludes());
    CHECK(set.includeBounds().empty());
    CHECK(set.weight(20.0f, 35.0f) == 0.0f);
    CHECK(set.weight(9.5f, 35.0f) == 1.0f);
    CHECK(set.weight(-1000.0f, 1000.0f) == 1.0f);
}

TEST_CASE("areas: includes add up, excludes take away from the sum")
{
    const osc::AreaSet set({
        area(AreaMode::Exclude, { rectangle(15.0f, 0.0f, 25.0f, 10.0f) }),
        area(AreaMode::Include, { rectangle(0.0f, 0.0f, 20.0f, 10.0f) }),
        area(AreaMode::Include, { rectangle(18.0f, 0.0f, 40.0f, 10.0f) }),
        area(AreaMode::Exclude, { rectangle(35.0f, 5.0f, 50.0f, 20.0f) }),
    });
    CHECK(set.weight(5.0f, 5.0f) == 1.0f);     // first include only
    CHECK(set.weight(19.0f, 5.0f) == 0.0f);    // both includes, but excluded
    CHECK(set.weight(30.0f, 5.5f) == 1.0f);    // second include only
    CHECK(set.weight(37.0f, 2.0f) == 1.0f);
    CHECK(set.weight(37.0f, 7.0f) == 0.0f);    // second exclude
    CHECK(set.weight(45.0f, 2.0f) == 0.0f);    // in no include
    const osc::Rect bounds = set.includeBounds();
    CHECK(bounds.minX <= 0.0f);
    CHECK(bounds.maxX >= 40.0f);
    CHECK(bounds.maxX < 40.01f);               // the exclude areas do not count
}

TEST_CASE("areas: a shape inside another makes a hole, shapes apart are both in")
{
    const osc::AreaSet ring({ area(AreaMode::Include, { rectangle(0.0f, 0.0f, 30.0f, 30.0f),
                                                        rectangle(10.0f, 10.0f, 20.0f, 20.0f) }) });
    CHECK(ring.weight(5.0f, 5.0f) == 1.0f);
    CHECK(ring.weight(15.0f, 15.0f) == 0.0f);
    CHECK(ring.weight(25.0f, 15.0f) == 1.0f);

    const osc::AreaSet islands({ area(AreaMode::Include, { rectangle(0.0f, 0.0f, 10.0f, 10.0f),
                                                           rectangle(50.0f, 50.0f, 60.0f, 60.0f) }) });
    CHECK(islands.weight(5.0f, 5.0f) == 1.0f);
    CHECK(islands.weight(55.0f, 55.0f) == 1.0f);
    CHECK(islands.weight(30.0f, 30.0f) == 0.0f);
}

TEST_CASE("areas: the winding of a shape does not matter")
{
    osc::Polyline clockwise = rectangle(0.0f, 0.0f, 10.0f, 10.0f);
    std::reverse(clockwise.points.begin(), clockwise.points.end());
    const osc::AreaSet set({ area(AreaMode::Include, { clockwise }) });
    CHECK(set.weight(5.0f, 5.0f) == 1.0f);
    CHECK(set.weight(15.0f, 5.0f) == 0.0f);
}

TEST_CASE("areas: falloff outside the edge")
{
    // Full inside, fading out over 4 units outside.
    const osc::AreaSet set({ area(AreaMode::Include, { rectangle(0.0f, 0.0f, 100.0f, 100.0f) }, 0.0f, 4.0f) });
    CHECK(set.weight(50.0f, 50.0f) == 1.0f);
    CHECK(set.weight(99.0f, 50.0f) == 1.0f);
    CHECK(set.weight(101.0f, 50.0f) == doctest::Approx(0.75f));
    CHECK(set.weight(102.0f, 50.0f) == doctest::Approx(0.5f));
    CHECK(set.weight(50.0f, -3.0f) == doctest::Approx(0.25f));
    CHECK(set.weight(104.5f, 50.0f) == 0.0f);
    // Beyond a corner the distance is to the corner.
    CHECK(set.weight(103.0f, 104.0f) == 0.0f);                       // 5 away
    CHECK(set.weight(101.2f, 101.6f) == doctest::Approx(0.5f));      // 2 away
    // The bounds reach as far as the falloff.
    CHECK(set.includeBounds().minX <= -4.0f);
    CHECK(set.includeBounds().maxY >= 104.0f);
}

TEST_CASE("areas: falloff inside the edge")
{
    // Fades over the last 10 units before the edge; nothing outside.
    const osc::AreaSet set({ area(AreaMode::Include, { rectangle(0.0f, 0.0f, 100.0f, 100.0f) }, -10.0f, 0.0f) });
    CHECK(set.weight(50.0f, 50.0f) == 1.0f);
    CHECK(set.weight(10.0f, 50.0f) == doctest::Approx(1.0f));
    CHECK(set.weight(5.0f, 50.0f) == doctest::Approx(0.5f));
    CHECK(set.weight(50.0f, 97.5f) == doctest::Approx(0.25f));
    CHECK(set.weight(-0.5f, 50.0f) == 0.0f);
    CHECK(set.weight(3.0f, 4.0f) == doctest::Approx(0.3f));          // near a corner: the closest edge counts
}

TEST_CASE("areas: falloff across the edge, and on an exclude area")
{
    const osc::AreaSet across({ area(AreaMode::Include, { rectangle(0.0f, 0.0f, 100.0f, 100.0f) }, -2.0f, 2.0f) });
    CHECK(across.weight(3.0f, 50.0f) == 1.0f);
    CHECK(across.weight(1.0f, 50.0f) == doctest::Approx(0.75f));
    CHECK(across.weight(-1.0f, 50.0f) == doctest::Approx(0.25f));
    CHECK(across.weight(-2.5f, 50.0f) == 0.0f);

    // A clearing with a soft border: nothing inside, back to full 5 units out.
    const osc::AreaSet clearing({ area(AreaMode::Exclude, { rectangle(40.0f, 40.0f, 60.0f, 60.0f) }, 0.0f, 5.0f) });
    CHECK(clearing.weight(50.0f, 50.0f) == 0.0f);
    CHECK(clearing.weight(62.0f, 50.0f) == doctest::Approx(0.4f));
    CHECK(clearing.weight(66.0f, 50.0f) == 1.0f);

    // Far before near is read as no falloff at near.
    const osc::AreaSet swapped({ area(AreaMode::Include, { rectangle(0.0f, 0.0f, 10.0f, 10.0f) }, 3.0f, 1.0f) });
    CHECK(swapped.weight(12.0f, 5.0f) == 1.0f);
    CHECK(swapped.weight(13.5f, 5.0f) == 0.0f);
}

TEST_CASE("areas: open shapes are bands")
{
    osc::Polyline path;
    path.closed = false;
    path.points = { { 0.0f, 0.0f }, { 100.0f, 0.0f }, { 100.0f, 50.0f } };

    SUBCASE("a band with a hard edge")
    {
        const osc::AreaSet road({ area(AreaMode::Exclude, { path }, 0.0f, 0.0f, 6.0f) });
        CHECK(road.weight(50.0f, 2.0f) == 0.0f);
        CHECK(road.weight(50.0f, -2.9f) == 0.0f);
        CHECK(road.weight(50.0f, 3.1f) == 1.0f);
        CHECK(road.weight(98.0f, 30.0f) == 0.0f);
        CHECK(road.weight(96.0f, 30.0f) == 1.0f);
        CHECK(road.weight(-2.0f, 0.0f) == 0.0f);        // round ends
        CHECK(road.weight(-4.0f, 0.0f) == 1.0f);
    }
    SUBCASE("a band with soft shoulders")
    {
        const osc::AreaSet road({ area(AreaMode::Exclude, { path }, 0.0f, 4.0f, 6.0f) });
        CHECK(road.weight(50.0f, 2.0f) == 0.0f);
        CHECK(road.weight(50.0f, 5.0f) == doctest::Approx(0.5f));
        CHECK(road.weight(50.0f, 7.5f) == 1.0f);
    }
    SUBCASE("no width: only the falloff around the line")
    {
        const osc::AreaSet line({ area(AreaMode::Include, { path }, 1.0f, 3.0f) });
        CHECK(line.weight(50.0f, 0.5f) == 1.0f);
        CHECK(line.weight(50.0f, 2.0f) == doctest::Approx(0.5f));
        CHECK(line.weight(50.0f, 3.5f) == 0.0f);
        const osc::AreaSet nothing({ area(AreaMode::Include, { path }) });
        CHECK(nothing.weight(50.0f, 1.0f) == 0.0f);
    }
    SUBCASE("a single point is a disc")
    {
        osc::Polyline dot;
        dot.closed = false;
        dot.points = { { 5.0f, 5.0f } };
        const osc::AreaSet disc({ area(AreaMode::Include, { dot }, 0.0f, 0.0f, 4.0f) });
        CHECK(disc.weight(6.0f, 6.0f) == 1.0f);
        CHECK(disc.weight(7.5f, 5.0f) == 0.0f);
    }
    SUBCASE("open and closed shapes in one area")
    {
        const osc::AreaSet both({ area(AreaMode::Include, { path, rectangle(200.0f, 0.0f, 220.0f, 20.0f) }, 0.0f, 0.0f, 6.0f) });
        CHECK(both.weight(50.0f, 2.0f) == 1.0f);
        CHECK(both.weight(210.0f, 10.0f) == 1.0f);
        CHECK(both.weight(150.0f, 10.0f) == 0.0f);
    }
}

TEST_CASE("areas: shapes that enclose nothing")
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    osc::Polyline two;
    two.points = { { 0.0f, 0.0f }, { 10.0f, 10.0f } };
    osc::Polyline flat;
    flat.points = { { 0.0f, 0.0f }, { 5.0f, 0.0f }, { 10.0f, 0.0f }, { 20.0f, 0.0f } };
    osc::Polyline samePoint;
    samePoint.points = { { 3.0f, 3.0f }, { 3.0f, 3.0f }, { 3.0f, 3.0f }, { 3.0f, 3.0f } };
    osc::Polyline notNumbers;
    notNumbers.points = { { nan, 0.0f }, { 0.0f, nan }, { nan, nan } };
    const osc::Polyline empty;

    for (const osc::Polyline& shape : { two, flat, samePoint, notNumbers, empty })
    {
        // As an include area: the area exists, so "everything" no longer applies, and it holds nothing.
        const osc::AreaSet include({ area(AreaMode::Include, { shape }) });
        CHECK(include.hasIncludes());
        CHECK(include.weight(3.0f, 3.0f) == 0.0f);
        CHECK(include.weight(5.0f, 0.0f) == 0.0f);
        CHECK(include.weight(100.0f, 100.0f) == 0.0f);
        // As an exclude area it removes nothing.
        const osc::AreaSet exclude({ area(AreaMode::Exclude, { shape }) });
        CHECK(exclude.weight(3.0f, 3.0f) == 1.0f);
        CHECK(exclude.weight(5.0f, 0.0f) == 1.0f);
    }

    // An area with no shapes at all behaves the same way.
    const osc::AreaSet bare({ area(AreaMode::Include, {}) });
    CHECK(bare.hasIncludes());
    CHECK(bare.includeBounds().empty());
    CHECK(bare.weight(0.0f, 0.0f) == 0.0f);
}

TEST_CASE("areas: points that are not numbers or repeat are skipped, the rest of the shape stands")
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    osc::Polyline messy;
    messy.points = { { 0.0f, 0.0f }, { 0.0f, 0.0f }, { nan, 5.0f }, { 10.0f, 0.0f }, { 10.0f, inf },
                     { 10.0f, 10.0f }, { 10.0f, 10.0f }, { 0.0f, 10.0f }, { 0.0f, 0.0f } };
    osc::Area a = area(AreaMode::Include, { messy });
    a.width = nan;
    a.falloffNear = nan;
    a.falloffFar = inf;
    const osc::AreaSet set({ a });
    CHECK(set.weight(5.0f, 5.0f) == 1.0f);
    CHECK(set.weight(15.0f, 5.0f) == 0.0f);
    CHECK(set.weight(nan, 5.0f) == 0.0f);
    CHECK(set.weight(5.0f, inf) == 0.0f);
}

TEST_CASE("areas: the prepared areas agree with the definition")
{
    // Random concave and self-crossing shapes, with every kind of falloff, against the slow way.
    std::size_t checked = 0;
    std::size_t partial = 0;
    for (std::uint64_t scene = 0; scene < 60; ++scene)
    {
        osc::Area a;
        a.mode = AreaMode::Include;
        const std::uint64_t kind = scene % 6;
        if (kind == 0)
            a.shapes = { star(50.0f, 50.0f, 45.0f, 40, scene) };
        else if (kind == 1)
            a.shapes = { star(50.0f, 50.0f, 45.0f, 700, scene), star(50.0f, 50.0f, 12.0f, 9, scene + 1) };
        else if (kind == 2)
            a.shapes = { tangle(100.0f, 25, scene, true) };
        else if (kind == 3)
            a.shapes = { tangle(100.0f, 30, scene, false) };
        else if (kind == 4)
            a.shapes = { tangle(100.0f, 12, scene, false), star(30.0f, 60.0f, 25.0f, 15, scene),
                         star(75.0f, 30.0f, 20.0f, 6, scene + 2) };
        else
            a.shapes = { star(50.0f, 50.0f, 45.0f, 5, scene), rectangle(48.0f, 48.0f, 52.0f, 52.0f) };

        const std::uint64_t falloff = (scene / 6) % 5;
        if (falloff == 1)
            a.falloffFar = 6.0f;
        else if (falloff == 2)
            a.falloffNear = -5.0f;
        else if (falloff == 3)
        {
            a.falloffNear = -3.0f;
            a.falloffFar = 40.0f;   // longer than most of the shape
        }
        else if (falloff == 4)
        {
            a.falloffNear = 1.0f;
            a.falloffFar = 1.5f;
        }
        a.width = (kind == 3 || kind == 4) ? 3.0f + static_cast<float>(scene % 4) : 0.0f;

        const osc::AreaSet set({ a });
        for (std::uint64_t i = 0; i < 1500; ++i)
        {
            const float x = osc::unitFloat(osc::hashCombine(scene * 2 + 100, i)) * 160.0f - 30.0f;
            const float y = osc::unitFloat(osc::hashCombine(scene * 2 + 101, i)) * 160.0f - 30.0f;
            const float expected = weightBruteForce(a, x, y);
            const float got = set.weight(x, y);
            if (std::fabs(got - expected) > 1e-5f)
            {
                CAPTURE(scene);
                CAPTURE(x);
                CAPTURE(y);
                CHECK(got == doctest::Approx(expected));
            }
            ++checked;
            partial += (expected > 0.0f && expected < 1.0f) ? 1 : 0;
        }
    }
    CHECK(checked == 60 * 1500);
    CHECK(partial > 2000);   // the falloff zones were really exercised
}

TEST_CASE("areas: a shape with very many edges, and huge coordinates")
{
    // A circle of 50 000 sides.
    osc::Polyline ring;
    for (int i = 0; i < 50000; ++i)
    {
        double s = 0.0, c = 1.0;
        osc::sinCosTurns(i / 50000.0, s, c);
        ring.points.push_back({ static_cast<float>(1000.0 * c), static_cast<float>(1000.0 * s) });
    }
    const osc::AreaSet circle({ area(AreaMode::Include, { ring }, -50.0f, 0.0f) });
    CHECK(circle.weight(0.0f, 0.0f) == 1.0f);
    CHECK(circle.weight(940.0f, 0.0f) == 1.0f);
    CHECK(circle.weight(0.0f, 975.0f) == doctest::Approx(0.5f).epsilon(0.001));
    CHECK(circle.weight(1001.0f, 0.0f) == 0.0f);
    CHECK(circle.weight(-710.0f, -710.0f) == 0.0f);   // just outside, on the diagonal

    const float big = 1e30f;
    const osc::AreaSet huge({ area(AreaMode::Include, { rectangle(-big, -big, big, big) }, 0.0f, 1e29f) });
    CHECK(huge.weight(0.0f, 0.0f) == 1.0f);
    CHECK(huge.weight(big * 1.05f, 0.0f) == doctest::Approx(0.5f).epsilon(0.01));
    CHECK(huge.includeBounds().finite());
}
