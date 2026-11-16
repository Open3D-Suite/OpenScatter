# SPDX-License-Identifier: GPL-3.0-or-later
#
# Tests. For now those of the core library, which run without 3ds Max in a few seconds.
add_executable(osc_core_tests
    core/helpers.h
    core/scene.h
    core/test_main.cpp
    core/test_basics.cpp
    core/test_pattern.cpp
    core/test_surface_index.cpp
    core/test_area.cpp
    core/test_scatter.cpp
    core/test_determinism.cpp
    core/test_stability.cpp
)
target_link_libraries(osc_core_tests PRIVATE osc_core)
# doctest (third_party/doctest, MIT) and the terrain generator shared with the benchmark.
target_include_directories(osc_core_tests SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party)
target_include_directories(osc_core_tests PRIVATE ${PROJECT_SOURCE_DIR}/tools/common)
osc_target_options(osc_core_tests)

add_test(NAME osc_core_tests COMMAND osc_core_tests)
