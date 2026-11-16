// SPDX-License-Identifier: GPL-3.0-or-later
//
// OpenScatter core - versions.

#pragma once

#include <cstdint>

namespace osc
{

inline constexpr int kVersionMajor = 0;
inline constexpr int kVersionMinor = 1;
inline constexpr int kVersionPatch = 0;

// Version of the distribution algorithm: how ids, positions, models and transforms are derived from the
// settings. It changes only when the same settings would give different instances, so a scene can store
// the version it was made with and keep rendering the same way.
inline constexpr std::uint32_t kAlgorithmVersion = 1;

} // namespace osc
