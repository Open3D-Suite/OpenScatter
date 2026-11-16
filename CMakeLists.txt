# SPDX-License-Identifier: GPL-3.0-or-later
#
# OpenScatter: scatter for 3ds Max. This builds the core library (no 3ds Max needed), its tools and
# its tests, and - when asked, since it needs the 3ds Max SDK - the plugin for 3ds Max.
cmake_minimum_required(VERSION 3.24)
project(OpenScatter VERSION 0.1.0 LANGUAGES CXX)

option(OSC_BUILD_TOOLS "Build the command-line tools (osc_tilegen, osc_bench)" ON)
option(OSC_BUILD_TESTS "Build the tests of the core library" ON)
option(OSC_BUILD_PLUGIN "Build the 3ds Max plugin (needs the 3ds Max SDK: set OSC_MAXSDK)" OFF)
option(OSC_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
set(OSC_MAXSDK "$ENV{ADSK_3DSMAX_SDK_2027}" CACHE PATH "The maxsdk folder of the 3ds Max 2027 SDK (it holds include and lib)")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Every program in one folder of the build tree.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Compile options shared by every target of ours.
#
# The scatter must give the same instances on every machine, so floating point follows IEEE 754 to the
# letter: no fast-math, and no contraction of a * b + c into a fused multiply-add (which rounds once
# instead of twice and would differ between builds and CPUs). With MSVC that is /fp:precise without
# /fp:contract and without /arch:AVX2; with GCC and Clang, -ffp-contract=off.
function(osc_target_options target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /fp:precise)
        if(OSC_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -ffp-contract=off)
        if(OSC_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

add_subdirectory(core)

if(OSC_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

if(OSC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# The plugin links the engine built here with the same options, so it scatters bit for bit like the
# tests. 3ds Max only loads plugins built against the release C runtime: no plugin in a debug build.
if(OSC_BUILD_PLUGIN)
    if(MSVC AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_subdirectory(plugin)
    else()
        message(WARNING "OSC_BUILD_PLUGIN needs MSVC and a release build: the plugin is left out of this one.")
    endif()
endif()
