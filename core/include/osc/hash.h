// SPDX-License-Identifier: GPL-3.0-or-later
//
// OpenScatter core - hashing.
//
// Nothing in the core draws from a random-number generator with state. Everything random about an
// instance is a hash of its stable id, a seed and a tag for the aspect being decided, so the value does
// not depend on the order in which instances are computed, on the thread count or on what else changed.
//
// These functions are part of the algorithm: changing one changes every scatter (see kAlgorithmVersion).

#pragma once

#include <cstdint>

namespace osc
{

// SplitMix64 finaliser: a bijection of 64-bit integers that mixes all the bits.
inline std::uint64_t mix64(std::uint64_t x)
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

inline std::uint64_t hashCombine(std::uint64_t a, std::uint64_t b)
{
    return mix64(a ^ (mix64(b) + (a << 6) + (a >> 2)));
}

// A float in [0, 1) from the top 24 bits of a hash: every value is exact, and 1 is never returned.
inline float unitFloat(std::uint64_t h) { return static_cast<float>(h >> 40) * (1.0f / 16777216.0f); }

} // namespace osc
