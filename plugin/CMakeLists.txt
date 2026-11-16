# SPDX-License-Identifier: GPL-3.0-or-later
#
# OpenScatter.dlo: the plugin for 3ds Max 2027. Built only when OSC_BUILD_PLUGIN is ON, because it
# needs the 3ds Max SDK (OSC_MAXSDK: the SDK's "maxsdk" folder) and a release build: 3ds Max loads
# plugins built against the release C runtime only.
enable_language(RC)

# The SDK builds its plugins with /EHa; take the default /EHsc out of this directory so that the
# compiler does not warn about one overriding the other.
string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

if(NOT EXISTS "${OSC_MAXSDK}/include/max.h")
    message(FATAL_ERROR "3ds Max SDK not found at OSC_MAXSDK='${OSC_MAXSDK}'. "
                        "Pass -DOSC_MAXSDK=<path to the SDK's maxsdk folder>.")
endif()

# The version resource: the file properties of the .dlo carry the project version.
configure_file(src/version.rc.in "${CMAKE_CURRENT_BINARY_DIR}/version.rc" @ONLY)

add_library(OpenScatter SHARED
    src/plugin.h
    src/scatter_object.h
    src/dllmain.cpp
    src/scatter_object.cpp
    src/render_instancing.cpp
    src/render_mesh.cpp
    src/pflow_operator.cpp
    src/carrier.cpp
    src/viewport.cpp
    src/OpenScatter.def
    "${CMAKE_CURRENT_BINARY_DIR}/version.rc"
)

set_target_properties(OpenScatter PROPERTIES
    PREFIX ""
    SUFFIX ".dlo"
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    MSVC_RUNTIME_LIBRARY "MultiThreadedDLL"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugin")

# The SDK headers are not ours: no warnings from them.
target_include_directories(OpenScatter SYSTEM PRIVATE "${OSC_MAXSDK}/include")
target_link_directories(OpenScatter PRIVATE "${OSC_MAXSDK}/lib/x64/Release")

# The definitions and switches of the SDK's own property sheets (3dsmax.common.tools.settings.props).
target_compile_definitions(OpenScatter PRIVATE
    UNICODE _UNICODE
    WIN32_LEAN_AND_MEAN NOMINMAX _ADESK_3DSMAX_WINDOWS_
    WINVER=0x0601 _WIN32_WINNT=0x0601 _WIN32_WINDOWS=0x0601 _WIN32_IE=0x0800
    _WINDOWS _USRDLL
    _CRT_SECURE_NO_DEPRECATE _CRT_NONSTDC_NO_DEPRECATE _SCL_SECURE_NO_DEPRECATE
    ISOLATION_AWARE_ENABLED=1
    MODULE_NAME=OpenScatter.dlo)
target_compile_options(OpenScatter PRIVATE
    /GR /EHa /fp:precise /permissive- /utf-8 /Zc:__cplusplus /Zc:lambda /Zc:inline
    /external:anglebrackets /external:W0
    /W4 /wd4100 /wd4324)
if(OSC_WARNINGS_AS_ERRORS)
    target_compile_options(OpenScatter PRIVATE /WX)
endif()

target_link_libraries(OpenScatter PRIVATE
    osc_core
    core.lib geom.lib mesh.lib maxutil.lib paramblk2.lib gfx.lib Maxscrpt.lib particleflow.lib
    GraphicsDriver.lib GraphicsUtility.lib DefaultRenderItems.lib optimesh.lib DataBridge.lib MNMath.lib)
