// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tests of the OpenScatter core: hashing, deterministic trigonometry, rectangles, images, threads.

#include <doctest/doctest.h>

#include <atomic>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <vector>

#include "helpers.h"

TEST_CASE("hash: unitFloat stays in [0, 1)")
{
    CHECK(osc::unitFloat(0) == 0.0f);
    CHECK(osc::unitFloat(~std::uint64_t(0)) < 1.0f);
    for (std::uint64_t i = 0; i < 10000; ++i)
    {
        const float u = osc::unitFloat(osc::mix64(i));
        CHECK(u >= 0.0f);
        CHECK(u < 1.0f);
    }
}

TEST_CASE("hash: values are fixed, spread and order-sensitive")
{
    // The hash functions are part of the algorithm: these values must never change.
    CHECK(osc::mix64(0) == 0xE220A8397B1DCDAFull);
    CHECK(osc::mix64(1) == 0x910A2DEC89025CC1ull);
    CHECK(osc::hashCombine(1, 2) != osc::hashCombine(2, 1));

    std::set<std::uint64_t> seen;
    for (std::uint64_t i = 0; i < 5000; ++i)
        seen.insert(osc::hashCombine(42, i));
    CHECK(seen.size() == 5000);

    double mean = 0.0;
    for (std::uint64_t i = 0; i < 100000; ++i)
        mean += osc::unitFloat(osc::hashCombine(9, i));
    CHECK(mean / 100000.0 == doctest::Approx(0.5).epsilon(0.01));
}

TEST_CASE("detmath: sine and cosine match the C library closely")
{
    for (int i = -4000; i <= 4000; ++i)
    {
        const double turns = i / 997.0;
        double s = 0.0, c = 0.0;
        osc::sinCosTurns(turns, s, c);
        CHECK(std::fabs(s - std::sin(turns * 6.283185307179586)) < 1e-12);
        CHECK(std::fabs(c - std::cos(turns * 6.283185307179586)) < 1e-12);
        CHECK(std::fabs(s * s + c * c - 1.0) < 1e-12);
    }
}

TEST_CASE("detmath: exact at the quarter turns, safe with nonsense")
{
    double s = 0.0, c = 0.0;
    osc::sinCosTurns(0.0, s, c);
    CHECK(s == 0.0);
    CHECK(c == 1.0);
    osc::sinCosTurns(0.25, s, c);
    CHECK(s == 1.0);
    CHECK(c == 0.0);
    osc::sinCosTurns(0.5, s, c);
    CHECK(s == 0.0);
    CHECK(c == -1.0);
    osc::sinCosTurns(-0.25, s, c);
    CHECK(s == -1.0);
    CHECK(c == 0.0);
    osc::sinCosTurns(3.0, s, c);
    CHECK(s == 0.0);
    CHECK(c == 1.0);

    osc::sinCosTurns(std::numeric_limits<double>::quiet_NaN(), s, c);
    CHECK(s == 0.0);
    CHECK(c == 1.0);
    osc::sinCosTurns(std::numeric_limits<double>::infinity(), s, c);
    CHECK(s == 0.0);
    CHECK(c == 1.0);

    CHECK(osc::cosDegrees(0.0f) == 1.0f);
    CHECK(osc::cosDegrees(90.0f) == 0.0f);
    CHECK(osc::cosDegrees(180.0f) == -1.0f);
    CHECK(osc::cosDegrees(60.0f) == doctest::Approx(0.5f));
}

TEST_CASE("math: clamp01 and rectangles")
{
    CHECK(osc::clamp01(-1.0f) == 0.0f);
    CHECK(osc::clamp01(0.25f) == 0.25f);
    CHECK(osc::clamp01(7.0f) == 1.0f);
    CHECK(osc::clamp01(std::numeric_limits<float>::quiet_NaN()) == 0.0f);

    const osc::Rect none;
    const osc::Rect a{ 0.0f, 0.0f, 10.0f, 10.0f };
    const osc::Rect b{ 5.0f, -5.0f, 20.0f, 5.0f };
    CHECK(none.empty());
    CHECK_FALSE(a.empty());
    CHECK(osc::intersect(a, none).empty());
    CHECK(osc::unite(a, none).maxX == 10.0f);
    const osc::Rect i = osc::intersect(a, b);
    CHECK(i.minX == 5.0f);
    CHECK(i.minY == 0.0f);
    CHECK(i.maxX == 10.0f);
    CHECK(i.maxY == 5.0f);
    const osc::Rect u = osc::unite(a, b);
    CHECK(u.minX == 0.0f);
    CHECK(u.minY == -5.0f);
    CHECK(u.maxX == 20.0f);
    CHECK(u.maxY == 10.0f);
    CHECK(osc::intersect(a, osc::Rect{ 11.0f, 0.0f, 12.0f, 1.0f }).empty());

    osc::Rect nan = a;
    nan.maxX = std::numeric_limits<float>::quiet_NaN();
    CHECK(nan.empty());
}

TEST_CASE("image: bilinear sampling")
{
    osc::Image image;
    image.width = 2;
    image.height = 2;
    image.pixels = { 0.0f, 1.0f,     // row at v = 0.25
                     2.0f, 3.0f };   // row at v = 0.75

    SUBCASE("pixel centres give the pixels")
    {
        CHECK(osc::sampleBilinear(image, 0.25f, 0.25f, osc::Wrap::Clamp) == 0.0f);
        CHECK(osc::sampleBilinear(image, 0.75f, 0.25f, osc::Wrap::Clamp) == 1.0f);
        CHECK(osc::sampleBilinear(image, 0.25f, 0.75f, osc::Wrap::Clamp) == 2.0f);
        CHECK(osc::sampleBilinear(image, 0.75f, 0.75f, osc::Wrap::Clamp) == 3.0f);
    }
    SUBCASE("in between, the blend")
    {
        CHECK(osc::sampleBilinear(image, 0.5f, 0.25f, osc::Wrap::Clamp) == doctest::Approx(0.5f));
        CHECK(osc::sampleBilinear(image, 0.5f, 0.5f, osc::Wrap::Clamp) == doctest::Approx(1.5f));
    }
    SUBCASE("clamp holds the edge, repeat wraps around")
    {
        CHECK(osc::sampleBilinear(image, -5.0f, 0.25f, osc::Wrap::Clamp) == 0.0f);
        CHECK(osc::sampleBilinear(image, 9.0f, 0.75f, osc::Wrap::Clamp) == 3.0f);
        CHECK(osc::sampleBilinear(image, 0.0f, 0.25f, osc::Wrap::Clamp) == 0.0f);
        CHECK(osc::sampleBilinear(image, 0.0f, 0.25f, osc::Wrap::Repeat) == doctest::Approx(0.5f));
        CHECK(osc::sampleBilinear(image, 1.25f, 0.25f, osc::Wrap::Repeat) == doctest::Approx(0.0f));
        CHECK(osc::sampleBilinear(image, -0.25f, 0.25f, osc::Wrap::Repeat) == doctest::Approx(1.0f));
    }
    SUBCASE("nonsense in, zero out")
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        CHECK(osc::sampleBilinear(image, nan, 0.5f, osc::Wrap::Clamp) == 0.0f);
        CHECK(osc::sampleBilinear(image, 0.5f, inf, osc::Wrap::Repeat) == 0.0f);
        CHECK(std::isfinite(osc::sampleBilinear(image, 1e30f, -1e30f, osc::Wrap::Repeat)));
        CHECK(std::isfinite(osc::sampleBilinear(image, 1e30f, -1e30f, osc::Wrap::Clamp)));

        osc::Image broken;
        CHECK(osc::sampleBilinear(broken, 0.5f, 0.5f, osc::Wrap::Clamp) == 0.0f);
        broken.width = 4;
        broken.height = 4;
        broken.pixels = { 1.0f };   // not 16 values
        CHECK_FALSE(broken.valid());
        CHECK(osc::sampleBilinear(broken, 0.5f, 0.5f, osc::Wrap::Clamp) == 0.0f);
        broken.width = -4;
        CHECK_FALSE(broken.valid());
    }
    SUBCASE("a single pixel is that value everywhere")
    {
        osc::Image one;
        one.width = one.height = 1;
        one.pixels = { 0.7f };
        CHECK(osc::sampleBilinear(one, 0.1f, 0.9f, osc::Wrap::Clamp) == doctest::Approx(0.7f));
        CHECK(osc::sampleBilinear(one, 3.3f, -2.0f, osc::Wrap::Repeat) == doctest::Approx(0.7f));
    }
}

TEST_CASE("parallel: every index once, for any thread count")
{
    for (const unsigned threads : { 0u, 1u, 3u, 32u, 500u })
    {
        std::vector<std::atomic<int>> hits(1000);
        std::atomic<unsigned> worstWorker{ 0 };
        osc::parallelFor(hits.size(), threads, [&](std::size_t i, unsigned worker) {
            ++hits[i];
            unsigned seen = worstWorker.load();
            while (worker > seen && !worstWorker.compare_exchange_weak(seen, worker))
            {
            }
        });
        for (const std::atomic<int>& h : hits)
            CHECK(h.load() == 1);
        CHECK(worstWorker.load() < std::max(1u, threads));
    }
    osc::parallelFor(0, 8, [](std::size_t, unsigned) { FAIL("no index, no call"); });
}

TEST_CASE("parallel: an exception in a worker comes back to the caller")
{
    CHECK_THROWS_AS(osc::parallelFor(100, 8, [](std::size_t i, unsigned) {
        if (i == 37)
            throw std::runtime_error("boom");
    }), std::runtime_error);
}
