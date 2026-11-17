// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tests of the OpenScatter core: the same scatter on every machine and with any number of threads.
//
// A scene that renders on several machines, or in tiles, only works when each of them computes exactly
// the same instances. So the result must not depend on the thread count, and it must match a hash
// stored here: if a change to the code moves that hash, old scenes would scatter differently, and that
// has to be a decision (a new kAlgorithmVersion), never an accident.

#include <doctest/doctest.h>

#include "helpers.h"
#include "scene.h"

TEST_CASE("determinism: the same instances with 1, 4 and 32 threads - flat ground")
{
    const osc::Pattern& pattern = helpers::testPattern();
    osc::ScatterSettings s = helpers::flatSettings(pattern, 300.0f, 0.4f);
    s.modelWeights = { 1.0f, 2.0f };
    s.transform.scale[0] = { 0.7f, 1.4f };

    const osc::ScatterResult one = osc::scatterPlanar(pattern, s, 1);
    REQUIRE(one.status == osc::ScatterStatus::Ok);
    REQUIRE(one.count() > 300000);
    const std::uint64_t expected = osc::hashInstances(one);
    for (const unsigned threads : { 2u, 4u, 7u, 32u, 0u })
    {
        CAPTURE(threads);
        const osc::ScatterResult many = osc::scatterPlanar(pattern, s, threads);
        CHECK(many.count() == one.count());
        CHECK(osc::hashInstances(many) == expected);
        CHECK(many.stats.candidates == one.stats.candidates);
        CHECK(many.tileColumns == one.tileColumns);
        CHECK(many.tileRows == one.tileRows);
    }
}

TEST_CASE("determinism: the same instances with 1, 4 and 32 threads - every feature at once")
{
    const osc::Pattern& pattern = helpers::defaultPattern();
    const osc::SurfaceIndex surface(scene::terrain());
    const osc::AreaSet areas(scene::areas());
    const osc::Image map = scene::densityMap();
    const osc::ScatterSettings s = scene::settings(surface, areas, map);

    const osc::ScatterResult one = osc::scatterPlanar(pattern, s, 1);
    REQUIRE(one.status == osc::ScatterStatus::Ok);
    const std::uint64_t hash = osc::hashInstances(one);

    // Every filter took its share, so every code path is in the hash.
    CHECK(one.stats.rejectedByAreas > 1000);
    CHECK(one.stats.rejectedByMap > 1000);
    CHECK(one.stats.rejectedBySlope > 1000);
    CHECK(one.stats.rejectedByAltitude > 1000);
    CHECK(one.count() > 20000);
    CHECK_FALSE(one.stats.mapIgnored);
    CHECK(one.stats.candidates == one.count() + one.stats.rejectedByAreas + one.stats.rejectedByMap +
                                      one.stats.rejectedOffSurface + one.stats.rejectedBySlope +
                                      one.stats.rejectedByAltitude);

    for (const unsigned threads : { 4u, 32u, 0u })
    {
        CAPTURE(threads);
        const osc::ScatterResult many = osc::scatterPlanar(pattern, s, threads);
        CHECK(many.count() == one.count());
        CHECK(osc::hashInstances(many) == hash);
        CHECK(many.stats.rejectedByAreas == one.stats.rejectedByAreas);
        CHECK(many.stats.rejectedByMap == one.stats.rejectedByMap);
        CHECK(many.stats.rejectedBySlope == one.stats.rejectedBySlope);
        CHECK(many.stats.rejectedByAltitude == one.stats.rejectedByAltitude);
    }

    // The stored result. A different number here means the algorithm changed (see the top of the file).
    CHECK(one.count() == 35622);
    CHECK(hash == 0x66ECE48136F166E8ull);

    // With an instance limit too: the cut depends on counts gathered by the threads.
    osc::ScatterSettings limited = s;
    limited.maxInstances = 10000;
    const osc::ScatterResult limitedOne = osc::scatterPlanar(pattern, limited, 1);
    CHECK(limitedOne.stats.limited);
    CHECK(limitedOne.count() <= 10000);
    CHECK(limitedOne.count() > 9900);
    for (const unsigned threads : { 4u, 32u })
        CHECK(osc::hashInstances(osc::scatterPlanar(pattern, limited, threads)) == osc::hashInstances(limitedOne));
    CHECK(osc::hashInstances(limitedOne) == 0x5E007C2CF4422FD2ull);
}

TEST_CASE("determinism: the inputs of the stored result are themselves the same everywhere")
{
    // If the stored hash above fails, these say whether it is the scatter or the scene that moved.
    const osc::TriMesh terrain = scene::terrain();
    std::uint64_t h = 0;
    for (const osc::Vec3& v : terrain.vertices)
        h = osc::hashCombine(h, std::bit_cast<std::uint32_t>(v.z));
    CHECK(h == 0xEA8517D04DF8911Aull);

    const osc::Image map = scene::densityMap();
    h = 0;
    for (const float p : map.pixels)
        h = osc::hashCombine(h, std::bit_cast<std::uint32_t>(p));
    CHECK(h == 0x14FC48970BA565C4ull);

    h = 0;
    for (const osc::Area& area : scene::areas())
        for (const osc::Polyline& shape : area.shapes)
            for (const osc::Vec2& p : shape.points)
                h = osc::hashCombine(h, (static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(p.x)) << 32) |
                                            std::bit_cast<std::uint32_t>(p.y));
    CHECK(h == 0xA7BB8BDB3A34B0D0ull);
}

TEST_CASE("determinism: the hash tells scatters apart")
{
    const osc::Pattern& pattern = helpers::testPattern();
    osc::ScatterSettings s = helpers::flatSettings(pattern, 40.0f, 0.2f);
    const osc::ScatterResult base = osc::scatterPlanar(pattern, s);
    const std::uint64_t hash = osc::hashInstances(base);

    std::size_t tile = 0; // a block with at least two instances
    while (tile < base.tiles.size() && base.tiles[tile].size() < 2)
        ++tile;
    REQUIRE(tile < base.tiles.size());

    osc::ScatterResult changed = base;
    changed.tiles[tile][0].transform.t.z += 0.001f;
    CHECK(osc::hashInstances(changed) != hash);
    changed = base;
    changed.tiles[tile][0].model = 1;
    CHECK(osc::hashInstances(changed) != hash);
    changed = base;
    changed.tiles[tile][0].transform.y.x = -changed.tiles[tile][0].transform.y.x;
    CHECK(osc::hashInstances(changed) != hash);
    changed = base;
    std::swap(changed.tiles[tile][0], changed.tiles[tile][1]);
    CHECK(osc::hashInstances(changed) != hash);
    changed = base;
    changed.tiles[tile].pop_back();
    CHECK(osc::hashInstances(changed) != hash);

    CHECK(osc::hashInstances(osc::ScatterResult{}) == osc::hashInstances(osc::ScatterResult{}));
}
