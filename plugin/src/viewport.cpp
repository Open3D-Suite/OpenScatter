// SPDX-License-Identifier: GPL-3.0-or-later
//
// OpenScatter plugin for 3ds Max - the viewport.
//
// The instances are drawn with the viewport instancing API of 3ds Max
// (MaxSDK::Graphics::ViewportInstancing::InstanceDisplayGeometry): per model, one geometry - its
// bounding box or its mesh - and one transform per instance, sent to the graphics card once. A
// geometry made of triangles only shows in shaded views, so every model also gets a second one made
// of lines for wireframe views.
//
// How much is drawn depends on the display driver. With a GPU driver millions of boxes are
// interactive; with the software driver (Nitrous Software, Direct3D WARP) every triangle is drawn on
// the CPU and there is no instancing to speak of, so the budget is about a hundred times smaller.
// Above the budget the instances are thinned evenly by rank, and meshes fall back to boxes.

#include "scatter_object.h"

#include <Graphics/GeometryEnums.h>
#include <Graphics/IDisplayManager.h>
#include <Graphics/IRenderItemContainer.h>
#include <Graphics/IndexBufferHandle.h>
#include <Graphics/InstanceDisplayGeometry.h>
#include <Graphics/MaterialRequiredStreams.h>
#include <Graphics/UpdateDisplayContext.h>
#include <Graphics/UpdateNodeContext.h>
#include <Graphics/VertexBufferHandle.h>
#include <maxscript/maxscript.h>
#include <mouseman.h>

#include <algorithm>
#include <chrono>

using namespace MaxSDK::Graphics;

namespace oscmax
{

namespace
{

// What a driver carries without the viewport grinding to a halt (measured: docs/ARCHITECTURE.md).
constexpr std::size_t kGpuMaxInstances = 2000000;
constexpr std::size_t kGpuMaxTriangles = 30000000;
constexpr std::size_t kSoftwareMaxInstances = 20000;
constexpr std::size_t kSoftwareMaxTriangles = 250000;

std::mutex g_driverMutex;
int g_driverApi = 0; // 0 = not known yet

// Which display driver this 3ds Max draws its viewports with, as the number its NitrousGraphicsManager
// interface gives: 3 = Direct3D 11 on a graphics card, 4 = Nitrous Software (Direct3D WARP, on the CPU);
// measured in 3ds Max 2027.2. 0 when it cannot be told, as in 3dsmaxbatch, which has no viewports.
//
// The SDK itself does not say: GraphicsWindow::getDriverString() is empty with Nitrous, and
// 3dsMax.ini still names the saved driver when another one was chosen on the command line. The
// interface is a scripted one, so it is asked through MAXScript, once: the driver cannot change
// without restarting 3ds Max. Main thread only (the viewport calls this while it updates the display).
int displayDriverApi()
{
    std::lock_guard<std::mutex> lock(g_driverMutex);
    if (g_driverApi > 0)
        return g_driverApi;
    Interface* ip = GetCOREInterface();
    if (!ip || !ip->GetActiveViewExp().IsAlive())
        return 0;
    FPValue result;
    if (ExecuteMAXScriptScript(_T("try (NitrousGraphicsManager.API()) catch (0)"), MAXScript::ScriptSource::NonEmbedded,
                               TRUE, &result) &&
        static_cast<int>(result.type) == static_cast<int>(TYPE_INT) && result.i > 0)
        g_driverApi = result.i;
    return g_driverApi;
}

constexpr int kDriverSoftware = 4;

void addStream(MaterialRequiredStreams& streams, ViewportInstancing::InstanceDisplayGeometry& geometry,
               MeshChannelCategory category, VertexBufferHandle& buffer, int channelId, int usageIndex)
{
    MaterialRequiredStreamElement element;
    element.SetType(VertexFieldFloat3);
    element.SetChannelCategory(category);
    element.SetStreamIndex(static_cast<BYTE>(streams.GetNumberOfStreams()));
    if (usageIndex >= 0)
        element.SetUsageIndex(static_cast<unsigned short>(usageIndex));
    if (channelId >= 0)
        element.SetChannelID(channelId);
    streams.AddStream(element);
    geometry.AddVertexBuffer(buffer);
}

bool initBuffer(VertexBufferHandle& buffer, std::vector<Point3>& data)
{
    return buffer.Initialize(sizeof(Point3), data.size(), data.data(), BufferUsageStatic, ReadWriteAcess,
                             BufferPoolTypeHardwareBuffer, VertexFieldFloat3);
}

const int kBoxQuads[6][4] = { { 0, 3, 2, 1 }, { 4, 5, 6, 7 }, { 0, 1, 5, 4 },
                              { 1, 2, 6, 5 }, { 2, 3, 7, 6 }, { 3, 0, 4, 7 } };
const int kBoxEdges[12][2] = { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 }, { 4, 5 }, { 5, 6 },
                               { 6, 7 }, { 7, 4 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };

void boxCorners(const Box3& box, Point3 c[8])
{
    const Point3 lo = box.Min();
    const Point3 hi = box.Max();
    c[0] = Point3(lo.x, lo.y, lo.z);
    c[1] = Point3(hi.x, lo.y, lo.z);
    c[2] = Point3(hi.x, hi.y, lo.z);
    c[3] = Point3(lo.x, hi.y, lo.z);
    c[4] = Point3(lo.x, lo.y, hi.z);
    c[5] = Point3(hi.x, lo.y, hi.z);
    c[6] = Point3(hi.x, hi.y, hi.z);
    c[7] = Point3(lo.x, hi.y, hi.z);
}

// Triangles of the model (or of its box), three positions each.
void shadedPositions(const ModelData& model, bool asBox, std::vector<Point3>& positions)
{
    positions.clear();
    if (asBox)
    {
        Point3 c[8];
        boxCorners(model.bounds, c);
        for (const auto& q : kBoxQuads)
        {
            positions.push_back(c[q[0]]);
            positions.push_back(c[q[1]]);
            positions.push_back(c[q[2]]);
            positions.push_back(c[q[0]]);
            positions.push_back(c[q[2]]);
            positions.push_back(c[q[3]]);
        }
        return;
    }
    const Mesh& mesh = model.mesh;
    positions.reserve(static_cast<std::size_t>(mesh.getNumFaces()) * 3);
    for (int f = 0; f < mesh.getNumFaces(); ++f)
        for (int k = 0; k < 3; ++k)
            positions.push_back(mesh.verts[mesh.faces[f].v[k]]);
}

// Lines of the model (its visible edges) or of its box, two positions each.
void wirePositions(const ModelData& model, bool asBox, std::vector<Point3>& positions)
{
    positions.clear();
    if (asBox)
    {
        Point3 c[8];
        boxCorners(model.bounds, c);
        for (const auto& e : kBoxEdges)
        {
            positions.push_back(c[e[0]]);
            positions.push_back(c[e[1]]);
        }
        return;
    }
    const Mesh& mesh = model.mesh;
    positions.reserve(static_cast<std::size_t>(mesh.getNumFaces()) * 4);
    for (int f = 0; f < mesh.getNumFaces(); ++f)
    {
        const Face& face = mesh.faces[f];
        for (int k = 0; k < 3; ++k)
        {
            if (!face.getEdgeVis(k))
                continue;
            const DWORD a = face.v[k];
            const DWORD b = face.v[(k + 1) % 3];
            if (a > b)
                continue; // an edge shared by two faces is drawn once (open edges with a > b are lost: fine for a
                          // preview)
            positions.push_back(mesh.verts[a]);
            positions.push_back(mesh.verts[b]);
        }
    }
    if (positions.empty())
    {
        // Nothing passed the filter (odd topology): every edge of every face.
        for (int f = 0; f < mesh.getNumFaces(); ++f)
            for (int k = 0; k < 3; ++k)
            {
                positions.push_back(mesh.verts[mesh.faces[f].v[k]]);
                positions.push_back(mesh.verts[mesh.faces[f].v[(k + 1) % 3]]);
            }
    }
}

// Builds one instanced geometry. Returns null when the driver refuses (out of GPU memory).
ViewportInstancing::InstanceDisplayGeometry* buildGeometry(std::vector<Point3>& positions, bool lines,
                                                           std::vector<Matrix3>& matrices, AColor color)
{
    const std::size_t numVerts = positions.size();
    const std::size_t perPrimitive = lines ? 2 : 3;
    if (numVerts < perPrimitive || matrices.empty())
        return nullptr;

    std::vector<Point3> normals(numVerts, Point3(0.0f, 0.0f, 1.0f));
    if (!lines)
    {
        for (std::size_t i = 0; i + 2 < numVerts; i += 3)
        {
            Point3 n = CrossProd(positions[i + 1] - positions[i], positions[i + 2] - positions[i]);
            const float len = Length(n);
            n = len > 0.0f ? n / len : Point3(0.0f, 0.0f, 1.0f);
            normals[i] = normals[i + 1] = normals[i + 2] = n;
        }
    }
    std::vector<int> indices(numVerts);
    for (std::size_t i = 0; i < numVerts; ++i)
        indices[i] = static_cast<int>(i);

    VertexBufferHandle positionBuffer, normalBuffer;
    IndexBufferHandle indexBuffer;
    if (!initBuffer(positionBuffer, positions) || !initBuffer(normalBuffer, normals))
        return nullptr;
    if (!indexBuffer.Initialize(IndexTypeInt, numVerts, indices.data()))
        return nullptr;

    auto* geometry = new ViewportInstancing::InstanceDisplayGeometry;
    geometry->AddRef();
    MaterialRequiredStreams streams;
    addStream(streams, *geometry, MeshChannelPosition, positionBuffer, -1, -1);
    addStream(streams, *geometry, MeshChannelVertexNormal, normalBuffer, -1, -1);
    if (!lines)
    {
        // The streams any material can ask of triangles in standard and high quality views:
        // four UV channels, tangents and bitangents (eight streams in all, the most instancing takes).
        std::vector<Point3> uvs(numVerts, Point3(0.0f, 0.0f, 0.0f));
        std::vector<Point3> tangents(numVerts, Point3(1.0f, 0.0f, 0.0f));
        std::vector<Point3> bitangents(numVerts, Point3(0.0f, 1.0f, 0.0f));
        VertexBufferHandle uvBuffer, tangentBuffer, bitangentBuffer;
        if (!initBuffer(uvBuffer, uvs) || !initBuffer(tangentBuffer, tangents) ||
            !initBuffer(bitangentBuffer, bitangents))
        {
            geometry->Release();
            return nullptr;
        }
        for (int channel = 1; channel <= 4; ++channel)
            addStream(streams, *geometry, MeshChannelTexcoord, uvBuffer, channel, channel - 1);
        addStream(streams, *geometry, MeshChannelTangent, tangentBuffer, 1, -1);
        addStream(streams, *geometry, MeshChannelBitangent, bitangentBuffer, 1, -1);
    }
    geometry->SetPrimitiveType(lines ? PrimitiveLineList : PrimitiveTriangleList);
    geometry->SetPrimitiveCount(static_cast<int>(numVerts / perPrimitive));
    geometry->SetIndexBuffer(indexBuffer);
    geometry->SetStreamRequirement(streams);

    ViewportInstancing::InstanceData data;
    data.numInstances = matrices.size();
    data.bTransformationsAreInWorldSpace = true;
    data.pMatrices = matrices.data();
    data.pColors = &color; // one colour, used for every instance
    data.numColors = 1;
    if (!geometry->CreateInstanceData(data))
    {
        geometry->Release();
        return nullptr;
    }
    return geometry;
}

// A small flat mark at the node: a square with a few "blades" standing on it.
void drawIcon(GraphicsWindow* gw, float size)
{
    const float h = 0.5f * size;
    Point3 square[5] = { Point3(-h, -h, 0.0f), Point3(h, -h, 0.0f), Point3(h, h, 0.0f), Point3(-h, h, 0.0f),
                         Point3(0.0f, 0.0f, 0.0f) };
    gw->polyline(4, square, nullptr, nullptr, TRUE, nullptr);
    const float spots[5][2] = {
        { -0.25f, -0.2f }, { 0.22f, -0.28f }, { 0.0f, 0.05f }, { -0.2f, 0.28f }, { 0.27f, 0.22f }
    };
    for (const auto& s : spots)
    {
        Point3 blade[3] = { Point3(s[0] * size, s[1] * size, 0.0f), Point3(s[0] * size, s[1] * size, 0.3f * size),
                            Point3(0.0f, 0.0f, 0.0f) };
        gw->polyline(2, blade, nullptr, nullptr, FALSE, nullptr);
    }
}

class ScatterCreateCallBack : public CreateMouseCallBack
{
  public:
    int proc(ViewExp* vpt, int msg, int point, int, IPoint2 m, Matrix3& mat) override
    {
        if (!vpt || !vpt->IsAlive())
            return CREATE_ABORT;
        if (msg == MOUSE_POINT || msg == MOUSE_MOVE)
        {
            if (point == 0)
                mat.SetTrans(vpt->SnapPoint(m, m, nullptr, SNAP_IN_PLANE));
            if (msg == MOUSE_POINT)
                return CREATE_STOP; // one click places the object; everything else is in its parameters
        }
        else if (msg == MOUSE_ABORT)
        {
            return CREATE_ABORT;
        }
        return CREATE_CONTINUE;
    }
};

ScatterCreateCallBack g_createCallBack;

} // namespace

ViewportBudget viewportBudget(int maxInstancesParam, int maxTrianglesParam)
{
    ViewportBudget budget;
    const int api = displayDriverApi();
    budget.driver =
        api == kDriverSoftware ? L"software" : (api == 3 ? L"Direct3D 11" : (api > 0 ? L"hardware" : L"unknown"));
    // An unknown driver gets the small budget: too few boxes is a nuisance, a frozen viewport is worse.
    budget.software = api == kDriverSoftware || api <= 0;
    budget.maxInstances = budget.software ? kSoftwareMaxInstances : kGpuMaxInstances;
    budget.maxTriangles = budget.software ? kSoftwareMaxTriangles : kGpuMaxTriangles;
    if (maxInstancesParam > 0)
        budget.maxInstances = static_cast<std::size_t>(maxInstancesParam);
    if (maxTrianglesParam > 0)
        budget.maxTriangles = static_cast<std::size_t>(maxTrianglesParam);
    return budget;
}

class ViewportDisplay
{
  public:
    explicit ViewportDisplay(OpenScatterObject* owner) : m_owner(owner)
    {
    }
    ~ViewportDisplay()
    {
        release();
    }

    bool updateItems(const UpdateDisplayContext& context, UpdateNodeContext& nodeContext,
                     IRenderItemContainer& container)
    {
        IParamBlock2* params = m_owner->params();
        if (!params)
            return true;
        const TimeValue t = context.GetDisplayTime();
        const int mode = params->GetInt(p_viewportMode, t);
        container.ClearAllRenderItems();
        if (mode == kViewNone)
        {
            release();
            m_built = false;
            m_drawnMode = kViewNone;
            m_drawnInstances = m_drawnTriangles = 0;
            return true;
        }

        m_owner->update(t);
        std::lock_guard<std::recursive_mutex> lock(m_owner->mutex());
        const InstanceBuffer& buffer = m_owner->buffer();
        const ViewportBudget budget =
            viewportBudget(params->GetInt(p_viewportMaxInstances, t), params->GetInt(p_viewportMaxTriangles, t));
        if (!m_built || m_version != buffer.version || m_mode != mode || m_maxInstances != budget.maxInstances ||
            m_maxTriangles != budget.maxTriangles)
        {
            rebuild(buffer, mode, budget);
            m_built = true;
            m_version = buffer.version;
            m_mode = mode;
            m_maxInstances = budget.maxInstances;
            m_maxTriangles = budget.maxTriangles;
        }
        for (const ModelGeometry& g : m_geometries)
        {
            if (g.shaded)
                g.shaded->GenerateInstances(false, context, nodeContext, container);
            if (g.wire)
                g.wire->GenerateInstances(true, context, nodeContext, container);
        }
        return true;
    }

    MSTR report() const
    {
        const ViewportBudget budget = viewportBudget(0, 0);
        MSTR text;
        text.printf(
            _T("driver=%s software=%d budgetInstances=%llu budgetTriangles=%llu drawn=%s instances=%llu of %llu ")
            _T("triangles=%llu buildSeconds=%.4f refused=%d"),
            budget.driver.c_str(), budget.software ? 1 : 0, static_cast<unsigned long long>(m_maxInstances),
            static_cast<unsigned long long>(m_maxTriangles),
            m_drawnMode == kViewMeshes ? _T("meshes") : (m_drawnMode == kViewBoxes ? _T("boxes") : _T("none")),
            static_cast<unsigned long long>(m_drawnInstances), static_cast<unsigned long long>(m_totalInstances),
            static_cast<unsigned long long>(m_drawnTriangles), m_buildSeconds, m_refused ? 1 : 0);
        return text;
    }

  private:
    struct ModelGeometry
    {
        ViewportInstancing::InstanceDisplayGeometry* shaded = nullptr;
        ViewportInstancing::InstanceDisplayGeometry* wire = nullptr;
    };

    void release()
    {
        for (ModelGeometry& g : m_geometries)
        {
            if (g.shaded)
                g.shaded->Release();
            if (g.wire)
                g.wire->Release();
        }
        m_geometries.clear();
    }

    void rebuild(const InstanceBuffer& buffer, int mode, const ViewportBudget& budget)
    {
        const auto start = std::chrono::steady_clock::now();
        release();
        m_refused = false;
        m_totalInstances = buffer.instances.size();
        m_drawnInstances = 0;
        m_drawnTriangles = 0;
        m_drawnMode = kViewNone;
        const std::size_t numModels = buffer.models.size();
        if (numModels == 0 || buffer.instances.empty())
            return;

        // How many instances of each model fall under a rank threshold, and what they cost in triangles.
        const auto count = [&](float threshold, bool boxes, std::size_t& instances, std::size_t& triangles) {
            instances = triangles = 0;
            for (const osc::Instance& inst : buffer.instances)
            {
                if (inst.model >= numModels || !(inst.rank < threshold))
                    continue;
                ++instances;
                triangles += boxes ? 12 : static_cast<std::size_t>(buffer.models[inst.model].mesh.getNumFaces());
            }
        };

        float threshold = rankThreshold(buffer, budget.maxInstances);
        bool boxes = mode != kViewMeshes;
        std::size_t instances = 0, triangles = 0;
        count(threshold, boxes, instances, triangles);
        if (!boxes && triangles > budget.maxTriangles)
        {
            // The meshes do not fit: boxes, which every budget can carry.
            boxes = true;
            count(threshold, boxes, instances, triangles);
        }
        for (int pass = 0; pass < 8 && triangles > budget.maxTriangles && instances > 0; ++pass)
        {
            const double ratio = static_cast<double>(budget.maxTriangles) / static_cast<double>(triangles);
            threshold = rankThreshold(buffer, static_cast<std::size_t>(static_cast<double>(instances) * ratio * 0.98));
            count(threshold, boxes, instances, triangles);
        }
        if (instances == 0 || triangles > budget.maxTriangles)
            return;

        m_geometries.resize(numModels);
        std::vector<Matrix3> matrices;
        std::vector<Point3> positions;
        for (std::size_t m = 0; m < numModels; ++m)
        {
            const ModelData& model = buffer.models[m];
            if (model.mesh.getNumFaces() == 0 || m >= buffer.byModel.size())
                continue;
            matrices.clear();
            for (const std::uint32_t k : buffer.byModel[m])
            {
                const osc::Instance& inst = buffer.instances[k];
                if (inst.rank < threshold)
                    matrices.push_back(toMatrix3(inst.transform));
            }
            if (matrices.empty())
                continue;
            const AColor color(model.wireColor.r, model.wireColor.g, model.wireColor.b, 1.0f);
            shadedPositions(model, boxes, positions);
            m_geometries[m].shaded = buildGeometry(positions, false, matrices, color);
            wirePositions(model, boxes, positions);
            m_geometries[m].wire = buildGeometry(positions, true, matrices, color);
            if (!m_geometries[m].shaded || !m_geometries[m].wire)
                m_refused = true;
        }
        m_drawnMode = boxes ? kViewBoxes : kViewMeshes;
        m_drawnInstances = instances;
        m_drawnTriangles = triangles;
        m_buildSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    }

    OpenScatterObject* m_owner;
    std::vector<ModelGeometry> m_geometries;
    bool m_built = false;
    BufferVersion m_version;
    int m_mode = -1;
    std::size_t m_maxInstances = 0;
    std::size_t m_maxTriangles = 0;

    // what was drawn last, for the report
    int m_drawnMode = kViewNone;
    std::size_t m_drawnInstances = 0;
    std::size_t m_totalInstances = 0;
    std::size_t m_drawnTriangles = 0;
    double m_buildSeconds = 0.0;
    bool m_refused = false;
};

ViewportDisplay* createViewportDisplay(OpenScatterObject* owner)
{
    return new ViewportDisplay(owner);
}

void destroyViewportDisplay(ViewportDisplay* display)
{
    delete display;
}

bool viewportUpdateItems(ViewportDisplay* display, const UpdateDisplayContext& context, UpdateNodeContext& nodeContext,
                         IRenderItemContainer& container)
{
    return display ? display->updateItems(context, nodeContext, container) : true;
}

MSTR viewportReport(ViewportDisplay* display)
{
    return display ? display->report() : MSTR();
}

// ---------------------------------------------------------------------------------------------
// The object's display entry points
// ---------------------------------------------------------------------------------------------
CreateMouseCallBack* OpenScatterObject::GetCreateMouseCallBack()
{
    return &g_createCallBack;
}

// The instances go through render items; the icon is a handful of lines drawn the classic way.
unsigned long OpenScatterObject::GetObjectDisplayRequirement() const
{
    return ObjectDisplayRequireLegacyDisplayMode;
}

bool OpenScatterObject::PrepareDisplay(const UpdateDisplayContext& context)
{
    update(context.GetDisplayTime());
    return true;
}

bool OpenScatterObject::UpdatePerNodeItems(const UpdateDisplayContext& context, UpdateNodeContext& nodeContext,
                                           IRenderItemContainer& container)
{
    return viewportUpdateItems(m_viewport, context, nodeContext, container);
}

int OpenScatterObject::Display(TimeValue t, INode* inode, ViewExp* vpt, int)
{
    if (!vpt || !vpt->IsAlive() || !inode || !pblock)
        return 0;
    const float size = pblock->GetFloat(p_iconSize, t);
    if (!(size > 0.0f))
        return 0;
    GraphicsWindow* gw = vpt->getGW();
    if (!gw)
        return 0;
    const DWORD limits = gw->getRndLimits();
    gw->setRndLimits(GW_WIREFRAME | GW_EDGES_ONLY | (limits & GW_Z_BUFFER));
    gw->setTransform(inode->GetObjectTM(t));
    if (inode->Selected())
        gw->setColor(LINE_COLOR, GetSelColor());
    else if (inode->IsFrozen())
        gw->setColor(LINE_COLOR, GetFreezeColor());
    else
    {
        const DWORD wire = inode->GetWireColor();
        gw->setColor(LINE_COLOR, GetRValue(wire) / 255.0f, GetGValue(wire) / 255.0f, GetBValue(wire) / 255.0f);
    }
    drawIcon(gw, size);
    gw->setRndLimits(limits);
    return 0;
}

int OpenScatterObject::HitTest(TimeValue t, INode* inode, int type, int crossing, int, IPoint2* p, ViewExp* vpt)
{
    if (!vpt || !vpt->IsAlive() || !inode || !pblock)
        return 0;
    GraphicsWindow* gw = vpt->getGW();
    if (!gw)
        return 0;
    HitRegion region;
    MakeHitRegion(region, type, crossing, 4, p);
    const DWORD limits = gw->getRndLimits();
    gw->setRndLimits((limits | GW_PICK) & ~GW_ILLUM);
    gw->setHitRegion(&region);
    gw->clearHitCode();

    // The icon, always.
    const float size = pblock->GetFloat(p_iconSize, t);
    if (size > 0.0f)
    {
        gw->setTransform(inode->GetObjectTM(t));
        drawIcon(gw, size);
    }
    int hit = gw->checkHitCode();

    // The instances themselves are hit-tested by the graphics card when it can; otherwise the box
    // around all of them stands in.
    if (!hit && !IsHardwareHitTesting(vpt))
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (!m_buffer.worldBounds.IsEmpty() && pblock->GetInt(p_viewportMode, t) != kViewNone)
        {
            Matrix3 identity;
            identity.IdentityMatrix();
            gw->setTransform(identity);
            Point3 c[8];
            boxCorners(m_buffer.worldBounds, c);
            for (const auto& e : kBoxEdges)
            {
                Point3 line[3] = { c[e[0]], c[e[1]], Point3(0.0f, 0.0f, 0.0f) };
                gw->polyline(2, line, nullptr, nullptr, FALSE, nullptr);
            }
            hit = gw->checkHitCode();
        }
    }
    gw->setRndLimits(limits);
    return hit;
}

} // namespace oscmax
