// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tests of the OpenScatter core: the planar scatter - placement, filters, models and transforms.

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <vector>

#include "helpers.h"
#include "terrain.h"

using helpers::flatSettings;
using helpers::flatten;
using helpers::testPattern;
using osc::ScatterStatus;

namespace
{

const float kNaN = std::numeric_limits<float>::quiet_NaN();
const float kInf = std::numeric_limits<float>::infinity();

// The instance's axes are perpendicular to one another, and their lengths are its scale.
void checkNoShear(const osc::Transform& t)
{
    const float lx = osc::length(t.x), ly = osc::length(t.y), lz = osc::length(t.z);
    CHECK(std::fabs(osc::dot(t.x, t.y)) < 1e-4f * lx * ly);
    CHECK(std::fabs(osc::dot(t.x, t.z)) < 1e-4f * lx * lz);
    CHECK(std::fabs(osc::dot(t.y, t.z)) < 1e-4f * ly * lz);
    // Right-handed: X cross Y points along Z.
    CHECK(osc::dot(osc::cross(t.x, t.y), t.z) > 0.0f);
}

} // namespace

TEST_CASE("scatter: what cannot be scattered says why")
{
    const osc::Pattern& pattern = testPattern();
    osc::ScatterSettings s = flatSettings(pattern, 50.0f);

    CHECK(osc::scatterPlanar(osc::Pattern{}, s).status == ScatterStatus::EmptyPattern);

    for (const float tile : { 0.0f, -5.0f, kNaN, kInf })
    {
        osc::ScatterSettings bad = s;
        bad.tileSize = tile;
        CHECK(osc::scatterPlanar(pattern, bad).status == ScatterStatus::InvalidSettings);
    }
    for (const float density : { -1.0f, kNaN, kInf })
    {
        osc::ScatterSettings bad = s;
        bad.density = density;
        CHECK(osc::scatterPlanar(pattern, bad).status == ScatterStatus::InvalidSettings);
    }
    {
        osc::ScatterSettings bad = s;
        bad.domain.maxX = kInf;
        CHECK(osc::scatterPlanar(pattern, bad).status == ScatterStatus::InvalidSettings);
    }
    {
        osc::ScatterSettings bad = s;
        bad.domain = osc::Rect{}; // no rectangle, no surface, no include area
        const osc::ScatterResult r = osc::scatterPlanar(pattern, bad);
        CHECK(r.status == ScatterStatus::NoDomain);
        CHECK(r.count() == 0);
    }
    {
        osc::ScatterSettings bad = s;
        bad.tileSize = 0.001f;
        bad.density = osc::patternDensity(pattern.size(), bad.tileSize);
        bad.domain = { 0.0f, 0.0f, 1000.0f, 1000.0f }; // a million million tiles
        const osc::ScatterResult r = osc::scatterPlanar(pattern, bad);
        CHECK(r.status == ScatterStatus::TooManyTiles);
        CHECK(r.tiles.empty());
        bad = s;
        bad.domain = { -1e30f, -1e30f, 1e30f, 1e30f }; // more tiles than a number can count
        CHECK(osc::scatterPlanar(pattern, bad).status == ScatterStatus::TooManyTiles);
    }
    for (const ScatterStatus status :
         { ScatterStatus::Ok, ScatterStatus::EmptyPattern, ScatterStatus::InvalidSettings, ScatterStatus::NoDomain,
           ScatterStatus::TooManyTiles, ScatterStatus::OutOfMemory })
        CHECK(std::string(osc::statusMessage(status)).size() > 1);
}

TEST_CASE("scatter: on flat ground, the density asked for")
{
    const osc::Pattern& pattern = testPattern();
    // 100 x 100 units with tiles of 20: 25 tiles' worth of pattern, whatever the seed shifts.
    for (const float share : { 1.0f, 0.5f, 0.1f, 0.013f })
    {
        CAPTURE(share);
        const osc::ScatterSettings s = flatSettings(pattern, 100.0f, share);
        const osc::ScatterResult r = osc::scatterPlanar(pattern, s);
        REQUIRE(r.status == ScatterStatus::Ok);
        // The density comes in whole pattern points: up to half a point of 4 096 * share off.
        const double expected = 25.0 * 4096.0 * share;
        const double tolerance = 0.002 + 0.6 / (4096.0 * share);
        CHECK(static_cast<double>(r.count()) == doctest::Approx(expected).epsilon(tolerance));
        CHECK(r.stats.instances == r.count());
        CHECK(r.stats.candidates == r.count());
        CHECK(r.stats.patternShare == doctest::Approx(share).epsilon(tolerance));
        CHECK_FALSE(r.stats.densityClamped);
        CHECK(r.tiles.size() == static_cast<std::size_t>(r.tileColumns) * r.tileRows);

        std::set<std::uint64_t> ids;
        for (const auto& tile : r.tiles)
        {
            float lastRank = -1.0f;
            for (const osc::Instance& inst : tile)
            {
                const osc::Vec3 p = inst.transform.t;
                REQUIRE(p.x >= 0.0f);
                REQUIRE(p.x < 100.0f);
                REQUIRE(p.y >= 0.0f);
                REQUIRE(p.y < 100.0f);
                REQUIRE(p.z == 0.0f);
                REQUIRE(inst.rank < share);
                REQUIRE(inst.rank > lastRank); // each block is in rank order
                lastRank = inst.rank;
                REQUIRE(inst.model == 0);
                ids.insert(inst.id);
            }
        }
        CHECK(ids.size() == r.count()); // every id is different
    }
}

TEST_CASE("scatter: the instances are evenly spread, not clumped")
{
    const osc::Pattern& pattern = testPattern();
    const osc::ScatterSettings s = flatSettings(pattern, 100.0f, 0.25f);
    const std::vector<osc::Instance> all = flatten(osc::scatterPlanar(pattern, s));
    // 10 x 10 cells of 10 units: 256 instances each on average.
    int cells[100] = {};
    for (const osc::Instance& inst : all)
        ++cells[static_cast<int>(inst.transform.t.y / 10.0f) * 10 + static_cast<int>(inst.transform.t.x / 10.0f)];
    for (const int count : cells)
    {
        CHECK(count > 256 * 0.85);
        CHECK(count < 256 * 1.15);
    }
}

TEST_CASE("scatter: no density, no instances; too much density, the pattern's most")
{
    const osc::Pattern& pattern = testPattern();
    osc::ScatterSettings s = flatSettings(pattern, 100.0f);

    s.density = 0.0f;
    osc::ScatterResult r = osc::scatterPlanar(pattern, s);
    CHECK(r.status == ScatterStatus::Ok);
    CHECK(r.count() == 0);
    CHECK(r.tiles.empty());

    // Less than one instance per tile is nothing at all.
    s.density = osc::patternDensity(pattern.size(), s.tileSize) * (0.4f / 4096.0f);
    CHECK(osc::scatterPlanar(pattern, s).count() == 0);

    s.density = osc::patternDensity(pattern.size(), s.tileSize) * 3.0f;
    r = osc::scatterPlanar(pattern, s);
    CHECK(r.status == ScatterStatus::Ok);
    CHECK(r.stats.densityClamped);
    CHECK(static_cast<double>(r.count()) == doctest::Approx(25.0 * 4096.0).epsilon(0.002));

    CHECK(osc::patternDensity(4096, 20.0f) == doctest::Approx(10.24f));
    CHECK(osc::tileSizeForDensity(4096, 10.24f) == doctest::Approx(20.0f));
    CHECK(osc::patternDensity(4096, 0.0f) == 0.0f);
    CHECK(osc::patternDensity(4096, kNaN) == 0.0f);
    CHECK(osc::tileSizeForDensity(4096, 0.0f) == 0.0f);
    CHECK(osc::tileSizeForDensity(0, 1.0f) == 0.0f);
}

TEST_CASE("scatter: a domain can be split and the pieces fit together")
{
    // The instances of a region do not depend on what else is scattered: two halves give exactly the
    // instances of the whole. A host can compute by regions and put them together.
    const osc::Pattern& pattern = testPattern();
    osc::ScatterSettings s = flatSettings(pattern, 90.0f, 0.3f);
    s.transform.scale[0] = { 0.5f, 2.0f };
    const osc::ScatterResult whole = osc::scatterPlanar(pattern, s);

    osc::ScatterSettings left = s, right = s;
    left.domain = { 0.0f, 0.0f, 37.5f, 90.0f };
    right.domain = { 37.5f, 0.0f, 90.0f, 90.0f };
    const osc::ScatterResult a = osc::scatterPlanar(pattern, left);
    const osc::ScatterResult b = osc::scatterPlanar(pattern, right);

    CHECK(a.count() + b.count() == whole.count());
    const auto all = helpers::byId(whole);
    for (const osc::ScatterResult* part : { &a, &b })
        for (const auto& tile : part->tiles)
            for (const osc::Instance& inst : tile)
            {
                const auto it = all.find(inst.id);
                REQUIRE(it != all.end());
                REQUIRE(helpers::same(inst, it->second));
            }
}

TEST_CASE("scatter: another seed is another scatter")
{
    const osc::Pattern& pattern = testPattern();
    osc::ScatterSettings s = flatSettings(pattern, 60.0f, 0.2f);
    const osc::ScatterResult a = osc::scatterPlanar(pattern, s);
    const osc::ScatterResult again = osc::scatterPlanar(pattern, s);
    CHECK(osc::hashInstances(a) == osc::hashInstances(again));

    s.seed = 2;
    const osc::ScatterResult b = osc::scatterPlanar(pattern, s);
    CHECK(osc::hashInstances(a) != osc::hashInstances(b));
    CHECK(static_cast<double>(b.count()) == doctest::Approx(static_cast<double>(a.count())).epsilon(0.01));
    CHECK(helpers::survivors(a, b).kept == 0); // not one id in common

    // And another pattern, another set of ids too.
    const osc::Pattern other = osc::makeProgressiveBlueNoise(4096, 8);
    s.seed = 1;
    CHECK(helpers::survivors(a, osc::scatterPlanar(other, s)).kept == 0);
}

TEST_CASE("scatter: an empty surface gives nothing, and that is not an error")
{
    const osc::Pattern& pattern = testPattern();
    const osc::SurfaceIndex empty;
    osc::ScatterSettings s = flatSettings(pattern, 100.0f, 0.2f);
    s.surface = &empty;
    osc::ScatterResult r = osc::scatterPlanar(pattern, s);
    CHECK(r.status == ScatterStatus::Ok);
    CHECK(r.count() == 0);

    s.domain = osc::Rect{}; // not even a rectangle: still just nothing
    r = osc::scatterPlanar(pattern, s);
    CHECK(r.status == ScatterStatus::Ok);
    CHECK(r.count() == 0);
}

TEST_CASE("scatter: instances land on the surface")
{
    const osc::Pattern& pattern = testPattern();
    // z = 5 + 0.1 x - 0.2 y over 80 x 60.
    const osc::SurfaceIndex surface(helpers::plane(0.0f, 0.0f, 80.0f, 60.0f, 5.0f, 0.1f, -0.2f));
    osc::ScatterSettings s;
    s.tileSize = 20.0f;
    s.density = 1.0f;
    s.surface = &surface; // no domain: the extent of the surface

    const osc::ScatterResult r = osc::scatterPlanar(pattern, s);
    REQUIRE(r.status == ScatterStatus::Ok);
    CHECK(static_cast<double>(r.count()) == doctest::Approx(80.0 * 60.0).epsilon(0.02));
    for (const osc::Instance& inst : flatten(r))
    {
        const osc::Vec3 p = inst.transform.t;
        REQUIRE(p.z == doctest::Approx(5.0f + 0.1f * p.x - 0.2f * p.y).epsilon(1e-5));
        REQUIRE(inst.transform.z.z == 1.0f); // upright: alignToNormal is 0
    }

    // A domain larger than the surface is cut down to it; one that misses it gives nothing.
    s.domain = { -500.0f, -500.0f, 500.0f, 500.0f };
    CHECK(osc::scatterPlanar(pattern, s).count() == r.count());
    s.domain = { 200.0f, 200.0f, 300.0f, 300.0f };
    CHECK(osc::scatterPlanar(pattern, s).count() == 0);
}

TEST_CASE("scatter: holes in the surface stay empty")
{
    const osc::Pattern& pattern = testPattern();
    // Two separate strips: nothing between x = 30 and x = 70.
    osc::TriMesh mesh = helpers::quad(0.0f, 0.0f, 30.0f, 100.0f, 1.0f);
    const osc::TriMesh second = helpers::quad(70.0f, 0.0f, 100.0f, 100.0f, 2.0f);
    for (const std::uint32_t i : second.indices)
        mesh.indices.push_back(i + 4);
    mesh.vertices.insert(mesh.vertices.end(), second.vertices.begin(), second.vertices.end());
    const osc::SurfaceIndex surface(mesh);

    osc::ScatterSettings s;
    s.tileSize = 20.0f;
    s.density = 2.0f;
    s.surface = &surface;
    const osc::ScatterResult r = osc::scatterPlanar(pattern, s);
    CHECK(r.stats.rejectedOffSurface > 0);
    CHECK(r.stats.candidates == r.count() + r.stats.rejectedOffSurface);
    CHECK(static_cast<double>(r.count()) == doctest::Approx(2.0 * 60.0 * 100.0).epsilon(0.02));
    for (const osc::Instance& inst : flatten(r))
    {
        const osc::Vec3 p = inst.transform.t;
        REQUIRE((p.x <= 30.0f || p.x >= 70.0f));
        REQUIRE(p.z == (p.x <= 30.0f ? 1.0f : 2.0f));
    }
}

TEST_CASE("scatter: slope limit")
{
    const osc::Pattern& pattern = testPattern();
    // Left half flat, right half climbing at 45 degrees.
    osc::TriMesh mesh;
    mesh.vertices = { { 0.0f, 0.0f, 0.0f },  { 50.0f, 0.0f, 0.0f },  { 100.0f, 0.0f, 50.0f },
                      { 0.0f, 50.0f, 0.0f }, { 50.0f, 50.0f, 0.0f }, { 100.0f, 50.0f, 50.0f } };
    mesh.indices = { 0, 1, 4, 0, 4, 3, 1, 2, 5, 1, 5, 4 };
    const osc::SurfaceIndex surface(mesh);

    osc::ScatterSettings s;
    s.tileSize = 20.0f;
    s.density = 2.0f;
    s.surface = &surface;
    const std::size_t everything = osc::scatterPlanar(pattern, s).count();

    SUBCASE("gentle ground only")
    {
        s.limitSlope = true;
        s.slopeMaxDegrees = 30.0f;
        const osc::ScatterResult r = osc::scatterPlanar(pattern, s);
        CHECK(r.stats.rejectedBySlope > 0);
        CHECK(r.count() + r.stats.rejectedBySlope == everything);
        CHECK(static_cast<double>(r.count()) == doctest::Approx(everything * 0.5).epsilon(0.03));
        for (const osc::Instance& inst : flatten(r))
            REQUIRE(inst.transform.t.x <= 50.0f);
    }
    SUBCASE("steep ground only")
    {
        s.limitSlope = true;
        s.slopeMinDegrees = 40.0f;
        s.slopeMaxDegrees = 50.0f;
        const osc::ScatterResult r = osc::scatterPlanar(pattern, s);
        CHECK(static_cast<double>(r.count()) == doctest::Approx(everything * 0.5).epsilon(0.03));
        for (const osc::Instance& inst : flatten(r))
            REQUIRE(inst.transform.t.x >= 50.0f);
    }
    SUBCASE("the whole scale is no limit, even exactly at its ends")
    {
        s.limitSlope = true;
        s.slopeMinDegrees = 0.0f;
        s.slopeMaxDegrees = 180.0f;
        CHECK(osc::scatterPlanar(pattern, s).count() == everything);
        s.slopeMaxDegrees = 45.5f;
        CHECK(osc::scatterPlanar(pattern, s).count() == everything);
        s.slopeMinDegrees = kNaN; // read as "from 0"
        s.slopeMaxDegrees = kNaN; // read as "to 180"
        CHECK(osc::scatterPlanar(pattern, s).count() == everything);
    }
    SUBCASE("a range nothing fits in")
    {
        s.limitSlope = true;
        s.slopeMinDegrees = 60.0f;
        s.slopeMaxDegrees = 80.0f;
        CHECK(osc::scatterPlanar(pattern, s).count() == 0);
        s.slopeMinDegrees = 30.0f;
        s.slopeMaxDegrees = 10.0f; // upside down
        CHECK(osc::scatterPlanar(pattern, s).count() == 0);
    }
    SUBCASE("measured from another vertical")
    {
        // With "up" leaning 45 degrees towards +X... the slope faces away from it: it is the ramp's
        // normal (-1, 0, 1) that is 90 degrees from (1, 0, 1), and the flat half that is 45.
        s.limitSlope = true;
        s.up = { 5.0f, 0.0f, 5.0f }; // any length
        s.slopeMinDegrees = 80.0f;
        s.slopeMaxDegrees = 100.0f;
        const osc::ScatterResult r = osc::scatterPlanar(pattern, s);
        CHECK(static_cast<double>(r.count()) == doctest::Approx(everything * 0.5).epsilon(0.03));
        for (const osc::Instance& inst : flatten(r))
            REQUIRE(inst.transform.t.x >= 50.0f);
    }
}

TEST_CASE("scatter: altitude limit")
{
    const osc::Pattern& pattern = testPattern();
    // z = x over 100 x 40.
    const osc::SurfaceIndex surface(helpers::plane(0.0f, 0.0f, 100.0f, 40.0f, 0.0f, 1.0f, 0.0f));
    osc::ScatterSettings s;
    s.tileSize = 20.0f;
    s.density = 2.0f;
    s.surface = &surface;
    const std::size_t everything = osc::scatterPlanar(pattern, s).count();

    s.limitAltitude = true;
    s.altitudeMin = 20.0f;
    s.altitudeMax = 45.0f;
    osc::ScatterResult r = osc::scatterPlanar(pattern, s);
    CHECK(r.stats.rejectedByAltitude > 0);
    CHECK(r.count() + r.stats.rejectedByAltitude == everything);
    CHECK(static_cast<double>(r.count()) == doctest::Approx(everything * 0.25).epsilon(0.03));
    for (const osc::Instance& inst : flatten(r))
    {
        REQUIRE(inst.transform.t.z >= 20.0f);
        REQUIRE(inst.transform.t.z <= 45.0f);
    }

    // The altitude of the origin shifts the scale: the same band, 100 units higher.
    s.upOffset = 100.0f;
    CHECK(osc::scatterPlanar(pattern, s).count() == 0);
    s.altitudeMin = 120.0f;
    s.altitudeMax = 145.0f;
    CHECK(osc::scatterPlanar(pattern, s).count() == r.count());

    // A limit that is not a number is no limit on that side.
    s.upOffset = 0.0f;
    s.altitudeMin = kNaN;
    s.altitudeMax = 45.0f;
    r = osc::scatterPlanar(pattern, s);
    CHECK(static_cast<double>(r.count()) == doctest::Approx(everything * 0.45).epsilon(0.03));
    s.altitudeMax = kNaN;
    CHECK(osc::scatterPlanar(pattern, s).count() == everything);

    // On flat ground the altitude is that of the ground.
    osc::ScatterSettings flat = flatSettings(pattern, 50.0f, 0.1f);
    flat.limitAltitude = true;
    flat.altitudeMin = 1.0f;
    flat.altitudeMax = 2.0f;
    CHECK(osc::scatterPlanar(pattern, flat).count() == 0);
    flat.upOffset = 1.5f;
    CHECK(osc::scatterPlanar(pattern, flat).count() > 0);
}

TEST_CASE("scatter: density map over a rectangle of the plane")
{
    const osc::Pattern& pattern = testPattern();
    osc::ScatterSettings s = flatSettings(pattern, 100.0f, 0.5f);
    const std::size_t everything = osc::scatterPlanar(pattern, s).count();

    // Black on the left, white on the right, sharp in the middle (many columns).
    osc::Image halves;
    halves.width = 200;
    halves.height = 1;
    halves.pixels.assign(200, 0.0f);
    std::fill(halves.pixels.begin() + 100, halves.pixels.end(), 1.0f);

    s.densityMap.image = &halves;
    s.densityMap.space = osc::MapSpace::PlanarXY;
    s.densityMap.rect = { 0.0f, 0.0f, 100.0f, 100.0f };
    osc::ScatterResult r = osc::scatterPlanar(pattern, s);
    CHECK_FALSE(r.stats.mapIgnored);
    CHECK(r.stats.rejectedByMap > 0);
    CHECK(r.count() + r.stats.rejectedByMap == everything);
    CHECK(static_cast<double>(r.count()) == doctest::Approx(everything * 0.5).epsilon(0.03));
    for (const osc::Instance& inst : flatten(r))
        REQUIRE(inst.transform.t.x > 49.5f);

    // A ramp from black to white: the density follows it. Count by quarters.
    osc::Image ramp;
    ramp.width = 256;
    ramp.height = 2;
    ramp.pixels.resize(512);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 256; ++x)
            ramp.pixels[static_cast<std::size_t>(y * 256 + x)] = (static_cast<float>(x) + 0.5f) / 256.0f;
    s.densityMap.image = &ramp;
    r = osc::scatterPlanar(pattern, s);
    double quarters[4] = {};
    for (const osc::Instance& inst : flatten(r))
        quarters[std::min(3, static_cast<int>(inst.transform.t.x / 25.0f))] += 1.0;
    for (int q = 0; q < 4; ++q)
    {
        CAPTURE(q);
        CHECK(quarters[q] == doctest::Approx(everything * 0.25 * (0.125 + 0.25 * q)).epsilon(0.06));
    }

    // Values outside 0..1 and values that are not numbers: clamped, and no density.
    osc::Image wild;
    wild.width = 2;
    wild.height = 1;
    wild.pixels = { 50.0f, kNaN };
    s.densityMap.image = &wild;
    r = osc::scatterPlanar(pattern, s);
    CHECK(r.status == ScatterStatus::Ok);
    for (const osc::Instance& inst : flatten(r))
        REQUIRE(inst.transform.t.x < 75.0f); // from the middle of the second pixel on, the blend is NaN
    CHECK(r.count() > everything / 5);
}

TEST_CASE("scatter: density map in the texture coordinates of the surface")
{
    const osc::Pattern& pattern = testPattern();
    // A terrain whose texture coordinates run 0..1 across it.
    const osc::SurfaceIndex surface(osc::tools::makeTerrain(100.0f, 20, 5.0f, 3, true));
    osc::ScatterSettings s;
    s.tileSize = 20.0f;
    s.density = 4.0f;
    s.surface = &surface;
    const std::size_t everything = osc::scatterPlanar(pattern, s).count();

    // White only where v is above one half.
    osc::Image halves;
    halves.width = 1;
    halves.height = 200;
    halves.pixels.assign(200, 0.0f);
    std::fill(halves.pixels.begin() + 100, halves.pixels.end(), 1.0f);
    s.densityMap.image = &halves;
    s.densityMap.space = osc::MapSpace::SurfaceUV;
    const osc::ScatterResult r = osc::scatterPlanar(pattern, s);
    CHECK_FALSE(r.stats.mapIgnored);
    CHECK(static_cast<double>(r.count()) == doctest::Approx(everything * 0.5).epsilon(0.03));
    for (const osc::Instance& inst : flatten(r))
        REQUIRE(inst.transform.t.y > 49.5f);
}

TEST_CASE("scatter: a density map that cannot be used is left out, and says so")
{
    const osc::Pattern& pattern = testPattern();
    osc::ScatterSettings s = flatSettings(pattern, 60.0f, 0.2f);
    const std::size_t everything = osc::scatterPlanar(pattern, s).count();

    osc::Image black;
    black.width = black.height = 4;
    black.pixels.assign(16, 0.0f);

    SUBCASE("in texture coordinates, with no surface")
    {
        s.densityMap.image = &black;
        s.densityMap.space = osc::MapSpace::SurfaceUV;
    }
    SUBCASE("in texture coordinates, on a surface that has none")
    {
        static const osc::SurfaceIndex bare(helpers::quad(0.0f, 0.0f, 60.0f, 60.0f));
        s.surface = &bare;
        s.densityMap.image = &black;
        s.densityMap.space = osc::MapSpace::SurfaceUV;
    }
    SUBCASE("over a rectangle with no area")
    {
        s.densityMap.image = &black;
        s.densityMap.space = osc::MapSpace::PlanarXY;
        s.densityMap.rect = { 10.0f, 10.0f, 10.0f, 50.0f };
    }
    SUBCASE("an image that is not valid")
    {
        static osc::Image broken;
        broken.width = 8;
        broken.height = 8;
        broken.pixels = { 0.0f, 0.0f };
        s.densityMap.image = &broken;
        s.densityMap.space = osc::MapSpace::PlanarXY;
        s.densityMap.rect = { 0.0f, 0.0f, 60.0f, 60.0f };
    }
    const osc::ScatterResult r = osc::scatterPlanar(pattern, s);
    CHECK(r.stats.mapIgnored);
    CHECK(r.count() == everything);
}

TEST_CASE("scatter: areas")
{
    const osc::Pattern& pattern = testPattern();
    osc::ScatterSettings s = flatSettings(pattern, 100.0f, 0.5f);
    const std::size_t everything = osc::scatterPlanar(pattern, s).count();

    SUBCASE("an include area")
    {
        const osc::AreaSet areas(
            { helpers::area(osc::AreaMode::Include, { helpers::rectangle(20.0f, 30.0f, 70.0f, 60.0f) }) });
        s.areas = &areas;
        const osc::ScatterResult r = osc::scatterPlanar(pattern, s);
        CHECK(static_cast<double>(r.count()) == doctest::Approx(everything * 0.15).epsilon(0.03));
        for (const osc::Instance& inst : flatten(r))
        {
            const osc::Vec3 p = inst.transform.t;
            REQUIRE((p.x >= 20.0f && p.x <= 70.0f && p.y >= 30.0f && p.y <= 60.0f));
        }
        // Only the tiles the area reaches were looked at.
        CHECK(r.stats.candidates < everything / 2);

        // With no rectangle and no surface, the include areas are the domain.
        s.domain = osc::Rect{};
        const osc::ScatterResult alone = osc::scatterPlanar(pattern, s);
        CHECK(alone.status == ScatterStatus::Ok);
        CHECK(osc::hashInstances(alone) == osc::hashInstances(r));
    }
    SUBCASE("an exclude area")
    {
        const osc::AreaSet areas(
            { helpers::area(osc::AreaMode::Exclude, { helpers::rectangle(20.0f, 30.0f, 70.0f, 60.0f) }) });
        s.areas = &areas;
        const osc::ScatterResult r = osc::scatterPlanar(pattern, s);
        CHECK(r.stats.rejectedByAreas > 0);
        CHECK(r.count() + r.stats.rejectedByAreas == everything);
        CHECK(static_cast<double>(r.count()) == doctest::Approx(everything * 0.85).epsilon(0.03));
        for (const osc::Instance& inst : flatten(r))
        {
            const osc::Vec3 p = inst.transform.t;
            REQUIRE_FALSE((p.x > 20.0f && p.x < 70.0f && p.y > 30.0f && p.y < 60.0f));
        }

        // With only exclude areas there is still no domain to take.
        s.domain = osc::Rect{};
        CHECK(osc::scatterPlanar(pattern, s).status == ScatterStatus::NoDomain);
    }
    SUBCASE("a falloff thins the instances gradually")
    {
        // Full density up to x = 40, none from x = 80 on.
        const osc::AreaSet areas({ helpers::area(osc::AreaMode::Include,
                                                 { helpers::rectangle(-10.0f, -10.0f, 40.0f, 110.0f) }, 0.0f, 40.0f) });
        s.areas = &areas;
        double bands[5] = {}; // of 20 units
        for (const osc::Instance& inst : flatten(osc::scatterPlanar(pattern, s)))
            bands[std::min(4, static_cast<int>(inst.transform.t.x / 20.0f))] += 1.0;
        const double full = everything * 0.2;
        CHECK(bands[0] == doctest::Approx(full).epsilon(0.03));
        CHECK(bands[1] == doctest::Approx(full).epsilon(0.03));
        CHECK(bands[2] == doctest::Approx(full * 0.75).epsilon(0.06));
        CHECK(bands[3] == doctest::Approx(full * 0.25).epsilon(0.12));
        CHECK(bands[4] == 0.0);
    }
    SUBCASE("an include area that encloses nothing: no instances")
    {
        osc::Polyline line;
        line.points = { { 10.0f, 10.0f }, { 90.0f, 90.0f } };
        const osc::AreaSet areas({ helpers::area(osc::AreaMode::Include, { line }) });
        s.areas = &areas;
        const osc::ScatterResult r = osc::scatterPlanar(pattern, s);
        CHECK(r.status == ScatterStatus::Ok);
        CHECK(r.count() == 0);
    }
    SUBCASE("an area set with no areas changes nothing")
    {
        const osc::AreaSet none;
        s.areas = &none;
        CHECK(osc::scatterPlanar(pattern, s).count() == everything);
    }
}

TEST_CASE("scatter: models by probability")
{
    const osc::Pattern& pattern = testPattern();
    osc::ScatterSettings s = flatSettings(pattern, 100.0f, 1.0f);
    const std::size_t everything = osc::scatterPlanar(pattern, s).count();

    const auto shares = [&](const std::vector<float>& weights) {
        s.modelWeights = weights;
        std::vector<double> counts(weights.size(), 0.0);
        const std::vector<osc::Instance> all = flatten(osc::scatterPlanar(pattern, s));
        for (const osc::Instance& inst : all)
        {
            REQUIRE(inst.model < weights.size());
            counts[inst.model] += 1.0;
        }
        for (double& c : counts)
            c /= all.empty() ? 1.0 : static_cast<double>(all.size());
        CHECK(all.size() == everything); // the models never change how many instances there are
        return counts;
    };

    std::vector<double> got = shares({ 1.0f, 3.0f });
    CHECK(got[0] == doctest::Approx(0.25).epsilon(0.03));
    CHECK(got[1] == doctest::Approx(0.75).epsilon(0.03));

    got = shares({ 5.0f, 0.0f, 2.5f, 2.5f });
    CHECK(got[0] == doctest::Approx(0.5).epsilon(0.03));
    CHECK(got[1] == 0.0);
    CHECK(got[2] == doctest::Approx(0.25).epsilon(0.03));
    CHECK(got[3] == doctest::Approx(0.25).epsilon(0.03));

    // Weights that are not positive numbers count as 0, wherever they are in the list.
    got = shares({ kNaN, -3.0f, 1.0f, kInf, 0.0f });
    CHECK(got[2] == 1.0);

    got = shares({ 0.0f, 0.0f, 1e-20f });
    CHECK(got[2] == 1.0);

    got = shares({ 7.0f });
    CHECK(got[0] == 1.0);

    // Each model's instances are themselves evenly spread: half the points of blue noise picked by an
    // independent hash never leave big gaps.
    s.modelWeights = { 1.0f, 1.0f };
    int cells[2][25] = {};
    for (const osc::Instance& inst : flatten(osc::scatterPlanar(pattern, s)))
        ++cells[inst.model]
               [static_cast<int>(inst.transform.t.y / 20.0f) * 5 + static_cast<int>(inst.transform.t.x / 20.0f)];
    for (const auto& model : cells)
        for (const int count : model)
            CHECK(count > 2048 * 0.9);

    // No model can be chosen: nothing to scatter.
    s.modelWeights = { 0.0f, kNaN, -1.0f };
    const osc::ScatterResult none = osc::scatterPlanar(pattern, s);
    CHECK(none.status == ScatterStatus::Ok);
    CHECK(none.count() == 0);
}

TEST_CASE("scatter: by default instances stand upright, turned about their Z, at scale 1")
{
    const osc::Pattern& pattern = testPattern();
    const osc::ScatterSettings s = flatSettings(pattern, 60.0f, 0.3f);
    double sumCos = 0.0, sumSin = 0.0;
    std::set<int> directions;
    const std::vector<osc::Instance> all = flatten(osc::scatterPlanar(pattern, s));
    for (const osc::Instance& inst : all)
    {
        const osc::Transform& t = inst.transform;
        REQUIRE(t.z.x == 0.0f);
        REQUIRE(t.z.y == 0.0f);
        REQUIRE(t.z.z == 1.0f);
        REQUIRE(t.x.z == 0.0f);
        REQUIRE(osc::length(t.x) == doctest::Approx(1.0f));
        REQUIRE(osc::length(t.y) == doctest::Approx(1.0f));
        checkNoShear(t);
        sumCos += t.x.x;
        sumSin += t.x.y;
        directions.insert(static_cast<int>((t.x.x + 1.0f) * 8.0f) * 100 + static_cast<int>((t.x.y + 1.0f) * 8.0f));
    }
    // Every direction is taken, none favoured.
    CHECK(std::fabs(sumCos / static_cast<double>(all.size())) < 0.02);
    CHECK(std::fabs(sumSin / static_cast<double>(all.size())) < 0.02);
    CHECK(directions.size() > 40);
}

TEST_CASE("scatter: rotation ranges")
{
    const osc::Pattern& pattern = testPattern();
    osc::ScatterSettings s = flatSettings(pattern, 60.0f, 0.2f);

    SUBCASE("none: every instance is the identity")
    {
        s.transform.rotationDegrees[2] = {};
        for (const osc::Instance& inst : flatten(osc::scatterPlanar(pattern, s)))
        {
            REQUIRE(inst.transform.x.x == 1.0f);
            REQUIRE(inst.transform.x.y == 0.0f);
            REQUIRE(inst.transform.y.y == 1.0f);
            REQUIRE(inst.transform.z.z == 1.0f);
        }
    }
    SUBCASE("a fixed quarter turn about Z")
    {
        s.transform.rotationDegrees[2] = { 90.0f, 90.0f };
        for (const osc::Instance& inst : flatten(osc::scatterPlanar(pattern, s)))
        {
            REQUIRE(inst.transform.x.x == 0.0f);
            REQUIRE(inst.transform.x.y == 1.0f); // X now points along +Y
            REQUIRE(inst.transform.y.x == -1.0f);
            REQUIRE(inst.transform.y.y == 0.0f);
        }
    }
    SUBCASE("a narrow range about Z stays inside it")
    {
        s.transform.rotationDegrees[2] = { -20.0f, 40.0f };
        float low = 1000.0f, high = -1000.0f;
        for (const osc::Instance& inst : flatten(osc::scatterPlanar(pattern, s)))
        {
            const float degrees = std::atan2(inst.transform.x.y, inst.transform.x.x) * 57.29578f;
            low = std::min(low, degrees);
            high = std::max(high, degrees);
        }
        CHECK(low >= -20.001f);
        CHECK(low < -19.0f);
        CHECK(high <= 40.001f);
        CHECK(high > 39.0f);
    }
    SUBCASE("a tilt about X leans the instance's Z towards -Y")
    {
        s.transform.rotationDegrees[2] = {};
        s.transform.rotationDegrees[0] = { 30.0f, 30.0f };
        for (const osc::Instance& inst : flatten(osc::scatterPlanar(pattern, s)))
        {
            REQUIRE(inst.transform.x.x == 1.0f);
            REQUIRE(inst.transform.z.y == doctest::Approx(-0.5f));
            REQUIRE(inst.transform.z.z == doctest::Approx(0.8660254f));
            checkNoShear(inst.transform);
        }
    }
    SUBCASE("a tilt about Y leans the instance's Z towards +X")
    {
        s.transform.rotationDegrees[2] = {};
        s.transform.rotationDegrees[1] = { 30.0f, 30.0f };
        for (const osc::Instance& inst : flatten(osc::scatterPlanar(pattern, s)))
        {
            REQUIRE(inst.transform.y.y == 1.0f);
            REQUIRE(inst.transform.z.x == doctest::Approx(0.5f));
            REQUIRE(inst.transform.z.z == doctest::Approx(0.8660254f));
            checkNoShear(inst.transform);
        }
    }
    SUBCASE("all three at once: still a pure rotation")
    {
        s.transform.rotationDegrees[0] = { -180.0f, 180.0f };
        s.transform.rotationDegrees[1] = { -90.0f, 90.0f };
        s.transform.rotationDegrees[2] = { 0.0f, 360.0f };
        for (const osc::Instance& inst : flatten(osc::scatterPlanar(pattern, s)))
        {
            REQUIRE(osc::length(inst.transform.x) == doctest::Approx(1.0f));
            REQUIRE(osc::length(inst.transform.y) == doctest::Approx(1.0f));
            REQUIRE(osc::length(inst.transform.z) == doctest::Approx(1.0f));
            checkNoShear(inst.transform);
        }
    }
}

TEST_CASE("scatter: scale ranges")
{
    const osc::Pattern& pattern = testPattern();
    osc::ScatterSettings s = flatSettings(pattern, 60.0f, 0.3f);

    SUBCASE("uniform: one factor for the three axes, from the first range")
    {
        s.transform.scale[0] = { 0.5f, 2.0f };
        s.transform.scale[1] = { 100.0f, 200.0f }; // not used
        float low = 1e9f, high = 0.0f;
        double sum = 0.0;
        const std::vector<osc::Instance> all = flatten(osc::scatterPlanar(pattern, s));
        for (const osc::Instance& inst : all)
        {
            const float sx = osc::length(inst.transform.x);
            REQUIRE(osc::length(inst.transform.y) == doctest::Approx(sx));
            REQUIRE(osc::length(inst.transform.z) == doctest::Approx(sx));
            low = std::min(low, sx);
            high = std::max(high, sx);
            sum += sx;
            checkNoShear(inst.transform);
        }
        CHECK(low >= 0.4999f);
        CHECK(low < 0.51f);
        CHECK(high <= 2.0001f);
        CHECK(high > 1.99f);
        CHECK(sum / static_cast<double>(all.size()) == doctest::Approx(1.25).epsilon(0.02)); // evenly spread
    }
    SUBCASE("per axis: each its own range, independent of the others")
    {
        s.transform.uniformScale = false;
        s.transform.scale[0] = { 1.0f, 2.0f };
        s.transform.scale[1] = { 3.0f, 3.0f };
        s.transform.scale[2] = { 0.1f, 0.2f };
        int tallAndWide = 0, total = 0;
        for (const osc::Instance& inst : flatten(osc::scatterPlanar(pattern, s)))
        {
            const float sx = osc::length(inst.transform.x), sz = osc::length(inst.transform.z);
            REQUIRE(sx >= 0.9999f);
            REQUIRE(sx <= 2.0001f);
            REQUIRE(osc::length(inst.transform.y) == doctest::Approx(3.0f));
            REQUIRE(sz >= 0.09999f);
            REQUIRE(sz <= 0.20001f);
            checkNoShear(inst.transform);
            tallAndWide += (sx > 1.5f && sz > 0.15f) ? 1 : 0;
            ++total;
        }
        CHECK(tallAndWide == doctest::Approx(total * 0.25).epsilon(0.1));
    }
    SUBCASE("ranges that are not numbers fall back to 1")
    {
        s.transform.scale[0] = { kNaN, kInf };
        for (const osc::Instance& inst : flatten(osc::scatterPlanar(pattern, s)))
            REQUIRE(osc::length(inst.transform.x) == doctest::Approx(1.0f));
    }
}

TEST_CASE("scatter: alignment to the surface normal")
{
    const osc::Pattern& pattern = testPattern();
    // z = x: the normal is (-1, 0, 1) / sqrt(2), 45 degrees from the vertical.
    const osc::SurfaceIndex surface(helpers::plane(0.0f, 0.0f, 60.0f, 60.0f, 0.0f, 1.0f, 0.0f));
    osc::ScatterSettings s;
    s.tileSize = 20.0f;
    s.density = 1.0f;
    s.surface = &surface;
    const float h = 0.70710678f;

    SUBCASE("0: upright")
    {
        for (const osc::Instance& inst : flatten(osc::scatterPlanar(pattern, s)))
            REQUIRE(inst.transform.z.z == 1.0f);
    }
    SUBCASE("1: along the normal")
    {
        s.transform.alignToNormal = 1.0f;
        for (const osc::Instance& inst : flatten(osc::scatterPlanar(pattern, s)))
        {
            REQUIRE(inst.transform.z.x == doctest::Approx(-h));
            REQUIRE(inst.transform.z.y == doctest::Approx(0.0f));
            REQUIRE(inst.transform.z.z == doctest::Approx(h));
            REQUIRE(osc::length(inst.transform.x) == doctest::Approx(1.0f));
            checkNoShear(inst.transform);
        }
    }
    SUBCASE("in between: in between")
    {
        s.transform.alignToNormal = 0.5f;
        for (const osc::Instance& inst : flatten(osc::scatterPlanar(pattern, s)))
        {
            // Halfway between (0, 0, 1) and the normal, normalised: 22.5 degrees from the vertical.
            REQUIRE(inst.transform.z.z == doctest::Approx(0.9238795f));
            REQUIRE(inst.transform.z.x == doctest::Approx(-0.3826834f));
            checkNoShear(inst.transform);
        }
        s.transform.alignToNormal = 55.0f; // clamped to 1
        for (const osc::Instance& inst : flatten(osc::scatterPlanar(pattern, s)))
            REQUIRE(inst.transform.z.z == doctest::Approx(h));
    }
    SUBCASE("without the random turn, the tilt has no twist")
    {
        // The shortest rotation from Z to the normal is about the Y axis here: Y stays where it was.
        s.transform.alignToNormal = 1.0f;
        s.transform.rotationDegrees[2] = {};
        for (const osc::Instance& inst : flatten(osc::scatterPlanar(pattern, s)))
        {
            REQUIRE(inst.transform.y.x == doctest::Approx(0.0f));
            REQUIRE(inst.transform.y.y == doctest::Approx(1.0f));
            REQUIRE(inst.transform.x.x == doctest::Approx(h));
            REQUIRE(inst.transform.x.z == doctest::Approx(h));
        }
    }
    SUBCASE("another vertical: instances stand along it")
    {
        s.up = { 0.0f, 3.0f, 4.0f };
        for (const osc::Instance& inst : flatten(osc::scatterPlanar(pattern, s)))
        {
            REQUIRE(inst.transform.z.y == doctest::Approx(0.6f));
            REQUIRE(inst.transform.z.z == doctest::Approx(0.8f));
            checkNoShear(inst.transform);
        }
        // Straight down, the one direction with no shortest rotation, and nonsense: still fine.
        s.up = { 0.0f, 0.0f, -1.0f };
        for (const osc::Instance& inst : flatten(osc::scatterPlanar(pattern, s)))
        {
            REQUIRE(inst.transform.z.z == -1.0f);
            checkNoShear(inst.transform);
        }
        for (const osc::Vec3 nonsense :
             { osc::Vec3{ 0.0f, 0.0f, 0.0f }, osc::Vec3{ kNaN, 0.0f, 1.0f }, osc::Vec3{ kInf, 0.0f, 0.0f } })
        {
            s.up = nonsense; // read as (0, 0, 1)
            for (const osc::Instance& inst : flatten(osc::scatterPlanar(pattern, s)))
                REQUIRE(inst.transform.z.z == 1.0f);
        }
    }
}

TEST_CASE("scatter: translation ranges")
{
    const osc::Pattern& pattern = testPattern();
    osc::ScatterSettings s = flatSettings(pattern, 60.0f, 0.2f);
    const std::vector<osc::Instance> still = flatten(osc::scatterPlanar(pattern, s));

    SUBCASE("sunk into flat ground")
    {
        s.transform.translation[2] = { -0.5f, -0.1f };
        const std::vector<osc::Instance> moved = flatten(osc::scatterPlanar(pattern, s));
        REQUIRE(moved.size() == still.size());
        float low = 0.0f, high = -1.0f;
        for (std::size_t i = 0; i < moved.size(); ++i)
        {
            REQUIRE(moved[i].id == still[i].id);
            REQUIRE(moved[i].transform.t.x == still[i].transform.t.x);
            REQUIRE(moved[i].transform.t.y == still[i].transform.t.y);
            low = std::min(low, moved[i].transform.t.z);
            high = std::max(high, moved[i].transform.t.z);
        }
        CHECK(low >= -0.5001f);
        CHECK(low < -0.49f);
        CHECK(high <= -0.0999f);
        CHECK(high > -0.11f);
    }
    SUBCASE("on a slope, Z is away from the ground when aligned to it")
    {
        const osc::SurfaceIndex surface(helpers::plane(0.0f, 0.0f, 60.0f, 60.0f, 0.0f, 1.0f, 0.0f));
        s.surface = &surface;
        s.transform.alignToNormal = 1.0f;
        const std::vector<osc::Instance> onGround = flatten(osc::scatterPlanar(pattern, s));
        s.transform.translation[2] = { 2.0f, 2.0f };
        const std::vector<osc::Instance> lifted = flatten(osc::scatterPlanar(pattern, s));
        REQUIRE(lifted.size() == onGround.size());
        for (std::size_t i = 0; i < lifted.size(); ++i)
        {
            const osc::Vec3 d = lifted[i].transform.t - onGround[i].transform.t;
            REQUIRE(d.x == doctest::Approx(-1.4142135f).epsilon(1e-4));
            REQUIRE(d.y == doctest::Approx(0.0f));
            REQUIRE(d.z == doctest::Approx(1.4142135f).epsilon(1e-4));
        }
    }
    SUBCASE("sideways, and the filters still see where the instance was born")
    {
        const osc::AreaSet areas(
            { helpers::area(osc::AreaMode::Include, { helpers::rectangle(10.0f, 10.0f, 50.0f, 50.0f) }) });
        s.areas = &areas;
        const std::size_t before = osc::scatterPlanar(pattern, s).count();
        s.transform.translation[0] = { 100.0f, 100.0f };
        const std::vector<osc::Instance> moved = flatten(osc::scatterPlanar(pattern, s));
        CHECK(moved.size() == before);
        for (const osc::Instance& inst : moved)
            REQUIRE(inst.transform.t.x >= 110.0f);
    }
}

TEST_CASE("scatter: the instance limit")
{
    const osc::Pattern& pattern = testPattern();
    osc::ScatterSettings s = flatSettings(pattern, 100.0f, 1.0f); // 102 400 instances
    const osc::ScatterResult all = osc::scatterPlanar(pattern, s);
    REQUIRE(all.count() > 100000);

    SUBCASE("above what there is: nothing changes")
    {
        s.maxInstances = 500000;
        const osc::ScatterResult r = osc::scatterPlanar(pattern, s);
        CHECK_FALSE(r.stats.limited);
        CHECK(osc::hashInstances(r) == osc::hashInstances(all));
    }
    SUBCASE("below: the highest ranks go, evenly, and the rest stay as they were")
    {
        s.maxInstances = 20000;
        const osc::ScatterResult r = osc::scatterPlanar(pattern, s);
        CHECK(r.stats.limited);
        CHECK(r.count() <= 20000);
        CHECK(r.count() > 19900); // as close as whole pattern points allow: 25 instances apart
        CHECK(r.stats.patternShare == doctest::Approx(20000.0 / 102400.0).epsilon(0.01));

        const helpers::Survivors kept = helpers::survivors(r, all);
        CHECK(kept.kept == r.count());
        CHECK(kept.identical == r.count());

        // Every instance kept ranks below every instance dropped.
        float highestKept = 0.0f;
        for (const osc::Instance& inst : flatten(r))
            highestKept = std::max(highestKept, inst.rank);
        const auto limited = helpers::byId(r);
        for (const osc::Instance& inst : flatten(all))
            if (limited.find(inst.id) == limited.end())
                REQUIRE(inst.rank > highestKept);

        // And they are still evenly spread: 10 x 10 cells, about 200 each.
        int cells[100] = {};
        for (const osc::Instance& inst : flatten(r))
            ++cells[static_cast<int>(inst.transform.t.y / 10.0f) * 10 + static_cast<int>(inst.transform.t.x / 10.0f)];
        for (const int count : cells)
        {
            CHECK(count > 200 * 0.8);
            CHECK(count < 200 * 1.2);
        }
    }
    SUBCASE("the limit counts what the filters leave, not what the domain could hold")
    {
        // An include area a tenth of the domain: about 10 240 instances, under a limit of 15 000.
        const osc::AreaSet areas(
            { helpers::area(osc::AreaMode::Include, { helpers::rectangle(0.0f, 0.0f, 100.0f, 10.0f) }) });
        s.areas = &areas;
        const std::size_t unlimited = osc::scatterPlanar(pattern, s).count();
        s.maxInstances = 15000;
        osc::ScatterResult r = osc::scatterPlanar(pattern, s);
        CHECK_FALSE(r.stats.limited);
        CHECK(r.count() == unlimited);
        s.maxInstances = 5000;
        r = osc::scatterPlanar(pattern, s);
        CHECK(r.stats.limited);
        CHECK(r.count() <= 5000);
        CHECK(r.count() > 4900);
    }
    SUBCASE("a limit below one instance per pattern point leaves nothing, the same for any thread count")
    {
        s.maxInstances = 10; // each pattern point gives 25 instances here
        const osc::ScatterResult r = osc::scatterPlanar(pattern, s);
        CHECK(r.status == ScatterStatus::Ok);
        CHECK(r.stats.limited);
        CHECK(r.count() == 0);
        s.maxInstances = 20000;
        CHECK(osc::hashInstances(osc::scatterPlanar(pattern, s, 1)) ==
              osc::hashInstances(osc::scatterPlanar(pattern, s, 7)));
    }
}

TEST_CASE("scatter: nonsense in the settings never breaks it")
{
    // Random meshes with bad indices and bad vertices, random areas, random numbers (some of them
    // not numbers at all) in every setting: whatever comes back must be a status and, when there are
    // instances, finite ones.
    const osc::Pattern pattern = osc::makeProgressiveBlueNoise(256, 99);
    const float specials[] = { 0.0f, -0.0f, 1.0f, -1.0f, kNaN, kInf, -kInf, 1e-30f, 1e30f, 3.4e38f, 45.0f, 1000.0f };
    std::size_t withInstances = 0;
    for (std::uint64_t round = 0; round < 400; ++round)
    {
        std::uint64_t counter = 0;
        const auto next = [&]() { return osc::hashCombine(round * 7919 + 13, counter++); };
        const auto number = [&]() {
            const std::uint64_t h = next();
            if (h % 4 == 0)
                return specials[(h >> 8) % std::size(specials)];
            return (osc::unitFloat(next()) - 0.3f) * 200.0f;
        };

        osc::TriMesh mesh;
        const std::size_t vertices = next() % 40;
        for (std::size_t i = 0; i < vertices; ++i)
            mesh.vertices.push_back({ number(), number(), number() });
        const std::size_t indices = next() % 90;
        for (std::size_t i = 0; i < indices; ++i)
            mesh.indices.push_back(static_cast<std::uint32_t>(next() % (vertices + 3)));
        if (next() % 3 == 0)
            for (std::size_t i = 0; i < indices; ++i)
                mesh.cornerUVs.push_back({ number(), number() });
        const osc::SurfaceIndex surface(std::move(mesh));

        std::vector<osc::Area> areaList;
        for (std::size_t a = next() % 4; a > 0; --a)
        {
            osc::Area ar;
            ar.mode = next() % 2 ? osc::AreaMode::Include : osc::AreaMode::Exclude;
            for (std::size_t sh = next() % 3; sh > 0; --sh)
            {
                osc::Polyline line;
                line.closed = next() % 2 == 0;
                for (std::size_t p = next() % 12; p > 0; --p)
                    line.points.push_back({ number(), number() });
                ar.shapes.push_back(line);
            }
            ar.width = number();
            ar.falloffNear = number();
            ar.falloffFar = number();
            areaList.push_back(ar);
        }
        const osc::AreaSet areas(areaList);

        osc::Image image;
        image.width = static_cast<int>(next() % 6);
        image.height = static_cast<int>(next() % 6);
        image.pixels.resize(static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) +
                            (next() % 5 == 0 ? 1 : 0));
        for (float& p : image.pixels)
            p = number();

        osc::ScatterSettings s;
        s.tileSize = next() % 3 ? 10.0f + osc::unitFloat(next()) * 90.0f : number();
        s.density = next() % 3 ? osc::unitFloat(next()) * 0.5f : number();
        s.seed = next();
        if (next() % 2)
            s.domain = { number(), number(), number(), number() };
        s.maxInstances = next() % 4 == 0 ? next() % 500 : 0;
        s.surface = next() % 3 ? &surface : nullptr;
        s.areas = next() % 2 ? &areas : nullptr;
        if (next() % 2)
        {
            s.densityMap.image = &image;
            s.densityMap.space = next() % 2 ? osc::MapSpace::PlanarXY : osc::MapSpace::SurfaceUV;
            s.densityMap.rect = { number(), number(), number(), number() };
            s.densityMap.wrap = next() % 2 ? osc::Wrap::Repeat : osc::Wrap::Clamp;
        }
        s.up = { number(), number(), number() };
        s.upOffset = number();
        s.limitSlope = next() % 2 == 0;
        s.slopeMinDegrees = number();
        s.slopeMaxDegrees = number();
        s.limitAltitude = next() % 3 == 0;
        s.altitudeMin = number();
        s.altitudeMax = number();
        for (std::size_t m = next() % 5; m > 0; --m)
            s.modelWeights.push_back(number());
        s.transform.alignToNormal = number();
        s.transform.uniformScale = next() % 2 == 0;
        for (int axis = 0; axis < 3; ++axis)
        {
            // Finite ranges here (bounded so the products stay finite): what is being tested is that
            // nonsense anywhere else never turns into nonsense in an instance.
            s.transform.rotationDegrees[axis] = { osc::unitFloat(next()) * 720.0f - 360.0f,
                                                  osc::unitFloat(next()) * 720.0f - 360.0f };
            s.transform.scale[axis] = { osc::unitFloat(next()) * 3.0f, osc::unitFloat(next()) * 3.0f };
            s.transform.translation[axis] = { osc::unitFloat(next()) * 10.0f - 5.0f,
                                              osc::unitFloat(next()) * 10.0f - 5.0f };
        }
        // Keep the work bounded: a huge domain with a tiny tile is a legitimate TooManyTiles or a
        // legitimate million instances, and neither is what this test is about.
        if (std::isfinite(s.tileSize) && s.tileSize > 0.0f && s.tileSize < 5.0f)
            s.tileSize = 5.0f;

        const osc::ScatterResult r = osc::scatterPlanar(pattern, s, 1 + static_cast<unsigned>(round % 4));
        REQUIRE(static_cast<int>(r.status) >= 0);
        REQUIRE(static_cast<int>(r.status) <= static_cast<int>(ScatterStatus::OutOfMemory));
        if (r.status != ScatterStatus::Ok)
        {
            REQUIRE(r.count() == 0);
            continue;
        }
        REQUIRE(r.stats.instances == r.count());
        if (s.maxInstances > 0)
            REQUIRE(r.count() <= s.maxInstances);
        withInstances += r.count() > 0 ? 1 : 0;
        for (const auto& tile : r.tiles)
            for (const osc::Instance& inst : tile)
            {
                REQUIRE(osc::isFinite(inst.transform.t));
                REQUIRE(osc::isFinite(inst.transform.x));
                REQUIRE(osc::isFinite(inst.transform.y));
                REQUIRE(osc::isFinite(inst.transform.z));
                REQUIRE(inst.rank >= 0.0f);
                REQUIRE(inst.rank < 1.0f);
                REQUIRE(inst.model < std::max<std::size_t>(1, s.modelWeights.size()));
            }
    }
    CHECK(withInstances > 20); // and it was not all rejected up front
}
