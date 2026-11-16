# SPDX-License-Identifier: GPL-3.0-or-later
#
# Command-line tools around the core library.

# osc_tilegen: generates the point patterns and writes them to pattern files.
add_executable(osc_tilegen tilegen/main.cpp)
target_link_libraries(osc_tilegen PRIVATE osc_core)
osc_target_options(osc_tilegen)

# osc_bench: times the engine against the targets of the project.
add_executable(osc_bench bench/main.cpp common/terrain.h)
target_include_directories(osc_bench PRIVATE common)
target_link_libraries(osc_bench PRIVATE osc_core)
osc_target_options(osc_bench)
