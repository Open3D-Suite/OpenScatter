// SPDX-License-Identifier: GPL-3.0-or-later
//
// OpenScatter core - point patterns.
//
// A pattern is a finite set of points on the unit square that can be repeated edge to edge (its
// distances wrap around: a torus), stored in rank order: the first k points are well spread for every
// k. The planar scatter tiles a pattern over the ground and keeps the points whose rank is below the
// density wanted at their position, so raising the density only adds instances and nothing moves.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "osc/math.h"

namespace osc
{

struct Pattern
{
    std::vector<Vec2> points;   // in rank order, each coordinate in [0, 1)
    std::uint64_t id = 0;       // identifies the pattern in instance ids; the generator and the loader
                                // set it to hashPattern(), so equal patterns have equal ids

    std::size_t size() const { return points.size(); }
    bool empty() const { return points.empty(); }
};

// The largest pattern the generator and the loader accept.
inline constexpr std::size_t kMaxPatternPoints = std::size_t(1) << 20;

// A progressive blue-noise pattern of "count" points: every prefix of it is itself blue noise.
// Built by weighted sample elimination (Cem Yuksel, "Sample Elimination for Generating Poisson Disk
// Sample Sets", 2015): "oversample" times as many random candidates are thinned down to the pattern,
// which is then halved again and again to find the order. The result depends only on the arguments:
// the same on every machine. Returns an empty pattern when count is 0 or above kMaxPatternPoints.
Pattern makeProgressiveBlueNoise(std::size_t count, std::uint64_t seed, std::size_t oversample = 5);

// A hash of the points, in order.
std::uint64_t hashPattern(const Pattern& pattern);

// True when every point is finite and inside [0, 1).
bool isValid(const Pattern& pattern);

// How well spread the first "count" points are: the smallest distance between any two of them (wrapping
// around the edges) divided by the largest distance that many points could keep (a hexagonal packing).
// 1 is perfect; purely random points give about 0.02 at a thousand points. Costs count * count.
float prefixSpacing(const Pattern& pattern, std::size_t count);

// Pattern files (.oscp): a header, the points as little-endian floats and a checksum.
// Both return false and describe the problem in "error" (when given) instead of throwing.
bool savePattern(const Pattern& pattern, const std::filesystem::path& path, std::string* error = nullptr);
bool loadPattern(const std::filesystem::path& path, Pattern& pattern, std::string* error = nullptr);

} // namespace osc
