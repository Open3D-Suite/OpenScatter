// SPDX-License-Identifier: GPL-3.0-or-later
//
// osc_bench - times the scatter engine on the cases its design was measured with: one and ten million
// instances on a terrain of a million triangles, plain and with a density map and a slope limit, plus
// areas and the full transform. Every case runs on one thread and on all of them and the two results
// must be identical.
//
// The targets are those of the project plan: a million instances in under a second and ten million in
// under ten, with the ten-million buffer under a gigabyte. The exit code is 0 only when they are met
// and every result was identical between thread counts.
//
//   osc_bench [--quick]      --quick leaves out the ten-million cases

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <vector>

#include "osc/osc.h"
#include "terrain.h"

namespace
{

using Clock = std::chrono::steady_clock;

double secondsSince(Clock::time_point start)
{
    return std::chrono::duration<double>(Clock::now() - start).count();
}

struct Measured
{
    std::size_t instances = 0;
    std::uint64_t hash = 0;
    double seconds = 0.0; // the best of the runs
    osc::ScatterStatus status = osc::ScatterStatus::Ok;
};

Measured measure(const osc::Pattern& pattern, const osc::ScatterSettings& settings, unsigned threads, int runs)
{
    Measured m;
    m.seconds = 1e30;
    for (int run = 0; run < runs; ++run)
    {
        const Clock::time_point start = Clock::now();
        const osc::ScatterResult result = osc::scatterPlanar(pattern, settings, threads);
        m.seconds = std::min(m.seconds, secondsSince(start));
        m.status = result.status;
        if (run == 0)
        {
            m.instances = result.count();
            m.hash = osc::hashInstances(result);
        }
    }
    return m;
}

// A closed polygon approximating a circle.
osc::Polyline circle(float cx, float cy, float radius, int sides)
{
    osc::Polyline ring;
    for (int i = 0; i < sides; ++i)
    {
        double s = 0.0;
        double c = 1.0;
        osc::sinCosTurns(static_cast<double>(i) / sides, s, c);
        ring.points.push_back({ cx + radius * static_cast<float>(c), cy + radius * static_cast<float>(s) });
    }
    return ring;
}

} // namespace

int main(int argc, char** argv)
{
    bool quick = false;
    for (int i = 1; i < argc; ++i)
    {
        if (std::string_view(argv[i]) == "--quick")
            quick = true;
        else
        {
            std::puts("osc_bench [--quick]   times the OpenScatter engine; --quick leaves out the ten-million cases");
            return std::string_view(argv[i]) == "--help" ? 0 : 1;
        }
    }

    const unsigned threads = osc::hardwareThreads();
    std::printf("OpenScatter core %d.%d.%d (algorithm %u) - benchmark, %u hardware threads\n", osc::kVersionMajor,
                osc::kVersionMinor, osc::kVersionPatch, osc::kAlgorithmVersion, threads);

    // The pattern.
    Clock::time_point start = Clock::now();
    const std::size_t patternPoints = 16384;
    const osc::Pattern pattern = osc::makeProgressiveBlueNoise(patternPoints, 2026);
    std::printf("pattern: %zu points built in %.2f s\n", pattern.size(), secondsSince(start));
    for (const std::size_t count : { std::size_t(64), std::size_t(256), std::size_t(1024), std::size_t(4096) })
        std::printf("  first %5zu points: closest pair at %.2f of the ideal spacing\n", count,
                    osc::prefixSpacing(pattern, count));

    // The terrain: 1 000 x 1 000 units, about a million triangles.
    const float size = 1000.0f;
    start = Clock::now();
    osc::TriMesh terrain = osc::tools::makeTerrain(size, 707, 120.0f, 99);
    const std::size_t triangles = terrain.triangleCount();
    std::printf("terrain: %zu triangles generated in %.2f s\n", triangles, secondsSince(start));
    start = Clock::now();
    const osc::SurfaceIndex surface(std::move(terrain));
    std::printf("terrain index built in %.3f s (one thread)\n", secondsSince(start));

    // A density map: noise baked to an image that covers the terrain, as the plugin will bake a map.
    osc::Image map;
    map.width = map.height = 1024;
    map.pixels.resize(static_cast<std::size_t>(map.width) * static_cast<std::size_t>(map.height));
    for (int y = 0; y < map.height; ++y)
        for (int x = 0; x < map.width; ++x)
        {
            const float n = osc::tools::fbm((static_cast<float>(x) + 0.5f) / static_cast<float>(map.width) * 5.0f,
                                            (static_cast<float>(y) + 0.5f) / static_cast<float>(map.height) * 5.0f, 5);
            map.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(map.width) +
                       static_cast<std::size_t>(x)] = osc::clamp01((n - 0.35f) * 3.0f);
        }

    // Areas: everything inside a big circle, except a road with soft shoulders and three clearings.
    std::vector<osc::Area> areaList;
    {
        osc::Area meadow;
        meadow.mode = osc::AreaMode::Include;
        meadow.shapes.push_back(circle(500.0f, 500.0f, 480.0f, 256));
        meadow.falloffNear = -40.0f; // thins out over the last 40 units before its edge
        areaList.push_back(meadow);

        osc::Area road;
        road.mode = osc::AreaMode::Exclude;
        osc::Polyline path;
        path.closed = false;
        for (int i = 0; i <= 200; ++i)
        {
            const float t = static_cast<float>(i) / 200.0f;
            path.points.push_back({ t * size, 500.0f + 180.0f * (osc::tools::fbm(t * 4.0f, 0.5f, 3) - 0.5f) * 2.0f });
        }
        road.shapes.push_back(path);
        road.width = 12.0f;
        road.falloffFar = 6.0f;
        areaList.push_back(road);

        osc::Area clearings;
        clearings.mode = osc::AreaMode::Exclude;
        clearings.shapes.push_back(circle(300.0f, 300.0f, 60.0f, 64));
        clearings.shapes.push_back(circle(720.0f, 260.0f, 45.0f, 64));
        clearings.shapes.push_back(circle(640.0f, 760.0f, 80.0f, 64));
        clearings.falloffFar = 15.0f;
        areaList.push_back(clearings);
    }
    start = Clock::now();
    const osc::AreaSet areas(areaList);
    std::printf("areas: 3 areas, 5 shapes, prepared in %.4f s\n", secondsSince(start));

    std::printf("instance: %zu bytes\n\n", sizeof(osc::Instance));

    bool identical = true;
    bool statusOk = true;
    struct Row
    {
        const char* label;
        Measured one;
        Measured many;
    };
    std::vector<Row> rows;

    const auto base = [&](float perUnit) {
        osc::ScatterSettings s;
        s.tileSize = osc::tileSizeForDensity(patternPoints, perUnit); // the whole pattern = that density
        s.density = perUnit;
        s.surface = &surface;
        s.modelWeights = { 1.0f, 1.0f, 1.0f };
        s.transform.scale[0] = { 0.8f, 1.2f };
        return s;
    };
    const auto run = [&](const char* label, const osc::ScatterSettings& s, int runs) {
        Row row;
        row.label = label;
        row.one = measure(pattern, s, 1, runs);
        row.many = measure(pattern, s, threads, runs);
        const bool same = row.one.hash == row.many.hash && row.one.instances == row.many.instances;
        identical = identical && same;
        statusOk = statusOk && row.one.status == osc::ScatterStatus::Ok && row.many.status == osc::ScatterStatus::Ok;
        std::printf("%-52s %9zu instances | 1 thread %7.3f s | %u threads %7.3f s | identical: %s\n", label,
                    row.many.instances, row.one.seconds, threads, row.many.seconds, same ? "yes" : "NO");
        rows.push_back(row);
    };
    const auto withMapAndSlope = [&](osc::ScatterSettings s) {
        s.densityMap.image = &map;
        s.densityMap.space = osc::MapSpace::PlanarXY;
        s.densityMap.rect = { 0.0f, 0.0f, size, size };
        s.limitSlope = true;
        s.slopeMaxDegrees = 25.0f;
        return s;
    };

    run("1 per unit2, on terrain", base(1.0f), 5);
    run("1 per unit2, terrain + density map + slope limit", withMapAndSlope(base(1.0f)), 5);
    {
        osc::ScatterSettings s = base(1.0f);
        s.areas = &areas;
        run("1 per unit2, terrain + areas with falloff", s, 5);
    }
    {
        osc::ScatterSettings s = base(1.0f);
        s.transform.alignToNormal = 1.0f;
        s.transform.rotationDegrees[0] = { -10.0f, 10.0f };
        s.transform.rotationDegrees[1] = { -10.0f, 10.0f };
        s.transform.uniformScale = false;
        s.transform.scale[1] = { 0.8f, 1.2f };
        s.transform.scale[2] = { 0.6f, 1.6f };
        s.transform.translation[2] = { -0.2f, 0.0f };
        run("1 per unit2, terrain + every transform", s, 5);
    }
    if (!quick)
    {
        run("10 per unit2, on terrain", base(10.0f), 3);
        run("10 per unit2, terrain + density map + slope limit", withMapAndSlope(base(10.0f)), 3);
        {
            osc::ScatterSettings s = base(10.0f);
            s.maxInstances = 1000000;
            run("10 per unit2, limited to 1 000 000 instances", s, 3);
        }
    }

    // Stability: raising the density keeps every instance that was already there.
    {
        osc::ScatterSettings s = base(1.0f);
        s.density = 0.30f;
        const osc::ScatterResult low = osc::scatterPlanar(pattern, s, threads);
        s.density = 0.60f;
        const osc::ScatterResult high = osc::scatterPlanar(pattern, s, threads);
        std::vector<std::uint64_t> ids;
        ids.reserve(high.count());
        for (const auto& tile : high.tiles)
            for (const osc::Instance& inst : tile)
                ids.push_back(inst.id);
        std::sort(ids.begin(), ids.end());
        std::size_t kept = 0;
        for (const auto& tile : low.tiles)
            for (const osc::Instance& inst : tile)
                kept += std::binary_search(ids.begin(), ids.end(), inst.id) ? 1 : 0;
        std::printf("\nstability: density 0.30 -> 0.60: %zu -> %zu instances, %zu of the first set still there\n",
                    low.count(), high.count(), kept);
        identical = identical && kept == low.count();
    }

    // The verdict, against the plan's targets and the Phase 0 prototype (which measured, on an
    // i9-14900KF with 32 threads, 0.009 s for one million instances and 0.047 s for ten million,
    // with a 48-byte instance that had no transform matrix).
    std::printf("\n");
    bool targets = true;
    const auto verdict = [&](std::size_t rowIndex, const char* what, double target, double prototype) {
        if (rowIndex >= rows.size())
            return;
        const Measured& m = rows[rowIndex].many;
        const bool ok = m.seconds < target;
        targets = targets && ok;
        std::printf("%s: %.3f s with %u threads (target < %.0f s: %s; Phase 0 prototype: %.3f s, %.1f times that)\n",
                    what, m.seconds, threads, target, ok ? "met" : "MISSED", prototype, m.seconds / prototype);
    };
    verdict(0, "one million instances on a million triangles", 1.0, 0.009);
    if (!quick)
    {
        verdict(4, "ten million instances on a million triangles", 10.0, 0.047);
        const double gigabytes = static_cast<double>(rows[4].many.instances) * sizeof(osc::Instance) / 1e9;
        const bool ok = gigabytes < 1.0;
        targets = targets && ok;
        std::printf("buffer for ten million instances: %.2f GB (target < 1 GB: %s)\n", gigabytes,
                    ok ? "met" : "MISSED");
    }
    std::printf("identical with 1 and %u threads, and stable when the density rises: %s\n", threads,
                identical ? "yes" : "NO");
    if (!statusOk)
        std::printf("a scatter did not finish: see the instance counts above\n");

    return (targets && identical && statusOk) ? 0 : 1;
}
