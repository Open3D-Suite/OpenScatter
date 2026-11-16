// SPDX-License-Identifier: GPL-3.0-or-later
//
// OpenScatter plugin for 3ds Max - what the source files share.
//
// The plugin is a translator around the engine (core/, namespace osc): it pulls meshes and settings
// out of the scene, calls the engine and hands the one instance buffer it gets back to the renderers
// and to the viewport. Everything that touches the 3ds Max SDK lives in this folder.

#pragma once

#include <max.h>
#include <iparamb2.h>
#include <ifnpub.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "osc/osc.h"

#define OPENSCATTER_CLASS_ID Class_ID(0x6b2d17e4, 0x39c05a8f)
#define OPENSCATTER_OPERATOR_CLASS_ID Class_ID(0x2e94c6b1, 0x57a30d7c)
#define OPENSCATTER_INTERFACE Interface_ID(0x7c1f48a3, 0x1d6e92b5)

#define OPENSCATTER_VERSION_STRING "0.1.0"

extern HINSTANCE hInstance;

namespace oscmax
{

// A diagnostic log, off unless the environment variable OPENSCATTER_LOG names a file to append to.
// For finding out what a renderer or Particle Flow asked of the plugin; nothing depends on it.
bool logEnabled();
void logLine(const char* format, ...);

// ---------------------------------------------------------------------------------------------
// Parameters of the OpenScatter object (ParamBlock2). The ids are stored in scenes: never
// renumber them, only append.
// ---------------------------------------------------------------------------------------------
enum { kScatterParamBlock = 0 };

enum ScatterParam
{
    p_surfaces = 0,          // INode tab: the objects to scatter on
    p_models,                // INode tab: the objects to scatter
    p_modelProbabilities,    // float tab, parallel to p_models (a missing entry counts as 1)
    p_density,               // instances per square metre
    p_patternSize,           // side of one pattern tile in scene units; 0 = chosen from the density
    p_seed,
    p_maxInstances,          // never more instances than this (0 = no limit)
    p_alignToNormal,         // 0 = upright, 1 = along the surface normal
    p_rotationMin,           // degrees about the instance's own X, Y and Z
    p_rotationMax,
    p_scaleMin,              // factor along the instance's own X, Y and Z
    p_scaleMax,
    p_uniformScale,          // one factor for the three axes, from the X range
    p_limitSlope,
    p_slopeMin,              // degrees between the surface normal and the world Z
    p_slopeMax,
    p_limitAltitude,
    p_altitudeMin,           // world Z, scene units
    p_altitudeMax,
    p_renderRoute,           // RenderRoute
    p_meshMaxFaces,          // cap of the aggregate mesh (0 = no cap)
    p_viewportMode,          // ViewportMode
    p_viewportMaxInstances,  // 0 = chosen from the display driver
    p_viewportMaxTriangles,  // 0 = chosen from the display driver
    p_iconSize,
    p_material,              // the Multi/Sub-Object material that gathers the models' materials (kept by the plugin)
    p_threads,               // 0 = one per hardware thread (the result never depends on it)
    p_algorithmVersion,      // the distribution algorithm the object was created with
};

// Which output adapter hands the instances to the renderer. Exactly one is active for a render.
enum RenderRoute
{
    kRouteAuto = 0,          // picked from the current renderer
    kRouteInstancing,        // MaxSDK::RenderTimeInstancing::RenderTimeInstancingInterface (Arnold, V-Ray)
    kRouteParticleFlow,      // a Particle Flow source driven by our operator (Corona)
    kRouteMesh,              // one aggregate mesh from GetRenderMesh (renderers with no instancing route)
    kRouteNone,              // nothing is rendered
    kRouteCount
};

enum ViewportMode
{
    kViewNone = 0,
    kViewBoxes,              // the bounding box of each model
    kViewMeshes,             // the models themselves while they fit the budget, boxes otherwise
    kViewModeCount
};

// ---------------------------------------------------------------------------------------------
// The shared instance buffer
// ---------------------------------------------------------------------------------------------

// One model, as every output needs it. The mesh is in "pivot space": the model's object-space mesh
// already moved by its node's object offset, so an instance transform takes it straight to the world.
struct ModelData
{
    INode* node = nullptr;       // null: the slot is empty and no instance uses it
    float weight = 0.0f;         // what the engine gets (0 when the slot cannot be used)
    Mesh mesh;                   // pivot space; face material IDs are slots of the combined material
    Matrix3 pivotTM;             // the node's object offset: object space -> pivot space
    Box3 bounds;                 // of the mesh
    int materialBase = 0;        // first slot of this model in the combined material
    int materialSlots = 1;       // how many slots it takes
    Color wireColor = Color(0.6f, 0.6f, 0.6f);
};

// What changed since an output last looked. Each output keeps the value it built from.
struct BufferVersion
{
    std::uint64_t scatter = 0;   // the instances
    std::uint64_t models = 0;    // the model meshes
    bool operator==(const BufferVersion& o) const { return scatter == o.scatter && models == o.models; }
    bool operator!=(const BufferVersion& o) const { return !(*this == o); }
};

struct InstanceBuffer
{
    std::vector<ModelData> models;
    std::vector<osc::Instance> instances;                // world space, in the engine's fixed order
    std::vector<std::vector<std::uint32_t>> byModel;     // indices into "instances", one list per model

    osc::ScatterStatus status = osc::ScatterStatus::Ok;
    osc::ScatterStats stats;
    std::uint64_t hash = 0;          // osc::hashInstances of the result
    float tileSize = 0.0f;           // what the engine was given
    float engineDensity = 0.0f;      // instances per square scene unit
    double seconds = 0.0;            // surface extraction + scatter
    std::size_t surfaceTriangles = 0;
    Box3 worldBounds;                // every instance with its model
    BufferVersion version;
};

inline Matrix3 toMatrix3(const osc::Transform& m)
{
    return Matrix3(Point3(m.x.x, m.x.y, m.x.z), Point3(m.y.x, m.y.y, m.y.z), Point3(m.z.x, m.z.y, m.z.z),
                   Point3(m.t.x, m.t.y, m.t.z));
}

// How many slots a model's material takes in the combined Multi/Sub-Object material: one, or one
// per sub-material when it is itself a Multi/Sub-Object.
int materialSlotsOf(Mtl* mtl);

// The rank below which about "keep" of the buffer's instances fall: thinning by rank removes
// instances evenly everywhere instead of emptying a corner. 2 (above every rank) when all fit.
float rankThreshold(const InstanceBuffer& buffer, std::size_t keep);

// ---------------------------------------------------------------------------------------------
// The object
// ---------------------------------------------------------------------------------------------
class OpenScatterObject;

// The OpenScatter object behind a node or an object pointer, or null.
OpenScatterObject* getOpenScatter(INode* node);
OpenScatterObject* getOpenScatter(Object* object);

ClassDesc2* getOpenScatterDesc();
ClassDesc2* getOpenScatterOperatorDesc();

// What each renderer-facing adapter handed out for the last render: exactly one of them may have.
struct RenderReport
{
    std::atomic<std::uint64_t> instancingTargets{ 0 };   // instances handed out through the instancing interface
    // Particles the carrier holds as of its last evaluation. Not reset when a render starts: Particle
    // Flow keeps its particles between renders when nothing changed, and then does not ask again.
    std::atomic<std::uint64_t> particles{ 0 };
    std::atomic<std::uint64_t> meshInstances{ 0 };       // instances baked into the aggregate mesh
    std::atomic<std::uint64_t> meshFaces{ 0 };
    // Called when a render starts.
    void reset()
    {
        instancingTargets = 0;
        meshInstances = 0;
        meshFaces = 0;
    }
};

// ---------------------------------------------------------------------------------------------
// carrier.cpp - the Particle Flow source that carries the instances into Corona
// ---------------------------------------------------------------------------------------------

// Registers the scene notifications the carriers depend on. Called once when the plugin loads.
void registerCarrierNotifications();
void unregisterCarrierNotifications();

// Asks for syncCarriers() to run on the main thread as soon as 3ds Max is idle. Safe from anywhere.
void requestCarrierSync();

// Brings every OpenScatter object's carrier and combined material in line with its settings and the
// current renderer. Main thread only. "allowDelete": also remove the carriers nobody needs.
void syncCarriers(bool allowDelete);

// True when "partNode" (an operator's node) belongs to the carrier whose PF Source is "sourceNode".
bool carrierOwns(INode* sourceNode, INode* partNode);

// The name of the renderer a render would use now, in lower case ("" when there is none).
std::wstring currentRendererName();

// The route kRouteAuto stands for with that renderer.
RenderRoute routeForRenderer(const std::wstring& lowerCaseName);

// ---------------------------------------------------------------------------------------------
// viewport.cpp
// ---------------------------------------------------------------------------------------------
struct ViewportBudget
{
    std::size_t maxInstances = 0;
    std::size_t maxTriangles = 0;
    bool software = false;           // the display driver draws on the CPU
    std::wstring driver;             // what 3ds Max calls it
};

// The budget the display driver of this 3ds Max can carry, with the object's own limits applied
// (0 = automatic).
ViewportBudget viewportBudget(int maxInstancesParam, int maxTrianglesParam);

} // namespace oscmax
