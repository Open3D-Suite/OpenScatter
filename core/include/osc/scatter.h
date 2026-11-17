// SPDX-License-Identifier: GPL-3.0-or-later
//
// OpenScatter core - the planar scatter.
//
// A pattern is tiled over the XY plane. Every point of every tile is a candidate with two properties
// that never change:
//   - a stable id, the hash of where it is born (pattern, seed, tile, index in the pattern), and
//   - a rank between 0 and 1, its position in the pattern's order.
// A candidate becomes an instance when its rank is below the density wanted at its position, which is
// the product of everything that modulates the density there (the density itself, the areas, the map).
// So raising the density only adds instances, lowering it only removes them, and editing an area or a
// map never moves the instances that remain. Each survivor is dropped onto the surface, filtered by
// slope and altitude, given a model and a transform - all of them hashes of its id - and written to the
// block of its tile. The blocks are in a fixed order: the result does not depend on the thread count.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "osc/area.h"
#include "osc/image.h"
#include "osc/math.h"
#include "osc/pattern.h"
#include "osc/surface_index.h"

namespace osc
{

struct Range
{
    float min = 0.0f;
    float max = 0.0f;
};

enum class MapSpace
{
    SurfaceUV, // sampled at the texture coordinates of the surface under the candidate
    PlanarXY,  // the image covers a rectangle of the XY plane
};

// A baked map that scales the density: 0 (black) = nothing, 1 (white) = the full density.
// The map is left out (and ScatterStats::mapIgnored set) when the image is not valid, when it is
// SurfaceUV and there is no surface with texture coordinates, or PlanarXY with an empty rectangle.
struct DensityMap
{
    const Image* image = nullptr; // not owned; null = no map
    MapSpace space = MapSpace::SurfaceUV;
    Rect rect; // PlanarXY only
    Wrap wrap = Wrap::Clamp;
};

// How each instance is oriented, turned, sized and nudged. Every range is sampled evenly with a hash
// of the instance's id, so the values survive any change to density, areas or maps.
struct TransformSettings
{
    float alignToNormal = 0.0f;                              // 0: stands along "up"; 1: along the surface normal
    Range rotationDegrees[3] = { {}, {}, { 0.0f, 360.0f } }; // about the instance's own X, Y and Z
    Range scale[3] = { { 1.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 1.0f } };
    bool uniformScale = true;  // one factor for the three axes, from scale[0]
    Range translation[3] = {}; // along the instance's axes before its rotation
};

struct ScatterSettings
{
    // --- placement ---
    // Side of one pattern tile. It fixes where every candidate is: change it and everything moves.
    float tileSize = 100.0f;
    // Instances per square unit where nothing thins them. It only moves the rank threshold, so changing
    // it adds or removes instances and moves none. The pattern cannot give more than
    // patternDensity(points, tileSize): above that the result is capped (ScatterStats::densityClamped).
    // It comes in whole pattern points per tile, and below one point per tile there is nothing: pick
    // the tile size for the densities wanted (tileSizeForDensity).
    float density = 0.01f;
    // Shifts and mirrors the pattern, and is part of every id: another seed is another scatter.
    std::uint64_t seed = 1;
    // Where to scatter: x in [minX, maxX), y in [minY, maxY). Left empty, the domain is the extent of
    // the surface, cut down to the reach of the include areas when there are any.
    Rect domain;
    // Never more instances than this (0 = no limit): above it the highest ranks go, evenly everywhere.
    std::uint64_t maxInstances = 0;

    // --- inputs (not owned; they must outlive the call) ---
    const SurfaceIndex* surface = nullptr; // null = flat ground at z = 0
    const AreaSet* areas = nullptr;
    DensityMap densityMap;

    // --- surface filters ---
    Vec3 up{ 0.0f, 0.0f, 1.0f }; // the vertical, for slope, altitude and orientation
    float upOffset = 0.0f;       // altitude of the origin: altitude = dot(position, up) + upOffset
    bool limitSlope = false;
    float slopeMinDegrees = 0.0f; // angle between the surface normal and "up"
    float slopeMaxDegrees = 90.0f;
    bool limitAltitude = false;
    float altitudeMin = 0.0f;
    float altitudeMax = 0.0f;

    // --- models ---
    // Relative probability of each model. Empty = a single model (index 0). Weights that are not
    // positive numbers count as 0; when none is positive there is nothing to scatter.
    std::vector<float> modelWeights;

    TransformSettings transform;

    // Reseeding one aspect changes only that aspect: positions and ids stay.
    std::uint64_t modelSeed = 0;
    std::uint64_t rotationSeed = 0;
    std::uint64_t scaleSeed = 0;
    std::uint64_t translationSeed = 0;
};

struct Instance
{
    Transform transform; // in the scatter's local space
    std::uint64_t id = 0;
    float rank = 0.0f; // keep those below a threshold to thin the result evenly
    std::uint32_t model = 0;
};

enum class ScatterStatus
{
    Ok, // also when there turned out to be nothing to scatter
    EmptyPattern,
    InvalidSettings, // tile size or density is not a usable number, or the domain is not finite
    NoDomain,        // no domain given and nothing to take it from
    TooManyTiles,    // the domain is too large for this tile size
    OutOfMemory,
};

// What a status means, in a few words of English.
const char* statusMessage(ScatterStatus status);

struct ScatterStats
{
    std::uint64_t candidates = 0; // pattern points inside the domain and below the density
    std::uint64_t rejectedByAreas = 0;
    std::uint64_t rejectedByMap = 0;
    std::uint64_t rejectedOffSurface = 0; // nothing under them
    std::uint64_t rejectedBySlope = 0;
    std::uint64_t rejectedByAltitude = 0;
    std::uint64_t instances = 0;
    float patternShare = 0.0f;   // the part of the pattern in use (1 = all of it)
    bool densityClamped = false; // more density asked than the pattern holds at this tile size
    bool limited = false;        // thinned to stay within maxInstances
    bool mapIgnored = false;     // a density map was given but could not be used
};

// The largest number of tiles one scatter may cover.
inline constexpr std::uint64_t kMaxTiles = std::uint64_t(1) << 20;

struct ScatterResult
{
    ScatterStatus status = ScatterStatus::Ok;

    // The tiles covered: tileColumns x tileRows of them starting at (tileX0, tileY0). "tiles" holds one
    // block per tile, row after row; inside a block the instances are in rank order.
    std::int32_t tileX0 = 0;
    std::int32_t tileY0 = 0;
    std::uint32_t tileColumns = 0;
    std::uint32_t tileRows = 0;
    std::vector<std::vector<Instance>> tiles;

    ScatterStats stats;

    std::size_t count() const
    {
        std::size_t n = 0;
        for (const std::vector<Instance>& tile : tiles)
            n += tile.size();
        return n;
    }
};

// "threads": how many to use; 0 = one per hardware thread. The result is the same for any value.
// Never throws: problems come back in ScatterResult::status.
ScatterResult scatterPlanar(const Pattern& pattern, const ScatterSettings& settings, unsigned threads = 0);

// A hash of every instance (id, model, rank and transform), in order: equal hashes, equal scatters.
std::uint64_t hashInstances(const ScatterResult& result);

// The density a pattern of that many points gives when all of it is used.
float patternDensity(std::size_t patternPoints, float tileSize);

// The tile size at which a pattern of that many points gives exactly that density.
float tileSizeForDensity(std::size_t patternPoints, float density);

} // namespace osc
