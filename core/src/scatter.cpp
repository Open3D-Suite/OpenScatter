// SPDX-License-Identifier: GPL-3.0-or-later
//
// OpenScatter core - the planar scatter.
//
// Nothing here may depend on the order of evaluation or on the machine: every decision about a
// candidate is a function of its tile, its index and the settings; the arithmetic is IEEE addition,
// multiplication, division and square root (see detmath.h for the trigonometry).

#include "osc/scatter.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <exception>
#include <limits>

#include "osc/detmath.h"
#include "osc/hash.h"
#include "osc/parallel.h"

namespace osc
{

namespace
{

// Which aspect of a scatter or of an instance a hash decides. These numbers are part of the algorithm:
// changing one changes every scatter that uses that aspect (see kAlgorithmVersion).
constexpr std::uint64_t kTagOffsetX = 0x101;
constexpr std::uint64_t kTagOffsetY = 0x102;
constexpr std::uint64_t kTagSymmetry = 0x103;
constexpr std::uint64_t kTagModel = 0x201;
constexpr std::uint64_t kTagRotation = 0x301;    // + axis
constexpr std::uint64_t kTagScale = 0x401;       // + axis
constexpr std::uint64_t kTagTranslation = 0x501; // + axis

float finiteOr(float v, float fallback)
{
    return std::isfinite(v) ? v : fallback;
}

Range finiteOr(Range r, float fallback)
{
    return { finiteOr(r.min, fallback), finiteOr(r.max, fallback) };
}

float sample(Range r, std::uint64_t key, std::uint64_t tag)
{
    return r.min + (r.max - r.min) * unitFloat(hashCombine(key, tag));
}

// A candidate that passed every filter: where it stands and the surface normal there.
struct Candidate
{
    Vec3 position;
    Vec3 normal;
};

enum class Outcome
{
    Outside, // not in the domain: not a candidate at all
    Areas,
    Map,
    OffSurface,
    Slope,
    Altitude,
    Accepted,
};

// Everything a scatter needs, worked out once from the settings and then only read by the threads.
struct Engine
{
    // placement
    std::vector<Vec2> points; // the part of the pattern in use, mirrored as the seed says
    std::vector<float> ranks;
    double tile = 1.0;
    double offsetX = 0.0;
    double offsetY = 0.0;
    Rect domain;
    float share = 0.0f; // the rank below which a candidate exists where nothing thins it
    std::uint64_t idBase = 0;

    // density
    const AreaSet* areas = nullptr;
    const Image* map = nullptr;
    bool mapOnSurface = false;
    Wrap mapWrap = Wrap::Clamp;
    double mapMinX = 0.0, mapMinY = 0.0, mapPerUnitX = 0.0, mapPerUnitY = 0.0;

    // surface
    const SurfaceIndex* surface = nullptr;
    Vec3 up{ 0.0f, 0.0f, 1.0f };
    float upOffset = 0.0f;
    bool limitSlope = false;
    float slopeCosLow = 0.0f;  // cosine at the largest angle allowed
    float slopeCosHigh = 0.0f; // cosine at the smallest
    bool limitAltitude = false;
    float altitudeMin = 0.0f;
    float altitudeMax = 0.0f;

    // models: only those that can be chosen
    std::vector<std::uint32_t> models;
    std::vector<float> cumulative;
    std::uint64_t modelSeed = 0;

    // transform
    float align = 0.0f;
    bool turns[3] = { false, false, false };
    Range rotation[3];
    bool uniformScale = true;
    Range scale[3];
    bool moves = false;
    Range translation[3];
    std::uint64_t rotationSeed = 0;
    std::uint64_t scaleSeed = 0;
    std::uint64_t translationSeed = 0;

    Outcome place(std::int32_t tx, std::int32_t ty, std::size_t i, Candidate& c) const;
    void finish(const Candidate& c, std::uint64_t tileKey, std::size_t i, Instance& out) const;
};

Outcome Engine::place(std::int32_t tx, std::int32_t ty, std::size_t i, Candidate& c) const
{
    const Vec2 p = points[i];
    const float x = static_cast<float>((static_cast<double>(tx) + p.x) * tile + offsetX);
    const float y = static_cast<float>((static_cast<double>(ty) + p.y) * tile + offsetY);
    if (!(x >= domain.minX && x < domain.maxX && y >= domain.minY && y < domain.maxY))
        return Outcome::Outside;

    // The density wanted here, as a share of the pattern. Every factor is at most 1, so the product
    // only goes down and the candidate can be dropped as soon as its rank is not below it.
    const float rank = ranks[i];
    float wanted = share;
    if (areas)
    {
        wanted *= areas->weight(x, y);
        if (!(rank < wanted))
            return Outcome::Areas;
    }
    if (map && !mapOnSurface)
    {
        const float u = static_cast<float>((x - mapMinX) * mapPerUnitX);
        const float v = static_cast<float>((y - mapMinY) * mapPerUnitY);
        wanted *= clamp01(sampleBilinear(*map, u, v, mapWrap));
        if (!(rank < wanted))
            return Outcome::Map;
    }

    c.position = { x, y, 0.0f };
    c.normal = { 0.0f, 0.0f, 1.0f };
    if (surface)
    {
        SurfaceHit hit;
        if (!surface->project(x, y, hit))
            return Outcome::OffSurface;
        c.position.z = hit.z;
        c.normal = hit.normal;
        if (map && mapOnSurface)
        {
            const Vec2 uv = surface->uvAt(hit);
            wanted *= clamp01(sampleBilinear(*map, uv.x, uv.y, mapWrap));
            if (!(rank < wanted))
                return Outcome::Map;
        }
    }

    if (limitSlope)
    {
        const float cosine = dot(c.normal, up);
        if (!(cosine >= slopeCosLow && cosine <= slopeCosHigh))
            return Outcome::Slope;
    }
    if (limitAltitude)
    {
        const float altitude = dot(c.position, up) + upOffset;
        if (!(altitude >= altitudeMin && altitude <= altitudeMax))
            return Outcome::Altitude;
    }
    return Outcome::Accepted;
}

void Engine::finish(const Candidate& c, std::uint64_t tileKey, std::size_t i, Instance& out) const
{
    const std::uint64_t id = hashCombine(tileKey, i);
    out.id = id;
    out.rank = ranks[i];

    // Model: by probability.
    if (models.size() <= 1)
    {
        out.model = models.empty() ? 0u : models[0];
    }
    else
    {
        const float u = unitFloat(hashCombine(hashCombine(id, modelSeed), kTagModel));
        const std::size_t k =
            static_cast<std::size_t>(std::upper_bound(cumulative.begin(), cumulative.end(), u) - cumulative.begin());
        out.model = models[std::min(k, models.size() - 1)];
    }

    // Base orientation: the instance's Z goes along a blend of the vertical and the surface normal,
    // by the shortest rotation that takes the Z axis there (no twist).
    Vec3 dir = up;
    if (align > 0.0f)
    {
        const Vec3 blend = up + (c.normal - up) * align;
        const float len = length(blend);
        if (len > 1e-6f)
            dir = blend * (1.0f / len);
    }
    Vec3 bx{ 1.0f, 0.0f, 0.0f };
    Vec3 by{ 0.0f, -1.0f, 0.0f }; // straight down: half a turn about X
    if (dir.z > -0.999999f)
    {
        const float a = 1.0f / (1.0f + dir.z);
        const float xy = dir.x * dir.y * a;
        bx = { 1.0f - dir.x * dir.x * a, 0.0f - xy, 0.0f - dir.x };
        by = { 0.0f - xy, 1.0f - dir.y * dir.y * a, 0.0f - dir.y };
    }
    const Vec3 bz = dir;

    // Random rotation about the instance's own axes: X first, then Y, then Z.
    Vec3 axes[3] = { { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } };
    if (turns[0] || turns[1] || turns[2])
    {
        const std::uint64_t key = hashCombine(id, rotationSeed);
        float s = 0.0f;
        float co = 1.0f;
        if (turns[0])
        {
            sinCosDegrees(sample(rotation[0], key, kTagRotation + 0), s, co);
            for (Vec3& v : axes)
                v = { v.x, co * v.y - s * v.z, s * v.y + co * v.z };
        }
        if (turns[1])
        {
            sinCosDegrees(sample(rotation[1], key, kTagRotation + 1), s, co);
            for (Vec3& v : axes)
                v = { co * v.x + s * v.z, v.y, co * v.z - s * v.x };
        }
        if (turns[2])
        {
            sinCosDegrees(sample(rotation[2], key, kTagRotation + 2), s, co);
            for (Vec3& v : axes)
                v = { co * v.x - s * v.y, s * v.x + co * v.y, v.z };
        }
    }

    // Random scale, along the model's own axes (so it never shears).
    const std::uint64_t scaleKey = hashCombine(id, scaleSeed);
    float factor[3];
    factor[0] = sample(scale[0], scaleKey, kTagScale + 0);
    factor[1] = uniformScale ? factor[0] : sample(scale[1], scaleKey, kTagScale + 1);
    factor[2] = uniformScale ? factor[0] : sample(scale[2], scaleKey, kTagScale + 2);

    out.transform.x = (bx * axes[0].x + by * axes[0].y + bz * axes[0].z) * factor[0];
    out.transform.y = (bx * axes[1].x + by * axes[1].y + bz * axes[1].z) * factor[1];
    out.transform.z = (bx * axes[2].x + by * axes[2].y + bz * axes[2].z) * factor[2];

    // Random translation, along the base orientation (Z = away from the ground).
    out.transform.t = c.position;
    if (moves)
    {
        const std::uint64_t key = hashCombine(id, translationSeed);
        out.transform.t = out.transform.t + bx * sample(translation[0], key, kTagTranslation + 0) +
                          by * sample(translation[1], key, kTagTranslation + 1) +
                          bz * sample(translation[2], key, kTagTranslation + 2);
    }
}

// What one thread keeps to itself: the candidates of the tile it is working on that passed every
// filter, and its counts. The counts are summed at the end: sums of whole numbers, so the order the
// threads ran in cannot show.
struct Accepted
{
    Candidate candidate;
    std::uint32_t index;
};

struct Worker
{
    std::vector<Accepted> accepted;
    std::uint64_t candidates = 0;
    std::uint64_t byAreas = 0;
    std::uint64_t byMap = 0;
    std::uint64_t offSurface = 0;
    std::uint64_t bySlope = 0;
    std::uint64_t byAltitude = 0;
};

void run(const Pattern& pattern, const ScatterSettings& s, unsigned threads, ScatterResult& result)
{
    const std::size_t n = pattern.points.size();
    if (n == 0)
    {
        result.status = ScatterStatus::EmptyPattern;
        return;
    }
    if (!(std::isfinite(s.tileSize) && s.tileSize > 0.0f) || !(std::isfinite(s.density) && s.density >= 0.0f) ||
        (!s.domain.empty() && !s.domain.finite()))
    {
        result.status = ScatterStatus::InvalidSettings;
        return;
    }

    // --- where: the domain given, cut down to the surface and to the reach of the include areas ---
    Rect domain;
    bool haveDomain = false;
    const auto narrow = [&](const Rect& r) {
        domain = haveDomain ? intersect(domain, r) : r;
        haveDomain = true;
    };
    if (!s.domain.empty())
        narrow(s.domain);
    if (s.surface)
        narrow(s.surface->bounds());
    if (s.areas && s.areas->hasIncludes())
        narrow(s.areas->includeBounds());
    if (!haveDomain)
    {
        result.status = ScatterStatus::NoDomain;
        return;
    }
    if (domain.empty())
        return;

    Engine e;
    e.domain = domain;
    e.tile = s.tileSize;

    // --- how many: the density as a share of the pattern, and the points that share covers ---
    const double wantedShare = static_cast<double>(s.density) * e.tile * e.tile / static_cast<double>(n);
    result.stats.densityClamped = wantedShare > 1.000001;
    e.share = static_cast<float>(std::min(1.0, wantedShare));
    const auto rankOf = [n](std::size_t i) { return (static_cast<float>(i) + 0.5f) / static_cast<float>(n); };
    std::size_t limit = 0; // the first index whose rank is not below the share
    for (std::size_t low = 0, high = n; low < high;)
    {
        const std::size_t mid = low + (high - low) / 2;
        if (rankOf(mid) < e.share)
            limit = low = mid + 1;
        else
            high = mid;
    }
    if (limit == 0)
        return;

    // --- the seed: a shift of the tiling and one of the eight symmetries of the square ---
    e.offsetX = static_cast<double>(unitFloat(hashCombine(s.seed, kTagOffsetX))) * e.tile;
    e.offsetY = static_cast<double>(unitFloat(hashCombine(s.seed, kTagOffsetY))) * e.tile;
    const std::uint64_t symmetry = hashCombine(s.seed, kTagSymmetry);
    e.idBase = hashCombine(pattern.id, s.seed);

    const double fx0 = std::floor((domain.minX - e.offsetX) / e.tile);
    const double fy0 = std::floor((domain.minY - e.offsetY) / e.tile);
    const double fx1 = std::floor((domain.maxX - e.offsetX) / e.tile);
    const double fy1 = std::floor((domain.maxY - e.offsetY) / e.tile);
    const double tileLimit = 1e9;
    if (!(std::fabs(fx0) < tileLimit && std::fabs(fy0) < tileLimit && std::fabs(fx1) < tileLimit &&
          std::fabs(fy1) < tileLimit) ||
        (fx1 - fx0 + 1.0) * (fy1 - fy0 + 1.0) > static_cast<double>(kMaxTiles))
    {
        result.status = ScatterStatus::TooManyTiles;
        return;
    }
    const std::int32_t tx0 = static_cast<std::int32_t>(fx0);
    const std::int32_t ty0 = static_cast<std::int32_t>(fy0);
    const std::size_t columns = static_cast<std::size_t>(fx1 - fx0) + 1;
    const std::size_t rows = static_cast<std::size_t>(fy1 - fy0) + 1;
    const std::size_t tileCount = columns * rows;

    // --- density modulation ---
    e.areas = (s.areas && !s.areas->empty()) ? s.areas : nullptr;
    if (s.densityMap.image)
    {
        const DensityMap& m = s.densityMap;
        bool usable = m.image->valid();
        if (usable && m.space == MapSpace::SurfaceUV)
        {
            usable = s.surface && s.surface->hasUVs();
            e.mapOnSurface = true;
        }
        else if (usable)
        {
            const double w = static_cast<double>(m.rect.maxX) - m.rect.minX;
            const double h = static_cast<double>(m.rect.maxY) - m.rect.minY;
            usable = m.rect.finite() && w > 0.0 && h > 0.0;
            if (usable)
            {
                e.mapMinX = m.rect.minX;
                e.mapMinY = m.rect.minY;
                e.mapPerUnitX = 1.0 / w;
                e.mapPerUnitY = 1.0 / h;
            }
        }
        e.map = usable ? m.image : nullptr;
        e.mapWrap = m.wrap;
        result.stats.mapIgnored = !usable;
    }

    // --- surface filters ---
    e.surface = s.surface;
    if (isFinite(s.up))
    {
        const double len = std::sqrt(static_cast<double>(s.up.x) * s.up.x + static_cast<double>(s.up.y) * s.up.y +
                                     static_cast<double>(s.up.z) * s.up.z);
        if (len > 1e-12 && std::isfinite(len))
            e.up = { static_cast<float>(s.up.x / len), static_cast<float>(s.up.y / len),
                     static_cast<float>(s.up.z / len) };
    }
    e.upOffset = finiteOr(s.upOffset, 0.0f);
    e.limitSlope = s.limitSlope;
    {
        // At the ends of the scale there is no limit, whatever rounding does to a cosine.
        const float lowAngle = std::clamp(finiteOr(s.slopeMinDegrees, 0.0f), 0.0f, 180.0f);
        const float highAngle = std::clamp(finiteOr(s.slopeMaxDegrees, 180.0f), 0.0f, 180.0f);
        e.slopeCosHigh = lowAngle <= 0.0f ? std::numeric_limits<float>::infinity() : cosDegrees(lowAngle);
        e.slopeCosLow = highAngle >= 180.0f ? -std::numeric_limits<float>::infinity() : cosDegrees(highAngle);
    }
    e.limitAltitude = s.limitAltitude;
    e.altitudeMin = std::isnan(s.altitudeMin) ? -std::numeric_limits<float>::infinity() : s.altitudeMin;
    e.altitudeMax = std::isnan(s.altitudeMax) ? std::numeric_limits<float>::infinity() : s.altitudeMax;

    // --- models ---
    if (!s.modelWeights.empty())
    {
        double total = 0.0;
        for (const float w : s.modelWeights)
            if (std::isfinite(w) && w > 0.0f)
                total += w;
        if (!(total > 0.0))
            return; // every model has probability 0
        double running = 0.0;
        for (std::size_t m = 0; m < s.modelWeights.size(); ++m)
        {
            const float w = s.modelWeights[m];
            if (!(std::isfinite(w) && w > 0.0f))
                continue;
            running += w;
            e.models.push_back(static_cast<std::uint32_t>(m));
            e.cumulative.push_back(static_cast<float>(running / total));
        }
        e.cumulative.back() = 2.0f; // above any value drawn: the last model takes the rest
    }
    e.modelSeed = s.modelSeed;

    // --- transform ---
    const TransformSettings& t = s.transform;
    e.align = clamp01(t.alignToNormal);
    for (int axis = 0; axis < 3; ++axis)
    {
        e.rotation[axis] = finiteOr(t.rotationDegrees[axis], 0.0f);
        e.turns[axis] = e.rotation[axis].min != 0.0f || e.rotation[axis].max != 0.0f;
        e.scale[axis] = finiteOr(t.scale[axis], 1.0f);
        e.translation[axis] = finiteOr(t.translation[axis], 0.0f);
        e.moves = e.moves || e.translation[axis].min != 0.0f || e.translation[axis].max != 0.0f;
    }
    e.uniformScale = t.uniformScale;
    e.rotationSeed = s.rotationSeed;
    e.scaleSeed = s.scaleSeed;
    e.translationSeed = s.translationSeed;

    // --- the part of the pattern in use ---
    e.points.resize(limit);
    e.ranks.resize(limit);
    for (std::size_t i = 0; i < limit; ++i)
    {
        Vec2 p = pattern.points[i];
        if (symmetry & 1u)
            std::swap(p.x, p.y);
        if (symmetry & 2u)
            p.x = 1.0f - p.x;
        if (symmetry & 4u)
            p.y = 1.0f - p.y;
        e.points[i] = p;
        e.ranks[i] = rankOf(i);
    }

    if (threads == 0)
        threads = hardwareThreads();
    threads = static_cast<unsigned>(std::min<std::size_t>(std::min(threads, 1024u), tileCount));

    const auto tileX = [&](std::size_t tile) { return tx0 + static_cast<std::int32_t>(tile % columns); };
    const auto tileY = [&](std::size_t tile) { return ty0 + static_cast<std::int32_t>(tile / columns); };

    // --- the instance limit: count what each pattern index would give over the whole domain, and
    //     keep the longest prefix of the pattern that stays within the limit. Cutting by rank removes
    //     instances evenly everywhere, and the cut does not depend on the thread count. ---
    if (s.maxInstances > 0 && static_cast<std::uint64_t>(tileCount) * limit > s.maxInstances)
    {
        std::vector<std::vector<std::uint32_t>> perWorker(threads);
        parallelFor(tileCount, threads, [&](std::size_t tile, unsigned worker) {
            std::vector<std::uint32_t>& counts = perWorker[worker];
            if (counts.empty())
                counts.assign(limit, 0);
            const std::int32_t tx = tileX(tile);
            const std::int32_t ty = tileY(tile);
            Candidate c;
            for (std::size_t i = 0; i < limit; ++i)
                if (e.place(tx, ty, i, c) == Outcome::Accepted)
                    ++counts[i];
        });
        std::vector<std::uint64_t> total(limit, 0);
        for (const std::vector<std::uint32_t>& counts : perWorker)
            for (std::size_t i = 0; i < counts.size(); ++i)
                total[i] += counts[i];
        std::uint64_t kept = 0;
        std::size_t keep = 0;
        while (keep < limit && kept + total[keep] <= s.maxInstances)
            kept += total[keep++];
        if (keep < limit)
        {
            limit = keep;
            result.stats.limited = true;
        }
    }
    result.stats.patternShare = static_cast<float>(static_cast<double>(limit) / static_cast<double>(n));
    if (limit == 0)
        return;

    // --- the scatter itself: one block per tile ---
    result.tileX0 = tx0;
    result.tileY0 = ty0;
    result.tileColumns = static_cast<std::uint32_t>(columns);
    result.tileRows = static_cast<std::uint32_t>(rows);
    result.tiles.resize(tileCount);
    std::vector<Worker> workers(threads);
    parallelFor(tileCount, threads, [&](std::size_t tile, unsigned worker) {
        const std::int32_t tx = tileX(tile);
        const std::int32_t ty = tileY(tile);
        Worker& w = workers[worker];

        // First which candidates of the tile stay, then a block of exactly that size for them: the
        // result holds no spare memory, and no instance is copied while its block grows.
        w.accepted.clear();
        std::uint64_t candidates = 0, byAreas = 0, byMap = 0, offSurface = 0, bySlope = 0, byAltitude = 0;
        Candidate c;
        for (std::size_t i = 0; i < limit; ++i)
        {
            const Outcome outcome = e.place(tx, ty, i, c);
            if (outcome == Outcome::Outside)
                continue;
            ++candidates;
            switch (outcome)
            {
            case Outcome::Areas:
                ++byAreas;
                break;
            case Outcome::Map:
                ++byMap;
                break;
            case Outcome::OffSurface:
                ++offSurface;
                break;
            case Outcome::Slope:
                ++bySlope;
                break;
            case Outcome::Altitude:
                ++byAltitude;
                break;
            default:
                w.accepted.push_back({ c, static_cast<std::uint32_t>(i) });
                break;
            }
        }
        w.candidates += candidates;
        w.byAreas += byAreas;
        w.byMap += byMap;
        w.offSurface += offSurface;
        w.bySlope += bySlope;
        w.byAltitude += byAltitude;
        if (w.accepted.empty())
            return;

        const std::uint64_t tileKey =
            hashCombine(hashCombine(e.idBase, static_cast<std::uint64_t>(static_cast<std::int64_t>(tx))),
                        static_cast<std::uint64_t>(static_cast<std::int64_t>(ty)));
        std::vector<Instance>& out = result.tiles[tile];
        out.reserve(w.accepted.size());
        for (const Accepted& a : w.accepted)
        {
            out.emplace_back();
            e.finish(a.candidate, tileKey, a.index, out.back());
        }
    });

    for (const Worker& w : workers)
    {
        result.stats.candidates += w.candidates;
        result.stats.rejectedByAreas += w.byAreas;
        result.stats.rejectedByMap += w.byMap;
        result.stats.rejectedOffSurface += w.offSurface;
        result.stats.rejectedBySlope += w.bySlope;
        result.stats.rejectedByAltitude += w.byAltitude;
    }
    result.stats.instances = result.count();
}

} // namespace

const char* statusMessage(ScatterStatus status)
{
    switch (status)
    {
    case ScatterStatus::Ok:
        return "ok";
    case ScatterStatus::EmptyPattern:
        return "the pattern has no points";
    case ScatterStatus::InvalidSettings:
        return "the tile size, the density or the domain is not a usable number";
    case ScatterStatus::NoDomain:
        return "there is no domain: no rectangle, no surface and no include area";
    case ScatterStatus::TooManyTiles:
        return "the domain is too large for this tile size";
    case ScatterStatus::OutOfMemory:
        return "out of memory";
    }
    return "unknown";
}

ScatterResult scatterPlanar(const Pattern& pattern, const ScatterSettings& settings, unsigned threads)
{
    ScatterResult result;
    try
    {
        run(pattern, settings, threads, result);
    }
    catch (const std::exception&)
    {
        // The only thing that throws in there is running out of memory, in one form or another.
        const ScatterStats kept = result.stats;
        result = ScatterResult{};
        result.stats.densityClamped = kept.densityClamped;
        result.stats.mapIgnored = kept.mapIgnored;
        result.status = ScatterStatus::OutOfMemory;
    }
    return result;
}

std::uint64_t hashInstances(const ScatterResult& result)
{
    std::uint64_t h = 0x4F5343494E535431ull;
    for (const std::vector<Instance>& tile : result.tiles)
    {
        for (const Instance& inst : tile)
        {
            h = hashCombine(h, inst.id);
            h = hashCombine(h,
                            (static_cast<std::uint64_t>(inst.model) << 32) | std::bit_cast<std::uint32_t>(inst.rank));
            const Vec3* vectors[4] = { &inst.transform.x, &inst.transform.y, &inst.transform.z, &inst.transform.t };
            for (const Vec3* v : vectors)
            {
                h = hashCombine(h, (static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(v->x)) << 32) |
                                       std::bit_cast<std::uint32_t>(v->y));
                h = hashCombine(h, std::bit_cast<std::uint32_t>(v->z));
            }
        }
    }
    return h;
}

float patternDensity(std::size_t patternPoints, float tileSize)
{
    if (!(std::isfinite(tileSize) && tileSize > 0.0f))
        return 0.0f;
    return static_cast<float>(static_cast<double>(patternPoints) / (static_cast<double>(tileSize) * tileSize));
}

float tileSizeForDensity(std::size_t patternPoints, float density)
{
    if (!(std::isfinite(density) && density > 0.0f) || patternPoints == 0)
        return 0.0f;
    return static_cast<float>(std::sqrt(static_cast<double>(patternPoints) / static_cast<double>(density)));
}

} // namespace osc
