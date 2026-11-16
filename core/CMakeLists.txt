# SPDX-License-Identifier: GPL-3.0-or-later
#
# osc_core: the scatter engine. Plain C++20, no dependencies, no 3ds Max.
add_library(osc_core STATIC
    include/osc/area.h
    include/osc/detmath.h
    include/osc/hash.h
    include/osc/image.h
    include/osc/math.h
    include/osc/mesh.h
    include/osc/osc.h
    include/osc/parallel.h
    include/osc/pattern.h
    include/osc/scatter.h
    include/osc/surface_index.h
    include/osc/version.h
    src/area.cpp
    src/pattern.cpp
    src/scatter.cpp
    src/surface_index.cpp
)
target_include_directories(osc_core PUBLIC include)
target_compile_features(osc_core PUBLIC cxx_std_20)
osc_target_options(osc_core)

find_package(Threads REQUIRED)
target_link_libraries(osc_core PUBLIC Threads::Threads)
