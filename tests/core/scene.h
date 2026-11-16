// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tests of the OpenScatter core: a scene that uses every feature at once, for the determinism and
// stability tests. Everything in it is built with hashes and plain arithmetic, so it is the same scene
// on every machine.

#pragma once

#include <vector>

#include "helpers.h"
#include "terrain.h"

namespace scene
{

inline osc::Polyline circle(float cx, float cy, float radius, int sides)
{
    osc::Polyline ring;
    for (int i = 0; i < sides; ++i)
    {
        double s = 0.0, c = 1.0;
        osc::sinCosTurns(static_cast<double>(i) / sides, s, c);
        ring.points.push_back({ cx + radius * static_cast<float>(c), cy + radius * static_cast<float>(s) });
    }
    return ring;
}

// The areas of the scene: a meadow that thins towards its edge, a winding road with soft shoulders
// and a rectangular clearing. "clearingX" moves the clearing, to test what an edit changes.
inline std::vector<osc::Area> areas(float clearingX = 150.0f)
{
    osc::Area meadow = helpers::area(osc::AreaMode::Include, { circle(200.0f, 200.0f, 185.0f, 96) }, -30.0f, 0.0f);

    osc::Polyline path;
    path.closed = false;
    for (int i = 0; i <= 80; ++i)
    {
        const float t = static_cast<float>(i) / 80.0f;
        path.points.push_back({ t * 400.0f, 230.0f + 120.0f * (osc::tools::fbm(t * 3.0f, 0.25f, 8) - 0.5f) });
    }
    osc::Area road = helpers::area(osc::AreaMode::Exclude, { path }, 0.0f, 5.0f, 9.0f);

    osc::Area clearing = helpers::area(osc::AreaMode::Exclude,
                                       { helpers::rectangle(clearingX, 90.0f, clearingX + 60.0f, 140.0f) }, 0.0f, 8.0f);
    return { meadow, road, clearing };
}

// A density map: smooth noise with some black and some white in it.
inline osc::Image densityMap(std::uint64_t seed = 12)
{
    osc::Image image;
    image.width = image.height = 128;
    image.pixels.resize(128 * 128);
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 128; ++x)
            image.pixels[static_cast<std::size_t>(y * 128 + x)] = osc::clamp01(
                (osc::tools::fbm(static_cast<float>(x) / 128.0f * 4.0f, static_cast<float>(y) / 128.0f * 4.0f, seed) - 0.3f) * 2.5f);
    return image;
}

// A terrain of 400 x 400 with hills up to 60 high, with texture coordinates.
inline osc::TriMesh terrain() { return osc::tools::makeTerrain(400.0f, 96, 60.0f, 21, true); }

// The settings of the scene. The surface, the areas and the map are the caller's, and must outlive
// the scatter.
inline osc::ScatterSettings settings(const osc::SurfaceIndex& surface, const osc::AreaSet& areaSet, const osc::Image& map)
{
    osc::ScatterSettings s;
    s.tileSize = 64.0f;   // 16 384 points per tile: up to 4 per square unit
    s.density = 2.0f;
    s.seed = 2026;
    s.surface = &surface;
    s.areas = &areaSet;
    s.densityMap.image = &map;
    s.densityMap.space = osc::MapSpace::SurfaceUV;
    s.limitSlope = true;
    s.slopeMaxDegrees = 35.0f;
    s.limitAltitude = true;
    s.altitudeMin = 26.0f;
    s.altitudeMax = 38.0f;
    s.modelWeights = { 3.0f, 1.0f, 2.0f };
    s.transform.alignToNormal = 0.7f;
    s.transform.rotationDegrees[0] = { -8.0f, 8.0f };
    s.transform.rotationDegrees[1] = { -8.0f, 8.0f };
    s.transform.uniformScale = false;
    s.transform.scale[0] = { 0.8f, 1.25f };
    s.transform.scale[1] = { 0.8f, 1.25f };
    s.transform.scale[2] = { 0.6f, 1.8f };
    s.transform.translation[2] = { -0.3f, 0.0f };
    return s;
}

} // namespace scene
