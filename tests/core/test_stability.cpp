// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tests of the OpenScatter core: stability - what an edit does not touch.
//
// The promise to the artist: changing the density, an area, a map or a limit adds or removes
// instances, and never moves, turns, resizes or renames one that stays. Each test changes one thing and
// checks that every instance present before and after is the same bit for bit.

#include <doctest/doctest.h>

#include <cmath>

#include "helpers.h"
#include "scene.h"

using helpers::survivors;

namespace
{

// The scene of the determinism test: every feature at once.
struct Scene
{
    osc::SurfaceIndex surface{ scene::terrain() };
    osc::AreaSet areas{ scene::areas() };
    osc::Image map = scene::densityMap();
    osc::ScatterSettings settings = scene::settings(surface, areas, map);

    osc::ScatterResult scatter(const osc::ScatterSettings& s) const
    {
        osc::ScatterResult r = osc::scatterPlanar(helpers::defaultPattern(), s);
        REQUIRE(r.status == osc::ScatterStatus::Ok);
        return r;
    }
};

// Compares what the instances of "after" kept from "before", aspect by aspect.
struct Kept
{
    std::size_t common = 0;
    std::size_t samePosition = 0;
    std::size_t sameModel = 0;
    std::size_t sameAxes = 0;       // rotation and scale together
    std::size_t sameLengths = 0;    // scale alone
};

Kept compare(const osc::ScatterResult& before, const osc::ScatterResult& after)
{
    const auto later = helpers::byId(after);
    Kept k;
    for (const auto& tile : before.tiles)
        for (const osc::Instance& a : tile)
        {
            const auto it = later.find(a.id);
            if (it == later.end())
                continue;
            const osc::Instance& b = it->second;
            ++k.common;
            k.samePosition += helpers::sameBits(a.transform.t, b.transform.t) ? 1 : 0;
            k.sameModel += a.model == b.model ? 1 : 0;
            k.sameAxes += (helpers::sameBits(a.transform.x, b.transform.x) && helpers::sameBits(a.transform.y, b.transform.y)
                           && helpers::sameBits(a.transform.z, b.transform.z)) ? 1 : 0;
            k.sameLengths += (std::fabs(osc::length(a.transform.x) - osc::length(b.transform.x)) < 1e-5f
                              && std::fabs(osc::length(a.transform.y) - osc::length(b.transform.y)) < 1e-5f
                              && std::fabs(osc::length(a.transform.z) - osc::length(b.transform.z)) < 1e-5f) ? 1 : 0;
        }
    return k;
}

} // namespace

TEST_CASE("stability: raising the density only adds instances")
{
    const Scene sc;
    osc::ScatterSettings s = sc.settings;
    s.density = 0.8f;
    const osc::ScatterResult low = sc.scatter(s);
    s.density = 1.6f;
    const osc::ScatterResult mid = sc.scatter(s);
    s.density = 4.0f;   // all of the pattern
    const osc::ScatterResult high = sc.scatter(s);

    REQUIRE(low.count() > 5000);
    CHECK(static_cast<double>(mid.count()) == doctest::Approx(low.count() * 2.0).epsilon(0.05));
    CHECK(high.count() > mid.count());

    // Everything in the sparser scatter is in the denser one, untouched.
    helpers::Survivors kept = survivors(low, mid);
    CHECK(kept.kept == low.count());
    CHECK(kept.identical == low.count());
    kept = survivors(mid, high);
    CHECK(kept.kept == mid.count());
    CHECK(kept.identical == mid.count());

    // Each scatter uses the share of the pattern its density asks for, and no rank above it.
    CHECK(low.stats.patternShare == doctest::Approx(0.2f).epsilon(0.001));
    CHECK(mid.stats.patternShare == doctest::Approx(0.4f).epsilon(0.001));
    CHECK(high.stats.patternShare == 1.0f);
    for (const osc::Instance& inst : helpers::flatten(low))
        REQUIRE(inst.rank < 0.2f);
    for (const osc::Instance& inst : helpers::flatten(mid))
        REQUIRE(inst.rank < 0.4f);
}

TEST_CASE("stability: editing an area leaves the rest alone")
{
    const Scene sc;
    const osc::ScatterResult before = sc.scatter(sc.settings);

    // The clearing moves 40 units to the right: from x 150..210 to x 190..250 (falloff of 8 around).
    const osc::AreaSet edited(scene::areas(190.0f));
    osc::ScatterSettings s = sc.settings;
    s.areas = &edited;
    const osc::ScatterResult after = sc.scatter(s);

    CHECK(osc::hashInstances(before) != osc::hashInstances(after));
    const helpers::Survivors kept = survivors(before, after);
    CHECK(kept.kept > before.count() * 8 / 10);
    CHECK(kept.identical == kept.kept);   // whoever is in both is the same, bit for bit

    // Away from both positions of the clearing (and its falloff) nothing was added or removed.
    const auto farFromEdit = [](const osc::Instance& inst) {
        const osc::Vec3 p = inst.transform.t;
        return p.x < 140.0f || p.x > 260.0f || p.y < 80.0f || p.y > 150.0f;
    };
    const auto afterIds = helpers::byId(after);
    const auto beforeIds = helpers::byId(before);
    std::size_t farCount = 0;
    for (const osc::Instance& inst : helpers::flatten(before))
        if (farFromEdit(inst))
        {
            ++farCount;
            REQUIRE(afterIds.find(inst.id) != afterIds.end());
        }
    for (const osc::Instance& inst : helpers::flatten(after))
        if (farFromEdit(inst))
            REQUIRE(beforeIds.find(inst.id) != beforeIds.end());
    CHECK(farCount > before.count() / 2);

    // Where the clearing was, instances came back; where it is now, they went.
    std::size_t cameBack = 0, went = 0;
    for (const osc::Instance& inst : helpers::flatten(after))
        cameBack += beforeIds.find(inst.id) == beforeIds.end() ? 1 : 0;
    for (const osc::Instance& inst : helpers::flatten(before))
        went += afterIds.find(inst.id) == afterIds.end() ? 1 : 0;
    CHECK(cameBack > 0);
    CHECK(went > 0);
}

TEST_CASE("stability: adding and removing areas")
{
    const Scene sc;
    const osc::ScatterResult before = sc.scatter(sc.settings);

    // One more exclude area.
    std::vector<osc::Area> more = scene::areas();
    more.push_back(helpers::area(osc::AreaMode::Exclude, { scene::circle(260.0f, 300.0f, 35.0f, 32) }, 0.0f, 10.0f));
    const osc::AreaSet moreSet(more);
    osc::ScatterSettings s = sc.settings;
    s.areas = &moreSet;
    const osc::ScatterResult fewer = sc.scatter(s);
    CHECK(fewer.count() < before.count());
    helpers::Survivors kept = survivors(fewer, before);
    CHECK(kept.kept == fewer.count());        // nothing new appeared
    CHECK(kept.identical == fewer.count());

    // No areas at all: everything that was there still is.
    s.areas = nullptr;
    const osc::ScatterResult all = sc.scatter(s);
    CHECK(all.count() > before.count());
    kept = survivors(before, all);
    CHECK(kept.kept == before.count());
    CHECK(kept.identical == before.count());
}

TEST_CASE("stability: changing the density map, the slope or the altitude limit")
{
    const Scene sc;
    const osc::ScatterResult before = sc.scatter(sc.settings);

    SUBCASE("another map")
    {
        const osc::Image other = scene::densityMap(77);
        osc::ScatterSettings s = sc.settings;
        s.densityMap.image = &other;
        const osc::ScatterResult after = sc.scatter(s);
        CHECK(osc::hashInstances(after) != osc::hashInstances(before));
        const helpers::Survivors kept = survivors(before, after);
        CHECK(kept.kept > before.count() / 4);
        CHECK(kept.kept < before.count());
        CHECK(kept.identical == kept.kept);
    }
    SUBCASE("no map")
    {
        osc::ScatterSettings s = sc.settings;
        s.densityMap.image = nullptr;
        const osc::ScatterResult after = sc.scatter(s);
        CHECK(after.count() > before.count());
        const helpers::Survivors kept = survivors(before, after);
        CHECK(kept.kept == before.count());
        CHECK(kept.identical == before.count());
    }
    SUBCASE("a wider slope limit")
    {
        osc::ScatterSettings s = sc.settings;
        s.slopeMaxDegrees = 60.0f;
        const osc::ScatterResult after = sc.scatter(s);
        CHECK(after.count() > before.count());
        const helpers::Survivors kept = survivors(before, after);
        CHECK(kept.kept == before.count());
        CHECK(kept.identical == before.count());
    }
    SUBCASE("a narrower altitude range")
    {
        osc::ScatterSettings s = sc.settings;
        s.altitudeMin = 29.0f;
        s.altitudeMax = 35.0f;
        const osc::ScatterResult after = sc.scatter(s);
        CHECK(after.count() < before.count());
        const helpers::Survivors kept = survivors(after, before);
        CHECK(kept.kept == after.count());
        CHECK(kept.identical == after.count());
    }
    SUBCASE("a smaller domain")
    {
        osc::ScatterSettings s = sc.settings;
        s.domain = { 100.0f, 100.0f, 250.0f, 300.0f };
        const osc::ScatterResult part = sc.scatter(s);
        CHECK(part.count() < before.count());
        CHECK(part.count() > 0);
        const helpers::Survivors kept = survivors(part, before);
        CHECK(kept.kept == part.count());
        CHECK(kept.identical == part.count());
    }
}

TEST_CASE("stability: reseeding one aspect changes only that aspect")
{
    const Scene sc;
    const osc::ScatterResult before = sc.scatter(sc.settings);
    const std::size_t n = before.count();

    SUBCASE("models")
    {
        osc::ScatterSettings s = sc.settings;
        s.modelSeed = 5;
        const Kept k = compare(before, sc.scatter(s));
        CHECK(k.common == n);
        CHECK(k.samePosition == n);
        CHECK(k.sameAxes == n);
        CHECK(k.sameModel < n * 6 / 10);   // three models at 3 : 1 : 2: about 39 % keep theirs by chance
        CHECK(k.sameModel > n * 2 / 10);
    }
    SUBCASE("rotation")
    {
        osc::ScatterSettings s = sc.settings;
        s.rotationSeed = 5;
        const Kept k = compare(before, sc.scatter(s));
        CHECK(k.common == n);
        CHECK(k.samePosition == n);
        CHECK(k.sameModel == n);
        CHECK(k.sameLengths == n);
        CHECK(k.sameAxes < n / 100);
    }
    SUBCASE("scale")
    {
        osc::ScatterSettings s = sc.settings;
        s.scaleSeed = 5;
        const osc::ScatterResult after = sc.scatter(s);
        const Kept k = compare(before, after);
        CHECK(k.common == n);
        CHECK(k.samePosition == n);
        CHECK(k.sameModel == n);
        CHECK(k.sameLengths < n / 100);
        // The directions of the axes stay: only their lengths change.
        const auto later = helpers::byId(after);
        for (const osc::Instance& a : helpers::flatten(before))
        {
            const osc::Instance& b = later.at(a.id);
            const float cosine = osc::dot(a.transform.z, b.transform.z) / (osc::length(a.transform.z) * osc::length(b.transform.z));
            REQUIRE(cosine > 0.99999f);
        }
    }
    SUBCASE("translation")
    {
        osc::ScatterSettings s = sc.settings;
        s.translationSeed = 5;
        const Kept k = compare(before, sc.scatter(s));
        CHECK(k.common == n);
        CHECK(k.sameModel == n);
        CHECK(k.sameAxes == n);
        CHECK(k.samePosition < n / 100);
    }
}

TEST_CASE("stability: changing how instances look never changes which ones there are")
{
    const Scene sc;
    const osc::ScatterResult before = sc.scatter(sc.settings);
    const std::size_t n = before.count();

    SUBCASE("the probabilities of the models")
    {
        osc::ScatterSettings s = sc.settings;
        s.modelWeights = { 1.0f, 1.0f, 1.0f, 5.0f };
        const Kept k = compare(before, sc.scatter(s));
        CHECK(k.common == n);
        CHECK(k.samePosition == n);
        CHECK(k.sameAxes == n);
        CHECK(k.sameModel < n);
    }
    SUBCASE("the scale ranges")
    {
        osc::ScatterSettings s = sc.settings;
        s.transform.scale[2] = { 2.0f, 3.0f };
        const Kept k = compare(before, sc.scatter(s));
        CHECK(k.common == n);
        CHECK(k.samePosition == n);
        CHECK(k.sameModel == n);
    }
    SUBCASE("the rotation ranges and the alignment")
    {
        osc::ScatterSettings s = sc.settings;
        s.transform.rotationDegrees[2] = { 0.0f, 90.0f };
        s.transform.alignToNormal = 0.0f;
        const Kept k = compare(before, sc.scatter(s));
        CHECK(k.common == n);
        CHECK(k.sameModel == n);
        CHECK(k.sameLengths == n);
    }
}
