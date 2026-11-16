// SPDX-License-Identifier: GPL-3.0-or-later
//
// OpenScatter core - everything, in one include.
//
// The core is plain C++20 with no dependencies and no 3ds Max header: it takes meshes, polylines,
// images and settings, and returns instances. The 3ds Max plugin is a translator around it.

#pragma once

#include "osc/area.h"
#include "osc/detmath.h"
#include "osc/hash.h"
#include "osc/image.h"
#include "osc/math.h"
#include "osc/mesh.h"
#include "osc/parallel.h"
#include "osc/pattern.h"
#include "osc/scatter.h"
#include "osc/surface_index.h"
#include "osc/version.h"
