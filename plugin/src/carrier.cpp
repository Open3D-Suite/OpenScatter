// SPDX-License-Identifier: GPL-3.0-or-later
//
// OpenScatter plugin for 3ds Max - the carrier: the Particle Flow source that takes the instances
// into Corona, and the combined material the carrier and the aggregate mesh render with.
//
// How the carrier is created, owned and kept out of the way:
//   - It is created by the plugin, never by the user, the first time Particle Flow becomes the
//     active route of a scatter object (the route is set to it, or it is automatic and the current
//     renderer is Corona). That is checked when the route or the renderer changes and again when a
//     render starts.
//   - It is four scene nodes: a PF Source, its one event, our operator in that event and a Render
//     operator. All four carry the user property "OpenScatterCarrier" with the same value, so they
//     can be found and removed together. The operator references the scatter node; the scatter
//     object references nothing of the carrier, so there is no reference loop and deleting either
//     side never deletes the other.
//   - The source shows nothing in the viewport (no logo, no emitter icon, viewport quantity 0) and
//     is frozen so that it cannot be picked there. It must stay unhidden and renderable, or the
//     renderer would skip it. It does show in Particle View, like every Particle Flow source.
//   - It holds particles only while a render uses the Particle Flow route (pflow_operator.cpp).
//   - When its scatter object is deleted, the carrier is removed at the next sync. It is saved with
//     the scene and found again on load.
//
// The Particle Flow graph is built by a short MAXScript, the documented way to assemble one
// (particleFlow.BeginEdit / EndEdit); everything else here is C++.

#include "scatter_object.h"

#include <ParticleFlow/PFMessages.h>
#include <Util/MainThreadTaskManager.h>
#include <maxscript/maxscript.h>
#include <notify.h>
#include <stdmat.h>

#include <algorithm>
#include <cwctype>
#include <map>
#include <set>

namespace oscmax
{

// pflow_operator.cpp
OpenScatterObject* scatterOfOperator(Object* object);
void refreshOperator(Object* object);

namespace
{

const MCHAR* const kCarrierProperty = _T("OpenScatterCarrier");

std::mutex g_rendererMutex;
std::wstring g_rendererName;
bool g_rendererKnown = false;

std::atomic<bool> g_syncPending{ false };
std::atomic<bool> g_shutDown{ false };
bool g_inSync = false;
bool g_registered = false;

void refreshRendererName()
{
    std::wstring name;
    if (Interface* ip = GetCOREInterface())
    {
        if (Renderer* renderer = ip->GetCurrentRenderer(false))
        {
            MSTR className;
            static_cast<Animatable*>(renderer)->GetClassName(className, false);
            name = className.data() ? className.data() : L"";
        }
    }
    std::transform(name.begin(), name.end(), name.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    std::lock_guard<std::mutex> lock(g_rendererMutex);
    g_rendererName = name;
    g_rendererKnown = true;
}

void collectNodes(INode* node, std::vector<INode*>& out)
{
    if (!node)
        return;
    for (int i = 0; i < node->NumberOfChildren(); ++i)
    {
        INode* child = node->GetChildNode(i);
        if (!child)
            continue;
        out.push_back(child);
        collectNodes(child, out);
    }
}

std::vector<INode*> sceneNodes()
{
    std::vector<INode*> nodes;
    if (Interface* ip = GetCOREInterface())
        collectNodes(ip->GetRootNode(), nodes);
    return nodes;
}

bool isOperatorNode(INode* node)
{
    Object* object = node ? node->GetObjectRef() : nullptr;
    return object && object->ClassID() == OPENSCATTER_OPERATOR_CLASS_ID;
}

MSTR carrierId(INode* node)
{
    MSTR value;
    if (node && node->GetUserPropString(kCarrierProperty, value))
        return value;
    return MSTR();
}

MSTR handleText(INode* node)
{
    MSTR text;
    text.printf(_T("%u"), static_cast<unsigned>(node->GetHandle()));
    return text;
}

// What the scene holds of one carrier: the nodes that carry its tag.
struct CarrierNodes
{
    INode* source = nullptr;                // the PF Source the tag names (its handle is the tag)
    INode* op = nullptr;                    // our operator
    OpenScatterObject* scatter = nullptr;   // what the operator carries; null when that object is gone
    int nodes = 0;

    // Usable as it is: the source, an operator that still has its scatter object, and the event and
    // the Render operator next to them. Anything less - the user deleted a part of it, or its
    // scatter - is a leftover: it emits nothing, a new carrier replaces it and a sync removes it.
    bool whole() const { return nodes >= 4 && source && op && scatter; }
};

std::map<std::wstring, CarrierNodes> findCarriers(const std::vector<INode*>& nodes)
{
    std::map<std::wstring, CarrierNodes> carriers;
    for (INode* node : nodes)
    {
        const MSTR id = carrierId(node);
        if (id.isNull())
            continue;
        CarrierNodes& carrier = carriers[id.data()];
        ++carrier.nodes;
        if (handleText(node) == id)
            carrier.source = node;
        if (isOperatorNode(node) && (!carrier.op || !carrier.scatter))
        {
            carrier.op = node;
            carrier.scatter = scatterOfOperator(node->GetObjectRef());
        }
    }
    return carriers;
}

// Builds the carrier of one scatter node. Returns false when the script failed.
bool createCarrier(INode* scatterNode)
{
    MSTR script;
    script.printf(
        _T("(\n")
        _T("	local sc = maxOps.getNodeByHandle %u\n")
        _T("	if sc != undefined do with undo off\n")
        _T("	(\n")
        _T("		local pf = PF_Source name:(\"OpenScatter carrier - \" + sc.name)\n")
        _T("		pf.Enable_Particles = true\n")
        _T("		pf.Quantity_Viewport = 0.0\n")
        _T("		pf.Quantity_Render = 100.0\n")
        _T("		pf.Particle_Amount_Limit = 10000000\n")
        _T("		pf.Show_Logo = false\n")
        _T("		pf.Show_Emitter = false\n")
        _T("		pf.Integration_for_Render = 0\n")
        _T("		particleFlow.BeginEdit()\n")
        _T("		local op = OpenScatterOperator name:(\"OpenScatter instances - \" + sc.name)\n")
        _T("		local rn = RenderParticles name:(\"OpenScatter render - \" + sc.name)\n")
        _T("		local ev = Event name:(\"OpenScatter event - \" + sc.name)\n")
        _T("		particleFlow.EndEdit()\n")
        _T("		ev.AppendAction op\n")
        _T("		pf.AppendAction rn\n")
        _T("		pf.AppendInitialActionList ev\n")
        _T("		op.scatter = sc\n")
        _T("		local id = pf.inode.handle as string\n")
        _T("		for n in #(pf, op, rn, ev) do setUserProp n \"%s\" id\n")
        _T("		sc.layer.addNode pf\n")
        _T("		pf.renderable = true\n")
        _T("		pf.isFrozen = true\n")
        _T("		pf.showFrozenInGray = false\n")
        _T("	)\n")
        _T("	OK\n")
        _T(")\n"),
        static_cast<unsigned>(scatterNode->GetHandle()), kCarrierProperty);
    return ExecuteMAXScriptScript(script.data(), MAXScript::ScriptSource::NonEmbedded, TRUE) != FALSE;
}

class SyncTask : public MainThreadTask
{
public:
    void Execute() override
    {
        g_syncPending = false;
        syncCarriers(true);
    }
};

void resetReports()
{
    for (INode* node : sceneNodes())
        if (OpenScatterObject* scatter = getOpenScatter(node))
            scatter->report().reset();
}

void onNotify(void*, NotifyInfo* info)
{
    if (!info || g_shutDown)
        return;
    switch (info->intcode)
    {
    case NOTIFY_SYSTEM_SHUTDOWN:
        g_shutDown = true;
        break;
    case NOTIFY_POST_RENDERER_CHANGE:
        refreshRendererName();
        syncCarriers(false);
        break;
    case NOTIFY_PRE_RENDER:
        // The last moment the scene may still change: make sure the route this render needs exists.
        refreshRendererName();
        syncCarriers(false);
        resetReports();
        break;
    case NOTIFY_FILE_POST_OPEN:
    case NOTIFY_FILE_POST_MERGE_PROCESS_FINALIZED:
    case NOTIFY_SYSTEM_POST_RESET:
    case NOTIFY_SYSTEM_POST_NEW:
        refreshRendererName();
        requestCarrierSync();
        break;
    default:
        break;
    }
}

const int kNotifications[] = { NOTIFY_SYSTEM_SHUTDOWN,  NOTIFY_POST_RENDERER_CHANGE, NOTIFY_PRE_RENDER,     NOTIFY_FILE_POST_OPEN,
                               NOTIFY_FILE_POST_MERGE_PROCESS_FINALIZED,  NOTIFY_SYSTEM_POST_RESET,    NOTIFY_SYSTEM_POST_NEW };

} // namespace

void registerCarrierNotifications()
{
    if (g_registered)
        return;
    g_registered = true;
    for (const int code : kNotifications)
        RegisterNotification(onNotify, nullptr, code);
}

void unregisterCarrierNotifications()
{
    if (!g_registered)
        return;
    g_registered = false;
    for (const int code : kNotifications)
        UnRegisterNotification(onNotify, nullptr, code);
}

std::wstring currentRendererName()
{
    {
        std::lock_guard<std::mutex> lock(g_rendererMutex);
        if (g_rendererKnown)
            return g_rendererName;
    }
    refreshRendererName();
    std::lock_guard<std::mutex> lock(g_rendererMutex);
    return g_rendererName;
}

RenderRoute routeForRenderer(const std::wstring& name)
{
    const auto has = [&name](const wchar_t* word) { return name.find(word) != std::wstring::npos; };
    // Corona does not read the instancing interface; it instances Particle Flow particles.
    if (has(L"corona"))
        return kRouteParticleFlow;
    // The renderers known to read the instancing interface.
    if (has(L"arnold") || has(L"v-ray") || has(L"vray") || has(L"v_ray") || has(L"redshift") || has(L"octane"))
        return kRouteInstancing;
    // Scanline, ART and anything unknown: the aggregate mesh.
    return kRouteMesh;
}

void requestCarrierSync()
{
    if (g_shutDown || g_syncPending.exchange(true))
        return;
    if (IMainThreadTaskManager* tasks = IMainThreadTaskManager::GetInstance())
        tasks->PostTask(new SyncTask());
    else
        g_syncPending = false;
}

bool carrierOwns(INode* sourceNode, INode* partNode)
{
    if (!sourceNode || !partNode)
        return false;
    const MSTR id = carrierId(partNode);
    return !id.isNull() && id == handleText(sourceNode);
}

INode* findCarrier(OpenScatterObject* object)
{
    if (!object)
        return nullptr;
    for (const auto& entry : findCarriers(sceneNodes()))
        if (entry.second.whole() && entry.second.scatter == object)
            return entry.second.source;
    return nullptr;
}

bool OpenScatterObject::syncCombinedMaterial()
{
    if (!pblock)
        return false;
    const TimeValue t = GetCOREInterface()->GetTime();

    // One slot per model, or one per sub-material when the model's own material is a Multi/Sub-
    // Object. refreshModels gives the faces of each model the same slots.
    Mtl* current = pblock->GetMtl(p_material, t);
    std::vector<Mtl*> slots;
    const int count = pblock->Count(p_models);
    for (int i = 0; i < count; ++i)
    {
        INode* node = pblock->GetINode(p_models, t, i);
        Mtl* mtl = node ? node->GetMtl() : nullptr;
        if (mtl == current)
            mtl = nullptr;   // a model wearing the combined material itself would be a loop
        const int n = materialSlotsOf(mtl);
        if (mtl && mtl->IsMultiMtl())
        {
            for (int k = 0; k < n; ++k)
                slots.push_back(k < mtl->NumSubMtls() ? mtl->GetSubMtl(k) : nullptr);
        }
        else
        {
            slots.push_back(mtl);
        }
    }
    if (slots.empty())
        return false;

    bool changed = false;
    HoldSuspend noUndo;
    MultiMtl* multi = (current && current->ClassID() == Class_ID(MULTI_CLASS_ID, 0)) ? static_cast<MultiMtl*>(current) : nullptr;
    if (!multi)
    {
        multi = NewDefaultMultiMtl();
        if (!multi)
            return false;
        multi->SetName(_T("OpenScatter models"));
        pblock->SetValue(p_material, t, static_cast<Mtl*>(multi));
        changed = true;
    }
    const int wanted = static_cast<int>(slots.size());
    if (multi->NumSubMtls() != wanted)
    {
        multi->SetNumSubMtls(wanted);
        changed = true;
    }
    for (int i = 0; i < wanted; ++i)
    {
        if (multi->GetSubMtl(i) != slots[static_cast<std::size_t>(i)])
        {
            multi->SetSubMtl(i, slots[static_cast<std::size_t>(i)]);
            changed = true;
        }
    }
    return changed;
}

void syncCarriers(bool allowDelete)
{
    if (g_inSync || g_shutDown)
        return;
    Interface* ip = GetCOREInterface();
    if (!ip)
        return;
    g_inSync = true;

    const std::vector<INode*> nodes = sceneNodes();

    // --- every scatter object: its material, and its carrier if this is its route ---
    const std::map<std::wstring, CarrierNodes> carriers = findCarriers(nodes);
    std::set<OpenScatterObject*> seen;
    std::set<OpenScatterObject*> carried;
    std::vector<INode*> operators;
    for (const auto& entry : carriers)
    {
        if (!entry.second.whole())
            continue;
        operators.push_back(entry.second.op);
        carried.insert(entry.second.scatter);
    }
    bool created = false;
    for (INode* node : nodes)
    {
        OpenScatterObject* scatter = getOpenScatter(node);
        if (!scatter)
            continue;
        const bool first = seen.insert(scatter).second;
        if (first)
        {
            const bool materialChanged = scatter->syncCombinedMaterial();
            if (scatter->activeRoute() == kRouteParticleFlow && carried.find(scatter) == carried.end())
            {
                if (createCarrier(node))
                {
                    carried.insert(scatter);
                    created = true;
                }
            }
            else if (materialChanged)
            {
                // The event takes its material from the operator: tell it to ask again.
                for (INode* op : operators)
                    if (scatterOfOperator(op->GetObjectRef()) == scatter)
                        op->GetObjectRef()->NotifyDependents(FOREVER, PART_MTL, kPFMSG_UpdateMaterial, NOTIFY_ALL, TRUE);
            }
        }
    }

    // The aggregate mesh needs the combined material on the scatter's own node. It is given to a
    // node that has no material, and to one still wearing another scatter's combined material (a
    // copy of that scatter, made before it had its own) - never over a material the user chose.
    std::set<Mtl*> combinedMaterials;
    for (OpenScatterObject* scatter : seen)
        if (Mtl* combined = scatter->combinedMaterial())
            combinedMaterials.insert(combined);
    for (INode* node : nodes)
    {
        OpenScatterObject* scatter = getOpenScatter(node);
        Mtl* own = scatter ? scatter->combinedMaterial() : nullptr;
        if (!own)
            continue;
        Mtl* current = node->GetMtl();
        if (current != own && (!current || combinedMaterials.count(current)))
        {
            HoldSuspend noUndo;
            node->SetMtl(own);
        }
    }
    if (created)
    {
        // The new events take their material from our operator.
        for (INode* node : sceneNodes())
            if (isOperatorNode(node))
                node->GetObjectRef()->NotifyDependents(FOREVER, PART_MTL, kPFMSG_UpdateMaterial, NOTIFY_ALL, TRUE);
    }

    // --- carriers that hold the wrong thing for the next render ---
    // Particle Flow keeps its particles between renders; when the route changed without any of its
    // actions changing (another renderer was chosen), what it kept must go.
    for (INode* op : operators)
        refreshOperator(op->GetObjectRef());

    // --- what is left of carriers that are no longer whole ---
    // Their scatter object was deleted, or the user deleted a part of them. They emit nothing.
    if (allowDelete)
    {
        // By handle: deleting one node of a flow may take others with it.
        std::vector<ULONG> doomed;
        for (INode* node : nodes)
        {
            const MSTR id = carrierId(node);
            if (id.isNull())
                continue;
            const auto found = carriers.find(id.data());
            if (found == carriers.end() || !found->second.whole())
                doomed.push_back(node->GetHandle());
        }
        HoldSuspend noUndo;
        for (const ULONG handle : doomed)
            if (INode* node = ip->GetINodeByHandle(handle))
                ip->DeleteNode(node, FALSE, FALSE);
    }

    g_inSync = false;
}

} // namespace oscmax
