// SPDX-License-Identifier: GPL-3.0-or-later
//
// OpenScatter plugin for 3ds Max - the OpenScatter object: parameters, references, the instance
// buffer and the published functions.

#include "scatter_object.h"

#include <MeshNormalSpec.h>
#include <Rendering/RenderTimeInstancing.h>
#include <object.h>
#include <triobj.h>
#include <units.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace oscmax
{

// ---------------------------------------------------------------------------------------------
// Class descriptor
// ---------------------------------------------------------------------------------------------
namespace
{

class OpenScatterClassDesc : public ClassDesc2
{
public:
    int IsPublic() override { return TRUE; }
    void* Create(BOOL loading) override { return new OpenScatterObject(loading); }
    const MCHAR* ClassName() override { return _T("OpenScatter"); }
    const MCHAR* NonLocalizedClassName() override { return _T("OpenScatter"); }
    SClass_ID SuperClassID() override { return GEOMOBJECT_CLASS_ID; }
    Class_ID ClassID() override { return OPENSCATTER_CLASS_ID; }
    const MCHAR* Category() override { return _T("Open3D"); }
    const MCHAR* InternalName() override { return _T("OpenScatter"); }
    HINSTANCE HInstance() override { return hInstance; }
};

OpenScatterClassDesc g_scatterDesc;

// A surface or a model must not be an OpenScatter object, nor anything that depends on this one:
// either would make the scatter one of its own inputs.
class NodeValidator : public PBValidator
{
public:
    BOOL Validate(PB2Value&) override { return TRUE; }
    BOOL Validate(PB2Value& v, ReferenceMaker* owner, ParamID, int) override
    {
        INode* node = static_cast<INode*>(v.r);
        if (!node)
            return TRUE;
        if (getOpenScatter(node))
            return FALSE;
        if (owner && node->TestForLoop(FOREVER, owner) != REF_SUCCEED)
            return FALSE;
        return TRUE;
    }
};

NodeValidator g_nodeValidator;

// The two tables below are laid out by hand, one entry after another: clang-format leaves them alone.
// clang-format off
ParamBlockDesc2 g_scatterParams(
    kScatterParamBlock, _T("params"), 0, &g_scatterDesc, P_AUTO_CONSTRUCT, 0,

    p_surfaces, _T("surfaces"), TYPE_INODE_TAB, 0, P_VARIABLE_SIZE, 0,
        p_validator, &g_nodeValidator,
        p_end,
    p_models, _T("models"), TYPE_INODE_TAB, 0, P_VARIABLE_SIZE, 0,
        p_validator, &g_nodeValidator,
        p_end,
    p_modelProbabilities, _T("modelProbabilities"), TYPE_FLOAT_TAB, 0, P_VARIABLE_SIZE, 0,
        p_default, 1.0f,
        p_range, 0.0f, 1.0e6f,
        p_end,
    p_density, _T("density"), TYPE_FLOAT, 0, 0,
        p_default, 1.0f,
        p_range, 0.0f, 1.0e9f,
        p_end,
    p_patternSize, _T("patternSize"), TYPE_FLOAT, 0, 0,
        p_default, 0.0f,
        p_range, 0.0f, 1.0e9f,
        p_end,
    p_seed, _T("seed"), TYPE_INT, 0, 0,
        p_default, 1,
        p_range, 0, 0x7fffffff,
        p_end,
    p_maxInstances, _T("maxInstances"), TYPE_INT, 0, 0,
        p_default, 2000000,
        p_range, 0, 0x7fffffff,
        p_end,
    p_alignToNormal, _T("alignToNormal"), TYPE_FLOAT, 0, 0,
        p_default, 0.0f,
        p_range, 0.0f, 1.0f,
        p_end,
    p_rotationMin, _T("rotationMin"), TYPE_POINT3, 0, 0,
        p_default, Point3(0.0f, 0.0f, 0.0f),
        p_end,
    p_rotationMax, _T("rotationMax"), TYPE_POINT3, 0, 0,
        p_default, Point3(0.0f, 0.0f, 360.0f),
        p_end,
    p_scaleMin, _T("scaleMin"), TYPE_POINT3, 0, 0,
        p_default, Point3(1.0f, 1.0f, 1.0f),
        p_end,
    p_scaleMax, _T("scaleMax"), TYPE_POINT3, 0, 0,
        p_default, Point3(1.0f, 1.0f, 1.0f),
        p_end,
    p_uniformScale, _T("uniformScale"), TYPE_BOOL, 0, 0,
        p_default, TRUE,
        p_end,
    p_limitSlope, _T("limitSlope"), TYPE_BOOL, 0, 0,
        p_default, FALSE,
        p_end,
    p_slopeMin, _T("slopeMin"), TYPE_FLOAT, 0, 0,
        p_default, 0.0f,
        p_range, 0.0f, 180.0f,
        p_end,
    p_slopeMax, _T("slopeMax"), TYPE_FLOAT, 0, 0,
        p_default, 90.0f,
        p_range, 0.0f, 180.0f,
        p_end,
    p_limitAltitude, _T("limitAltitude"), TYPE_BOOL, 0, 0,
        p_default, FALSE,
        p_end,
    p_altitudeMin, _T("altitudeMin"), TYPE_FLOAT, 0, 0,
        p_default, 0.0f,
        p_range, -1.0e9f, 1.0e9f,
        p_end,
    p_altitudeMax, _T("altitudeMax"), TYPE_FLOAT, 0, 0,
        p_default, 0.0f,
        p_range, -1.0e9f, 1.0e9f,
        p_end,
    p_renderRoute, _T("renderRoute"), TYPE_INT, 0, 0,
        p_default, kRouteAuto,
        p_range, 0, kRouteCount - 1,
        p_end,
    p_meshMaxFaces, _T("meshMaxFaces"), TYPE_INT, 0, 0,
        p_default, 2000000,
        p_range, 0, 0x7fffffff,
        p_end,
    p_viewportMode, _T("viewportMode"), TYPE_INT, 0, 0,
        p_default, kViewBoxes,
        p_range, 0, kViewModeCount - 1,
        p_end,
    p_viewportMaxInstances, _T("viewportMaxInstances"), TYPE_INT, 0, 0,
        p_default, 0,
        p_range, 0, 0x7fffffff,
        p_end,
    p_viewportMaxTriangles, _T("viewportMaxTriangles"), TYPE_INT, 0, 0,
        p_default, 0,
        p_range, 0, 0x7fffffff,
        p_end,
    p_iconSize, _T("iconSize"), TYPE_FLOAT, 0, 0,
        p_default, 20.0f,
        p_range, 0.0f, 1.0e9f,
        p_end,
    p_material, _T("combinedMaterial"), TYPE_MTL, 0, 0,
        p_end,
    p_threads, _T("threads"), TYPE_INT, 0, 0,
        p_default, 0,
        p_range, 0, 1024,
        p_end,
    p_algorithmVersion, _T("algorithmVersion"), TYPE_INT, 0, 0,
        p_default, static_cast<int>(osc::kAlgorithmVersion),
        p_range, 0, 0x7fffffff,
        p_end,
    p_end);

// The functions published next to the parameters ("$.getInstanceCount()" in MAXScript).
FPInterfaceDesc g_scatterInterface(
    OPENSCATTER_INTERFACE, _T("OpenScatter"), 0, &g_scatterDesc, FP_MIXIN,

    IOpenScatter::kFnUpdate, _T("update"), 0, TYPE_VOID, 0, 0,
    IOpenScatter::kFnGetInstanceCount, _T("getInstanceCount"), 0, TYPE_INT, 0, 0,
    IOpenScatter::kFnGetInstanceHash, _T("getInstanceHash"), 0, TYPE_TSTR_BV, 0, 0,
    IOpenScatter::kFnGetStatus, _T("getStatus"), 0, TYPE_TSTR_BV, 0, 0,
    IOpenScatter::kFnGetStatistics, _T("getStatistics"), 0, TYPE_TSTR_BV, 0, 0,
    IOpenScatter::kFnGetActiveRoute, _T("getActiveRoute"), 0, TYPE_TSTR_BV, 0, 0,
    IOpenScatter::kFnGetRenderReport, _T("getRenderReport"), 0, TYPE_TSTR_BV, 0, 0,
    IOpenScatter::kFnGetViewportReport, _T("getViewportReport"), 0, TYPE_TSTR_BV, 0, 0,
    IOpenScatter::kFnGetCarrier, _T("getCarrier"), 0, TYPE_INODE, 0, 0,
    IOpenScatter::kFnSyncCarrier, _T("syncCarrier"), 0, TYPE_VOID, 0, 0,
    IOpenScatter::kFnGetModelInstanceCount, _T("getModelInstanceCount"), 0, TYPE_INT, 0, 1,
        _T("model"), 0, TYPE_INDEX,
    IOpenScatter::kFnGetInstanceTransform, _T("getInstanceTransform"), 0, TYPE_MATRIX3_BV, 0, 1,
        _T("index"), 0, TYPE_INDEX,
    IOpenScatter::kFnGetInstanceModel, _T("getInstanceModel"), 0, TYPE_INDEX, 0, 1,
        _T("index"), 0, TYPE_INDEX,
    IOpenScatter::kFnGetVersion, _T("getVersion"), 0, TYPE_TSTR_BV, 0, 0,
    p_end);
// clang-format on

// The pattern every scatter object tiles: the one "osc_tilegen" writes with its default arguments.
// Built once per session (about 0.1 s) on first use; it will come from a file in the bundle when
// there is more than one pattern to choose from.
const osc::Pattern& defaultPattern()
{
    static std::once_flag once;
    static osc::Pattern pattern;
    std::call_once(once, [] { pattern = osc::makeProgressiveBlueNoise(16384, 2026); });
    return pattern;
}

// A View for GetRenderMesh calls made outside a render.
class NullView : public View
{
public:
    NullView()
    {
        worldToView.IdentityMatrix();
        affineTM.IdentityMatrix();
        screenW = 640.0f;
        screenH = 480.0f;
        projType = 0;
        fov = 0.785f;
        pixelSize = 1.0f;
        flags = 0;
    }
    Point2 ViewToScreen(Point3 p) override { return Point2(p.x, p.y); }
};

bool finite(const Point3& p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }

// Copies the mesh a renderer would get from the node into "out", in the node's object space.
bool copyRenderMesh(INode* node, TimeValue t, Mesh& out, Interval& valid)
{
    out.setNumVerts(0);
    out.setNumFaces(0);
    const ObjectState os = node->EvalWorldState(t);
    if (!os.obj)
        return false;
    valid &= os.Validity(t);

    if (os.obj->SuperClassID() == GEOMOBJECT_CLASS_ID)
    {
        GeomObject* geom = static_cast<GeomObject*>(os.obj);
        NullView view;
        BOOL needDelete = FALSE;
        Mesh* mesh = geom->GetRenderMesh(t, node, view, needDelete);
        if (mesh)
        {
            if (mesh->getNumFaces() > 0)
                out = *mesh;
            if (needDelete)
                mesh->DeleteThis();
        }
    }
    if (out.getNumFaces() == 0 && os.obj->CanConvertToType(triObjectClassID))
    {
        TriObject* tri = static_cast<TriObject*>(os.obj->ConvertToType(t, triObjectClassID));
        if (tri)
        {
            out = tri->GetMesh();
            if (tri != os.obj)
                tri->DeleteThis();
        }
    }
    return out.getNumFaces() > 0;
}

// Appends the node's mesh, in world space, to the mesh the engine scatters on.
void appendSurface(INode* node, TimeValue t, osc::TriMesh& out, Interval& valid)
{
    const ObjectState os = node->EvalWorldState(t);
    if (!os.obj || !os.obj->CanConvertToType(triObjectClassID))
        return;
    valid &= os.Validity(t);
    TriObject* tri = static_cast<TriObject*>(os.obj->ConvertToType(t, triObjectClassID));
    if (!tri)
        return;
    const Mesh& mesh = tri->GetMesh();
    const Matrix3 tm = node->GetObjectTM(t, &valid);
    const int numVerts = mesh.getNumVerts();
    const int numFaces = mesh.getNumFaces();
    const std::size_t base = out.vertices.size();
    // The engine's indices are 32 bits wide.
    if (numVerts > 0 && numFaces > 0 && base + static_cast<std::size_t>(numVerts) < 0xffffffffull)
    {
        out.vertices.reserve(base + static_cast<std::size_t>(numVerts));
        for (int v = 0; v < numVerts; ++v)
        {
            const Point3 p = mesh.verts[v] * tm;
            out.vertices.push_back({ p.x, p.y, p.z });   // not finite: the engine skips its triangles
        }
        out.indices.reserve(out.indices.size() + static_cast<std::size_t>(numFaces) * 3);
        for (int f = 0; f < numFaces; ++f)
            for (int k = 0; k < 3; ++k)
                out.indices.push_back(static_cast<std::uint32_t>(base + mesh.faces[f].v[k]));
    }
    if (tri != os.obj)
        tri->DeleteThis();
}

const MCHAR* routeName(RenderRoute route)
{
    switch (route)
    {
    case kRouteAuto: return _T("auto");
    case kRouteInstancing: return _T("instancing");
    case kRouteParticleFlow: return _T("particleFlow");
    case kRouteMesh: return _T("mesh");
    default: return _T("none");
    }
}

MSTR widen(const char* text)
{
    std::wstring out;
    for (; text && *text; ++text)
        out.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*text)));
    return MSTR(out.c_str());
}

} // namespace

ClassDesc2* getOpenScatterDesc() { return &g_scatterDesc; }

FPInterfaceDesc* IOpenScatter::GetDesc() { return &g_scatterInterface; }

int materialSlotsOf(Mtl* mtl)
{
    if (mtl && mtl->IsMultiMtl())
    {
        const int n = mtl->NumSubMtls();
        return n > 0 ? n : 1;
    }
    return 1;
}

OpenScatterObject* getOpenScatter(Object* object)
{
    if (!object)
        return nullptr;
    Object* base = object->FindBaseObject();
    if (!base || base->ClassID() != OPENSCATTER_CLASS_ID || base->SuperClassID() != GEOMOBJECT_CLASS_ID)
        return nullptr;
    return static_cast<OpenScatterObject*>(base);
}

OpenScatterObject* getOpenScatter(INode* node)
{
    return node ? getOpenScatter(node->GetObjectRef()) : nullptr;
}

float rankThreshold(const InstanceBuffer& buffer, std::size_t keep)
{
    const std::size_t n = buffer.instances.size();
    if (keep >= n)
        return 2.0f;
    if (keep == 0)
        return -1.0f;
    std::vector<float> ranks(n);
    for (std::size_t i = 0; i < n; ++i)
        ranks[i] = buffer.instances[i].rank;
    std::nth_element(ranks.begin(), ranks.begin() + static_cast<std::ptrdiff_t>(keep), ranks.end());
    return ranks[keep];   // instances with a rank below this one: at most "keep"
}

// ---------------------------------------------------------------------------------------------
// Construction, references, notifications
// ---------------------------------------------------------------------------------------------
OpenScatterObject::OpenScatterObject(BOOL loading)
{
    m_empty.setNumVerts(0);
    m_empty.setNumFaces(0);
    m_instancing = createInstancingAdapter(this);
    m_viewport = createViewportDisplay(this);
    if (!loading)
        g_scatterDesc.MakeAutoParamBlocks(this);
}

OpenScatterObject::~OpenScatterObject()
{
    destroyViewportDisplay(m_viewport);
    destroyInstancingAdapter(m_instancing);
}

void OpenScatterObject::BeginEditParams(IObjParam*, ULONG, Animatable*)
{
    // No rollouts yet: every parameter is set from MAXScript or Python.
}

void OpenScatterObject::EndEditParams(IObjParam*, ULONG, Animatable*) {}

BaseInterface* OpenScatterObject::GetInterface(Interface_ID id)
{
    if (id == OPENSCATTER_INTERFACE)
        return static_cast<IOpenScatter*>(this);
    // The instancing interface exists for a renderer only while it is the active route: a renderer
    // that finds it must not also be given the aggregate mesh, and the other way round.
    if (id == RENDERTIME_INSTANCING_INTERFACE)
        return activeRoute() == kRouteInstancing ? instancingInterface(m_instancing) : nullptr;
    return GeomObject::GetInterface(id);
}

RefResult OpenScatterObject::NotifyRefChanged(const Interval&, RefTargetHandle hTarget, PartID&, RefMessage message, BOOL)
{
    if (hTarget != pblock || !pblock)
        return REF_SUCCEED;

    const ParamID id = pblock->LastNotifyParamID();
    switch (message)
    {
    case REFMSG_CHANGE:
        switch (id)
        {
        case p_models:
            // A model node moved, changed its mesh, or the list itself changed.
            m_modelsDirty = true;
            requestCarrierSync();
            break;
        case p_modelProbabilities:
            m_modelsDirty = true;
            break;
        case p_renderRoute:
            requestCarrierSync();
            break;
        case p_material:
        case p_meshMaxFaces:
        case p_viewportMode:
        case p_viewportMaxInstances:
        case p_viewportMaxTriangles:
        case p_iconSize:
        case p_algorithmVersion:
            // Outputs read these when they run; the instances do not depend on them.
            break;
        default:
            m_scatterDirty = true;
            break;
        }
        break;
    case REFMSG_NODE_MATERIAL_CHANGED:
        if (id == p_models)
        {
            m_modelsDirty = true;
            requestCarrierSync();
        }
        break;
    case REFMSG_NODE_WIRECOLOR_CHANGED:
        if (id == p_models)
            m_modelsDirty = true;
        break;
    case REFMSG_TARGET_DELETED:
        m_modelsDirty = true;
        m_scatterDirty = true;
        requestCarrierSync();
        break;
    default:
        break;
    }
    return REF_SUCCEED;
}

RefTargetHandle OpenScatterObject::Clone(RemapDir& remap)
{
    OpenScatterObject* copy = new OpenScatterObject(TRUE);
    copy->ReplaceReference(0, remap.CloneRef(pblock));
    BaseClone(this, copy, remap);
    // The copy needs its own combined material (the cloned parameter block still points at ours:
    // two objects keeping one material would overwrite each other) and, if Particle Flow is its
    // route, its own carrier. Both are made at the next sync.
    if (copy->pblock)
    {
        HoldSuspend noUndo;
        copy->pblock->SetValue(p_material, 0, static_cast<Mtl*>(nullptr));
    }
    requestCarrierSync();
    return copy;
}

IOResult OpenScatterObject::Load(ILoad* iload)
{
    m_scatterDirty = true;
    m_modelsDirty = true;
    return GeomObject::Load(iload);
}

// ---------------------------------------------------------------------------------------------
// The instance buffer
// ---------------------------------------------------------------------------------------------
void OpenScatterObject::update(TimeValue t)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!pblock)
        return;
    // The flags are cleared before the work: a change that arrives during it asks for another pass.
    if (m_modelsDirty.exchange(false) || !m_modelsValid.InInterval(t))
        refreshModels(t);
    if (m_scatterDirty.exchange(false) || !m_scatterValid.InInterval(t))
        refreshScatter(t);
}

void OpenScatterObject::refreshModels(TimeValue t)
{
    Interval valid = FOREVER;
    const int count = pblock->Count(p_models);
    const int weights = pblock->Count(p_modelProbabilities);
    std::vector<ModelData> models(static_cast<std::size_t>(count));

    int slot = 0;
    for (int i = 0; i < count; ++i)
    {
        ModelData& model = models[static_cast<std::size_t>(i)];
        model.pivotTM.IdentityMatrix();
        model.bounds.Init();
        INode* node = nullptr;
        pblock->GetValue(p_models, t, node, valid, i);
        float weight = 1.0f;
        if (i < weights)
            pblock->GetValue(p_modelProbabilities, t, weight, valid, i);

        // The slots depend only on the node's material, so that syncCombinedMaterial lays the
        // combined material out the same way without evaluating anything.
        model.materialBase = slot;
        Mtl* mtl = node ? node->GetMtl() : nullptr;
        if (mtl && mtl == combinedMaterial())
            mtl = nullptr;
        model.materialSlots = materialSlotsOf(mtl);
        if (node && copyRenderMesh(node, t, model.mesh, valid))
        {
            model.node = node;
            const bool multi = mtl && mtl->IsMultiMtl();

            // Into pivot space: an instance places the model's pivot, not its object origin.
            const Matrix3 objectTM = node->GetObjectTM(t, &valid);
            const Matrix3 nodeTM = node->GetNodeTM(t, &valid);
            model.pivotTM = objectTM * Inverse(nodeTM);
            Mesh& mesh = model.mesh;
            if (!model.pivotTM.IsIdentity())
            {
                for (int v = 0; v < mesh.getNumVerts(); ++v)
                    mesh.verts[v] = mesh.verts[v] * model.pivotTM;
                if (MeshNormalSpec* normals = mesh.GetSpecifiedNormals())
                    normals->Transform(model.pivotTM);
            }
            for (int f = 0; f < mesh.getNumFaces(); ++f)
            {
                const int id = multi ? (static_cast<int>(mesh.faces[f].getMatID()) % model.materialSlots) : 0;
                mesh.faces[f].setMatID(static_cast<MtlID>(model.materialBase + id));
            }
            mesh.InvalidateGeomCache();
            mesh.buildBoundingBox();
            model.bounds = mesh.getBoundingBox();
            const DWORD wire = node->GetWireColor();
            model.wireColor = Color(GetRValue(wire) / 255.0f, GetGValue(wire) / 255.0f, GetBValue(wire) / 255.0f);
            model.weight = (std::isfinite(weight) && weight > 0.0f) ? weight : 0.0f;
        }
        else
        {
            model.mesh.setNumVerts(0);
            model.mesh.setNumFaces(0);
            model.weight = 0.0f;
        }
        slot += model.materialSlots;
    }

    std::vector<float> engineWeights(models.size());
    for (std::size_t i = 0; i < models.size(); ++i)
        engineWeights[i] = models[i].weight;
    if (engineWeights != m_engineWeights)
        m_scatterDirty = true;   // which model each instance gets depends on the weights

    m_buffer.models = std::move(models);
    ++m_buffer.version.models;
    ++m_modelRuns;
    m_modelsValid = valid;
}

void OpenScatterObject::refreshScatter(TimeValue t)
{
    const auto start = std::chrono::steady_clock::now();
    Interval valid = FOREVER;
    InstanceBuffer& buffer = m_buffer;

    // --- the surfaces, as one mesh in world space ---
    osc::TriMesh surface;
    const int surfaces = pblock->Count(p_surfaces);
    for (int i = 0; i < surfaces; ++i)
    {
        INode* node = nullptr;
        pblock->GetValue(p_surfaces, t, node, valid, i);
        if (node)
            appendSurface(node, t, surface, valid);
    }
    buffer.surfaceTriangles = surface.triangleCount();

    // --- the settings ---
    float density = 0.0f;
    float patternSize = 0.0f;
    float alignToNormal = 0.0f;
    float slopeMin = 0.0f, slopeMax = 90.0f, altitudeMin = 0.0f, altitudeMax = 0.0f;
    int seed = 1, maxInstances = 0, threads = 0;
    BOOL uniformScale = TRUE, limitSlope = FALSE, limitAltitude = FALSE;
    Point3 rotationMin(0.0f, 0.0f, 0.0f), rotationMax(0.0f, 0.0f, 360.0f);
    Point3 scaleMin(1.0f, 1.0f, 1.0f), scaleMax(1.0f, 1.0f, 1.0f);
    pblock->GetValue(p_density, t, density, valid);
    pblock->GetValue(p_patternSize, t, patternSize, valid);
    pblock->GetValue(p_seed, t, seed, valid);
    pblock->GetValue(p_maxInstances, t, maxInstances, valid);
    pblock->GetValue(p_alignToNormal, t, alignToNormal, valid);
    pblock->GetValue(p_rotationMin, t, rotationMin, valid);
    pblock->GetValue(p_rotationMax, t, rotationMax, valid);
    pblock->GetValue(p_scaleMin, t, scaleMin, valid);
    pblock->GetValue(p_scaleMax, t, scaleMax, valid);
    pblock->GetValue(p_uniformScale, t, uniformScale, valid);
    pblock->GetValue(p_limitSlope, t, limitSlope, valid);
    pblock->GetValue(p_slopeMin, t, slopeMin, valid);
    pblock->GetValue(p_slopeMax, t, slopeMax, valid);
    pblock->GetValue(p_limitAltitude, t, limitAltitude, valid);
    pblock->GetValue(p_altitudeMin, t, altitudeMin, valid);
    pblock->GetValue(p_altitudeMax, t, altitudeMax, valid);
    pblock->GetValue(p_threads, t, threads, valid);

    const osc::Pattern& pattern = defaultPattern();

    // The density is typed per square metre; the engine works in scene units.
    double metresPerUnit = GetSystemUnitScale(UNITS_METERS);
    if (!(metresPerUnit > 0.0) || !std::isfinite(metresPerUnit))
        metresPerUnit = 1.0;
    const float engineDensity = static_cast<float>(static_cast<double>(density) * metresPerUnit * metresPerUnit);

    // The tile size fixes where every candidate is, so it must not follow the density smoothly:
    // left automatic, it is the largest power of two (in scene units) at which the pattern still
    // holds the density asked. Halving or doubling the density inside one such step moves nothing.
    float tileSize = patternSize;
    if (!(tileSize > 0.0f))
    {
        const float full = osc::tileSizeForDensity(pattern.size(), engineDensity);
        if (full > 0.0f && std::isfinite(full))
        {
            int exponent = 0;
            std::frexp(full, &exponent);            // full = m * 2^exponent, m in [0.5, 1)
            tileSize = std::ldexp(1.0f, exponent - 1);   // the power of two at or below "full"
        }
    }

    osc::ScatterSettings settings;
    settings.tileSize = tileSize;
    settings.density = engineDensity;
    settings.seed = static_cast<std::uint64_t>(seed < 0 ? 0 : seed);
    settings.maxInstances = static_cast<std::uint64_t>(maxInstances < 0 ? 0 : maxInstances);
    settings.limitSlope = limitSlope != FALSE;
    settings.slopeMinDegrees = slopeMin;
    settings.slopeMaxDegrees = slopeMax;
    settings.limitAltitude = limitAltitude != FALSE;
    settings.altitudeMin = altitudeMin;
    settings.altitudeMax = altitudeMax;
    settings.transform.alignToNormal = alignToNormal;
    const float rmin[3] = { rotationMin.x, rotationMin.y, rotationMin.z };
    const float rmax[3] = { rotationMax.x, rotationMax.y, rotationMax.z };
    const float smin[3] = { scaleMin.x, scaleMin.y, scaleMin.z };
    const float smax[3] = { scaleMax.x, scaleMax.y, scaleMax.z };
    for (int axis = 0; axis < 3; ++axis)
    {
        settings.transform.rotationDegrees[axis] = { rmin[axis], rmax[axis] };
        settings.transform.scale[axis] = { smin[axis], smax[axis] };
    }
    settings.transform.uniformScale = uniformScale != FALSE;
    settings.modelWeights.resize(buffer.models.size());
    bool anyModel = false;
    for (std::size_t i = 0; i < buffer.models.size(); ++i)
    {
        settings.modelWeights[i] = buffer.models[i].weight;
        anyModel = anyModel || buffer.models[i].weight > 0.0f;
    }
    m_engineWeights = settings.modelWeights;

    // --- the scatter ---
    buffer.instances.clear();
    buffer.byModel.assign(buffer.models.size(), {});
    buffer.stats = osc::ScatterStats{};
    buffer.status = osc::ScatterStatus::Ok;
    buffer.hash = 0;
    buffer.tileSize = tileSize;
    buffer.engineDensity = engineDensity;
    buffer.worldBounds.Init();

    // Without a surface or without a usable model there is nothing to place.
    if (!surface.indices.empty() && anyModel && engineDensity > 0.0f)
    {
        osc::SurfaceIndex index;
        index.build(std::move(surface));
        if (!index.empty())
        {
            settings.surface = &index;
            osc::ScatterResult result = osc::scatterPlanar(pattern, settings, static_cast<unsigned>(threads < 0 ? 0 : threads));
            buffer.status = result.status;
            buffer.stats = result.stats;
            if (result.status == osc::ScatterStatus::Ok)
            {
                try
                {
                    buffer.instances.reserve(result.count());
                    for (std::vector<osc::Instance>& tile : result.tiles)
                    {
                        buffer.instances.insert(buffer.instances.end(), tile.begin(), tile.end());
                        std::vector<osc::Instance>().swap(tile);   // give the block back as we go
                    }
                }
                catch (const std::bad_alloc&)
                {
                    buffer.instances.clear();
                    buffer.status = osc::ScatterStatus::OutOfMemory;
                }
            }
        }
    }

    // --- what the outputs need on top of the instances ---
    const std::size_t numModels = buffer.models.size();
    std::vector<float> radius(numModels, 0.0f);
    for (std::size_t m = 0; m < numModels; ++m)
    {
        const Box3& b = buffer.models[m].bounds;
        if (!b.IsEmpty())
            radius[m] = std::max(Length(b.Min()), Length(b.Max()));
    }
    try
    {
        std::vector<std::size_t> counts(numModels, 0);
        for (const osc::Instance& inst : buffer.instances)
            if (inst.model < numModels)
                ++counts[inst.model];
        for (std::size_t m = 0; m < numModels; ++m)
            buffer.byModel[m].reserve(counts[m]);
        for (std::size_t i = 0; i < buffer.instances.size(); ++i)
        {
            const osc::Instance& inst = buffer.instances[i];
            if (inst.model >= numModels)
                continue;
            buffer.byModel[inst.model].push_back(static_cast<std::uint32_t>(i));
            const osc::Transform& tm = inst.transform;
            const float scale = std::max(osc::length(tm.x), std::max(osc::length(tm.y), osc::length(tm.z)));
            const float reach = radius[inst.model] * scale;
            const Point3 p(tm.t.x, tm.t.y, tm.t.z);
            if (finite(p) && std::isfinite(reach))
            {
                buffer.worldBounds += p - Point3(reach, reach, reach);
                buffer.worldBounds += p + Point3(reach, reach, reach);
            }
        }
    }
    catch (const std::bad_alloc&)
    {
        buffer.instances.clear();
        buffer.byModel.assign(numModels, {});
        buffer.worldBounds.Init();
        buffer.status = osc::ScatterStatus::OutOfMemory;
    }

    ++buffer.version.scatter;
    ++m_scatterRuns;
    buffer.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    m_scatterValid = valid;
}

RenderRoute OpenScatterObject::activeRoute() const
{
    const int route = pblock ? pblock->GetInt(p_renderRoute, 0) : kRouteNone;
    if (route == kRouteAuto)
        return routeForRenderer(currentRendererName());
    if (route < 0 || route >= kRouteCount)
        return kRouteNone;
    return static_cast<RenderRoute>(route);
}

Mtl* OpenScatterObject::combinedMaterial() const
{
    return pblock ? pblock->GetMtl(p_material, 0) : nullptr;
}

// ---------------------------------------------------------------------------------------------
// Object
// ---------------------------------------------------------------------------------------------
Interval OpenScatterObject::ObjectValidity(TimeValue t)
{
    update(t);
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    const Interval valid = validity();
    return valid.InInterval(t) ? valid : Interval(t, t);
}

void OpenScatterObject::GetWorldBoundBox(TimeValue t, INode* inode, ViewExp*, Box3& box)
{
    update(t);
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    box = m_buffer.worldBounds;
    // The icon sits on the node.
    if (inode && pblock)
    {
        const float half = 0.5f * pblock->GetFloat(p_iconSize, t);
        const Point3 origin = inode->GetObjectTM(t).GetTrans();
        box += origin - Point3(half, half, half);
        box += origin + Point3(half, half, half);
    }
}

void OpenScatterObject::GetLocalBoundBox(TimeValue t, INode* inode, ViewExp* vpt, Box3& box)
{
    GetWorldBoundBox(t, inode, vpt, box);
    if (inode && !box.IsEmpty())
        box = box * Inverse(inode->GetObjectTM(t));
}

void OpenScatterObject::GetDeformBBox(TimeValue t, Box3& box, Matrix3* tm, BOOL)
{
    update(t);
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    box = m_buffer.worldBounds;
    if (tm && !box.IsEmpty())
        box = box * (*tm);
}

int OpenScatterObject::CanConvertToType(Class_ID obtype)
{
    return (obtype == triObjectClassID || obtype == defObjectClassID) ? TRUE : FALSE;
}

// Collapsing the object, or taking a snapshot of it, gives the aggregate mesh. Renderers do not come
// this way: they ask for the instancing interface or for GetRenderMesh.
Object* OpenScatterObject::ConvertToType(TimeValue t, Class_ID obtype)
{
    if (obtype == triObjectClassID || obtype == defObjectClassID)
    {
        update(t);
        TriObject* tri = CreateNewTriObject();
        if (tri)
        {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            Matrix3 identity;
            identity.IdentityMatrix();
            buildAggregate(tri->GetMesh(), identity, pblock ? pblock->GetInt(p_meshMaxFaces, t) : 0);
        }
        return tri;
    }
    return GeomObject::ConvertToType(t, obtype);
}

// ---------------------------------------------------------------------------------------------
// Published functions
// ---------------------------------------------------------------------------------------------
void OpenScatterObject::fnUpdate()
{
    update(GetCOREInterface()->GetTime());
}

int OpenScatterObject::fnGetInstanceCount()
{
    update(GetCOREInterface()->GetTime());
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return static_cast<int>(std::min<std::size_t>(m_buffer.instances.size(), 0x7fffffff));
}

MSTR OpenScatterObject::fnGetInstanceHash()
{
    update(GetCOREInterface()->GetTime());
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_buffer.hash == 0)
    {
        // The engine's own hash, over the buffer as the outputs see it.
        osc::ScatterResult wrapped;
        wrapped.tiles.push_back(m_buffer.instances);
        m_buffer.hash = osc::hashInstances(wrapped);
    }
    MSTR text;
    text.printf(_T("%016llx"), static_cast<unsigned long long>(m_buffer.hash));
    return text;
}

MSTR OpenScatterObject::fnGetStatus()
{
    update(GetCOREInterface()->GetTime());
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_buffer.status != osc::ScatterStatus::Ok)
        return widen(osc::statusMessage(m_buffer.status));
    if (pblock->Count(p_surfaces) == 0 || m_buffer.surfaceTriangles == 0)
        return _T("no surface to scatter on");
    bool anyModel = false;
    for (const ModelData& model : m_buffer.models)
        anyModel = anyModel || model.weight > 0.0f;
    if (!anyModel)
        return _T("no model to scatter");
    return _T("ok");
}

MSTR OpenScatterObject::fnGetStatistics()
{
    update(GetCOREInterface()->GetTime());
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    const osc::ScatterStats& s = m_buffer.stats;
    MSTR text;
    text.printf(_T("instances=%llu candidates=%llu offSurface=%llu bySlope=%llu byAltitude=%llu ")
                _T("patternShare=%.4f densityClamped=%d limited=%d tileSize=%g densityPerUnit2=%g ")
                _T("surfaceTriangles=%llu models=%d seconds=%.4f scatterRuns=%llu modelRuns=%llu"),
                static_cast<unsigned long long>(m_buffer.instances.size()),
                static_cast<unsigned long long>(s.candidates),
                static_cast<unsigned long long>(s.rejectedOffSurface),
                static_cast<unsigned long long>(s.rejectedBySlope),
                static_cast<unsigned long long>(s.rejectedByAltitude),
                static_cast<double>(s.patternShare), s.densityClamped ? 1 : 0, s.limited ? 1 : 0,
                static_cast<double>(m_buffer.tileSize), static_cast<double>(m_buffer.engineDensity),
                static_cast<unsigned long long>(m_buffer.surfaceTriangles),
                static_cast<int>(m_buffer.models.size()), m_buffer.seconds,
                static_cast<unsigned long long>(m_scatterRuns), static_cast<unsigned long long>(m_modelRuns));
    return text;
}

MSTR OpenScatterObject::fnGetActiveRoute()
{
    return routeName(activeRoute());
}

MSTR OpenScatterObject::fnGetRenderReport()
{
    MSTR text;
    text.printf(_T("instancing=%llu particleFlow=%llu mesh=%llu meshFaces=%llu"),
                static_cast<unsigned long long>(m_report.instancingTargets.load()),
                static_cast<unsigned long long>(m_report.particles.load()),
                static_cast<unsigned long long>(m_report.meshInstances.load()),
                static_cast<unsigned long long>(m_report.meshFaces.load()));
    return text;
}

MSTR OpenScatterObject::fnGetViewportReport()
{
    return viewportReport(m_viewport);
}

INode* OpenScatterObject::fnGetCarrier()
{
    return findCarrier(this);
}

void OpenScatterObject::fnSyncCarrier()
{
    syncCarriers(true);
}

int OpenScatterObject::fnGetModelInstanceCount(int model)
{
    update(GetCOREInterface()->GetTime());
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (model < 0 || static_cast<std::size_t>(model) >= m_buffer.byModel.size())
        return 0;
    return static_cast<int>(m_buffer.byModel[static_cast<std::size_t>(model)].size());
}

Matrix3 OpenScatterObject::fnGetInstanceTransform(int index)
{
    update(GetCOREInterface()->GetTime());
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    Matrix3 tm;
    tm.IdentityMatrix();
    if (index >= 0 && static_cast<std::size_t>(index) < m_buffer.instances.size())
        tm = toMatrix3(m_buffer.instances[static_cast<std::size_t>(index)].transform);
    return tm;
}

int OpenScatterObject::fnGetInstanceModel(int index)
{
    update(GetCOREInterface()->GetTime());
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (index < 0 || static_cast<std::size_t>(index) >= m_buffer.instances.size())
        return -1;
    return static_cast<int>(m_buffer.instances[static_cast<std::size_t>(index)].model);
}

MSTR OpenScatterObject::fnGetVersion()
{
    MSTR text;
    text.printf(_T("%hs (algorithm %u)"), OPENSCATTER_VERSION_STRING, static_cast<unsigned>(osc::kAlgorithmVersion));
    return text;
}

} // namespace oscmax
