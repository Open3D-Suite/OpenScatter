// SPDX-License-Identifier: GPL-3.0-or-later
//
// OpenScatter plugin for 3ds Max - the Particle Flow operator that carries the instances into Corona.
//
// Corona does not read the render-time instancing interface, but it renders the particles of a
// Particle Flow source as real instances when they share their shape. This operator is the only
// action of the carrier's event (carrier.cpp): it gives birth to one particle per instance and
// writes its position, orientation, scale and shape from the scatter object's instance buffer.
// There is no simulation: every step makes the particles match the buffer again.
//
// It emits only while Particle Flow is in its render state and Particle Flow is the scatter's
// active route. In the viewport, and with every other route, the carrier holds no particles, so it
// costs nothing while working and nothing is rendered twice.

#include "scatter_object.h"

#include <decomp.h>
#include <IParticleObjectExt.h>
#include <ParticleFlow/IChannelContainer.h>
#include <ParticleFlow/IPFAction.h>
#include <ParticleFlow/IPFSystem.h>
#include <ParticleFlow/IParticleChannels.h>
#include <ParticleFlow/PFClassIDs.h>
#include <ParticleFlow/PFSimpleOperator.h>
#include <ParticleFlow/ParticleChannelMask.h>

#include <algorithm>
#include <climits>
#include <cmath>

namespace oscmax
{

enum { kOperatorParamBlock = 0 };
enum { kOpScatter = 0 };

namespace
{

// How a rotation matrix becomes the quaternion Particle Flow expects in its orientation channel.
// The known-good conversion is the one decomp_affine gives; building a Quat straight from the
// matrix is far cheaper, so its convention is checked against decomp_affine once.
enum class QuatMode { Unknown, Direct, Inverse, Decompose };

QuatMode findQuatMode()
{
    Matrix3 probe;
    probe.IdentityMatrix();
    probe.RotateZ(0.5f);
    probe.RotateX(0.3f);
    AffineParts parts;
    decomp_affine(probe, &parts);
    const Quat direct(probe);
    const auto same = [](const Quat& a, const Quat& b) {
        const float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
        return std::fabs(std::fabs(d) - 1.0f) < 1.0e-4f;
    };
    if (same(direct, parts.q))
        return QuatMode::Direct;
    if (same(direct.Inverse(), parts.q))
        return QuatMode::Inverse;
    return QuatMode::Decompose;
}

// Splits an instance transform (orthogonal axes, no shear) into what a particle carries.
void decompose(const Matrix3& tm, QuatMode mode, Point3& position, Quat& orientation, Point3& scale)
{
    position = tm.GetTrans();
    if (mode == QuatMode::Decompose)
    {
        AffineParts parts;
        decomp_affine(tm, &parts);
        orientation = parts.q;
        scale = parts.k * parts.f;
        return;
    }
    Point3 x = tm.GetRow(0);
    Point3 y = tm.GetRow(1);
    Point3 z = tm.GetRow(2);
    scale = Point3(Length(x), Length(y), Length(z));
    if (!(scale.x > 0.0f) || !(scale.y > 0.0f) || !(scale.z > 0.0f))
    {
        orientation.Identity();
        return;
    }
    x /= scale.x;
    y /= scale.y;
    z /= scale.z;
    // A mirrored transform is not a rotation: move the mirror into the scale.
    if (DotProd(CrossProd(x, y), z) < 0.0f)
    {
        x = -x;
        scale.x = -scale.x;
    }
    Matrix3 rotation(x, y, z, Point3(0.0f, 0.0f, 0.0f));
    orientation = Quat(rotation);
    if (mode == QuatMode::Inverse)
        orientation = orientation.Inverse();
}

} // namespace

class OpenScatterOperator : public PFSimpleOperator
{
public:
    OpenScatterOperator();

    // Animatable
    void GetClassName(MSTR& s, bool) const override { s = _T("OpenScatterOperator"); }
    Class_ID ClassID() override { return OPENSCATTER_OPERATOR_CLASS_ID; }
    void BeginEditParams(IObjParam* ip, ULONG flags, Animatable* prev) override;
    void EndEditParams(IObjParam* ip, ULONG flags, Animatable* next) override;

    // ReferenceTarget
    RefTargetHandle Clone(RemapDir& remap) override;

    // BaseObject
    const MCHAR* GetObjectName(bool) const override { return _T("OpenScatterOperator"); }

    // IPFAction
    const ParticleChannelMask& ChannelsUsed(const Interval& time) const override;
    const Interval ActivityInterval() const override;
    bool IsFertile() const override { return true; }   // it creates its particles itself
    bool IsMaterialHolder() const override { return true; }
    Mtl* GetMaterial() override;

    // IPFOperator
    bool Proceed(IObject* pCont, PreciseTimeValue timeStart, PreciseTimeValue& timeEnd, Object* pSystem, INode* pNode,
                 INode* actionNode, IPFIntegrator* integrator) override;

    ClassDesc* GetClassDesc() const override;

    INode* scatterNode() const { return pblock() ? pblock()->GetINode(kOpScatter, 0) : nullptr; }

    // Whether a render started now must find particles here, and whether the last evaluation gave
    // any. Particle Flow keeps its particles from one render to the next when none of its actions
    // changed - and a change of renderer changes none - so the two can disagree: see refreshOperator.
    bool wouldEmit() const;
    bool lastEmit() const { return m_lastEmit; }

private:
    bool m_lastEmit = false;
};

namespace
{

class OpenScatterOperatorDesc : public ClassDesc2
{
public:
    int IsPublic() override { return 0; }
    void* Create(BOOL) override { return new OpenScatterOperator(); }
    const MCHAR* ClassName() override { return _T("OpenScatterOperator"); }
    const MCHAR* NonLocalizedClassName() override { return _T("OpenScatterOperator"); }
    SClass_ID SuperClassID() override { return HELPER_CLASS_ID; }
    Class_ID ClassID() override { return OPENSCATTER_OPERATOR_CLASS_ID; }
    Class_ID SubClassID() override { return PFOperatorSubClassID; }
    const MCHAR* Category() override { return _T("Particle Flow"); }
    const MCHAR* InternalName() override { return _T("OpenScatterOperator"); }
    HINSTANCE HInstance() override { return hInstance; }

    INT_PTR Execute(int cmd, ULONG_PTR arg1, ULONG_PTR, ULONG_PTR) override
    {
        static MCHAR description[] = _T("Carries the instances of an OpenScatter object. Created and kept by OpenScatter.");
        static MCHAR category[] = _T("Operator");
        switch (cmd)
        {
        case kPF_GetActionDescription:
            if (!arg1) return 0;
            *reinterpret_cast<MCHAR**>(arg1) = description;
            return 1;
        case kPF_PViewPublic:
            // Not offered in Particle View's depot: OpenScatter builds the carrier itself.
            if (!arg1) return 0;
            *reinterpret_cast<bool*>(arg1) = false;
            return 1;
        case kPF_PViewCategory:
            if (!arg1) return 0;
            *reinterpret_cast<MCHAR**>(arg1) = category;
            return 1;
        case kPF_IsFertile:
            if (!arg1) return 0;
            *reinterpret_cast<bool*>(arg1) = true;
            return 1;
        default:
            return 0;
        }
    }
};

OpenScatterOperatorDesc g_operatorDesc;

ParamBlockDesc2 g_operatorParams(
    kOperatorParamBlock, _T("Parameters"), 0, &g_operatorDesc, P_AUTO_CONSTRUCT, 0,

    kOpScatter, _T("scatter"), TYPE_INODE, 0, 0,
        p_end,
    p_end);

// Does the renderer draw this node at all?
bool nodeRenders(INode* node)
{
    return node && node->Renderable() && !node->IsNodeHidden(TRUE);
}

template <class Interface>
Interface* ensureChannel(IChannelContainer* container, Interface_ID writeID, Class_ID channelClass, Interface_ID readID)
{
    return static_cast<Interface*>(container->EnsureInterface(writeID, channelClass, true, readID, writeID, true));
}

} // namespace

ClassDesc2* getOpenScatterOperatorDesc() { return &g_operatorDesc; }

// Particle Flow starts simulating one step before the earliest moment a birth action is active, and
// proceeds from there to the time asked, a frame at a time. The interval must therefore begin at a
// real time: with one that never begins (FOREVER) it starts at tick 0, its first step is (0, 1 frame]
// and frame 0 itself is never computed - an empty render (measured). It begins at the first frame of
// the animation range, or at frame 0 when the range starts later, so every frame from there on has
// its particles. The price is one step per frame between that start and the frame rendered.
const Interval OpenScatterOperator::ActivityInterval() const
{
    const TimeValue start = GetCOREInterface()->GetAnimRange().Start();
    return Interval(std::min(start, TimeValue(0)), TIME_PosInfinity);
}

OpenScatterOperator::OpenScatterOperator()
{
    GetClassDesc()->MakeAutoParamBlocks(this);
}

ClassDesc* OpenScatterOperator::GetClassDesc() const { return &g_operatorDesc; }

void OpenScatterOperator::BeginEditParams(IObjParam* ip, ULONG flags, Animatable* prev)
{
    g_operatorDesc.BeginEditParams(ip, this, flags, prev);
}

void OpenScatterOperator::EndEditParams(IObjParam* ip, ULONG flags, Animatable* next)
{
    g_operatorDesc.EndEditParams(ip, this, flags, next);
}

RefTargetHandle OpenScatterOperator::Clone(RemapDir& remap)
{
    OpenScatterOperator* op = new OpenScatterOperator();
    op->ReplaceReference(0, remap.CloneRef(pblock()));
    BaseClone(this, op, remap);
    return op;
}

const ParticleChannelMask& OpenScatterOperator::ChannelsUsed(const Interval&) const
{
    static ParticleChannelMask mask(PCU_Amount,
                                    PCU_Amount | PCU_New | PCU_ID | PCU_Time | PCU_BirthTime | PCU_Position |
                                        PCU_Orientation | PCU_Scale | PCU_Shape);
    return mask;
}

// The particles take the material of their event, and the event takes it from here: the Multi/Sub-
// Object material that gathers the models' materials. Each shape's faces carry their model's slots.
Mtl* OpenScatterOperator::GetMaterial()
{
    OpenScatterObject* scatter = getOpenScatter(scatterNode());
    return scatter ? scatter->combinedMaterial() : nullptr;
}

bool OpenScatterOperator::Proceed(IObject* pCont, PreciseTimeValue timeStart, PreciseTimeValue& timeEnd, Object* pSystem,
                                  INode* systemNode, INode* actionNode, IPFIntegrator*)
{
    IParticleChannelAmountR* chAmountR = GetParticleChannelAmountRInterface(pCont);
    IParticleChannelAmountW* chAmountW = GetParticleChannelAmountWInterface(pCont);
    IChannelContainer* chCont = GetChannelContainerInterface(pCont);
    if (!chAmountR || !chAmountW || !chCont)
        return false;

    IPFSystem* pfSystem = pSystem ? GetPFSystemInterface(pSystem) : nullptr;
    IParticleObjectExt* particles = pSystem ? GetParticleObjectExtInterface(GetPFObject(pSystem)) : nullptr;

    INode* node = scatterNode();
    OpenScatterObject* scatter = getOpenScatter(node);
    const TimeValue t = TimeValue(timeEnd);

    // Particles exist only for a render through this route of a node the renderer draws.
    const bool renderState = pfSystem && pfSystem->IsRenderState();
    // An operator emits only under the PF Source its own tag names. A carrier cloned along with the
    // rest of a scene keeps the tag of the original: without this, both would emit the same instances.
    const bool own = !systemNode || !actionNode || carrierOwns(systemNode, actionNode);
    const bool emit = renderState && own && wouldEmit();
    if (renderState)
        m_lastEmit = emit;
    if (emit)
        scatter->update(t);
    std::unique_lock<std::recursive_mutex> lock;
    if (scatter)
        lock = std::unique_lock<std::recursive_mutex>(scatter->mutex());
    const InstanceBuffer* buffer = emit ? &scatter->buffer() : nullptr;

    const int wanted = buffer ? static_cast<int>(std::min<std::size_t>(buffer->instances.size(), INT_MAX / 2)) : 0;
    int amount = chAmountR->Count();
    if (logEnabled())
        logLine("pflow Proceed (%d, %d] render=%d emit=%d particles=%d wanted=%d", static_cast<int>(TimeValue(timeStart)), t,
                renderState ? 1 : 0, emit ? 1 : 0, amount, wanted);

    // --- make the number of particles match: the last ones go, or new ones are born ---
    if (amount > wanted)
    {
        if (wanted == 0)
            chAmountW->ZeroCount();
        else
            chAmountW->Delete(wanted, amount - wanted);
        amount = chAmountR->Count();
    }
    if (amount < wanted)
    {
        // The source has a limit on the particles it ever generates; the carrier sets it to the most
        // Particle Flow allows. Above it the last instances are left out (the report shows it).
        const int generated = particles ? particles->NumParticlesGenerated() : amount;
        const int allowance = pfSystem ? pfSystem->GetBornAllowance() : INT_MAX;
        const int born = std::min(wanted - amount, std::max(0, allowance - generated));
        if (born > 0)
        {
            IParticleChannelNewW* chNew = static_cast<IParticleChannelNewW*>(chCont->EnsureInterface(
                PARTICLECHANNELNEWW_INTERFACE, ParticleChannelNew_Class_ID, false, Interface_ID(0, 0), Interface_ID(0, 0)));
            IParticleChannelIDW* chID = static_cast<IParticleChannelIDW*>(chCont->EnsureInterface(
                PARTICLECHANNELIDW_INTERFACE, ParticleChannelID_Class_ID, false, Interface_ID(0, 0), Interface_ID(0, 0)));
            IParticleChannelPTVW* chTime = static_cast<IParticleChannelPTVW*>(chCont->EnsureInterface(
                PARTICLECHANNELTIMEW_INTERFACE, ParticleChannelPTV_Class_ID, true, PARTICLECHANNELTIMER_INTERFACE,
                PARTICLECHANNELTIMEW_INTERFACE));
            IParticleChannelPTVW* chBirth = static_cast<IParticleChannelPTVW*>(chCont->EnsureInterface(
                PARTICLECHANNELBIRTHTIMEW_INTERFACE, ParticleChannelPTV_Class_ID, true, PARTICLECHANNELBIRTHTIMER_INTERFACE,
                PARTICLECHANNELBIRTHTIMEW_INTERFACE));
            if (!chNew || !chID || !chTime || !chBirth)
                return false;
            const int first = amount;
            if (!chAmountW->AppendNum(born))
                return false;
            for (int i = first; i < first + born; ++i)
            {
                chNew->SetNew(i);
                chTime->SetValue(i, timeEnd);
                chID->SetID(i, i, generated + (i - first));
                chBirth->SetValue(i, timeEnd);
            }
        }
        amount = chAmountR->Count();
    }
    // The report describes renders: what the carrier holds while it idles in the viewport state is nothing.
    if (scatter && renderState && own)
        scatter->report().particles = static_cast<std::uint64_t>(amount > 0 ? amount : 0);
    if (amount <= 0 || !buffer)
        return true;
    amount = std::min(amount, wanted);

    // --- particle i is instance i ---
    IParticleChannelPoint3W* chPos = ensureChannel<IParticleChannelPoint3W>(
        chCont, PARTICLECHANNELPOSITIONW_INTERFACE, ParticleChannelPoint3_Class_ID, PARTICLECHANNELPOSITIONR_INTERFACE);
    IParticleChannelQuatW* chOrient = ensureChannel<IParticleChannelQuatW>(
        chCont, PARTICLECHANNELORIENTATIONW_INTERFACE, ParticleChannelQuat_Class_ID, PARTICLECHANNELORIENTATIONR_INTERFACE);
    IParticleChannelPoint3W* chScale = ensureChannel<IParticleChannelPoint3W>(
        chCont, PARTICLECHANNELSCALEW_INTERFACE, ParticleChannelPoint3_Class_ID, PARTICLECHANNELSCALER_INTERFACE);
    IParticleChannelMeshW* chMesh = ensureChannel<IParticleChannelMeshW>(
        chCont, PARTICLECHANNELSHAPEW_INTERFACE, ParticleChannelMesh_Class_ID, PARTICLECHANNELSHAPER_INTERFACE);
    if (!chPos || !chOrient || !chScale || !chMesh)
        return false;

    static const QuatMode quatMode = findQuatMode();
    for (int i = 0; i < amount; ++i)
    {
        Point3 position, scale;
        Quat orientation;
        decompose(toMatrix3(buffer->instances[static_cast<std::size_t>(i)].transform), quatMode, position, orientation, scale);
        chPos->SetValue(i, position);
        chOrient->SetValue(i, orientation);
        chScale->SetValue(i, scale);
    }

    // One mesh per model, shared by all its particles: a shared value is what lets the renderer
    // instance it. A value set particle by particle would be a copy each.
    const std::size_t numModels = buffer->models.size();
    std::size_t modelsInUse = 0;
    std::size_t onlyModel = 0;
    for (std::size_t m = 0; m < numModels && m < buffer->byModel.size(); ++m)
    {
        if (!buffer->byModel[m].empty())
        {
            ++modelsInUse;
            onlyModel = m;
        }
    }
    if (modelsInUse == 1)
    {
        chMesh->SetValue(const_cast<Mesh*>(&buffer->models[onlyModel].mesh));
    }
    else
    {
        for (std::size_t m = 0; m < numModels && m < buffer->byModel.size(); ++m)
        {
            const std::vector<std::uint32_t>& list = buffer->byModel[m];
            if (list.empty())
                continue;
            Tab<int> ids;
            ids.SetCount(static_cast<int>(list.size()));
            int n = 0;
            for (const std::uint32_t k : list)
                if (k < static_cast<std::uint32_t>(amount))
                    ids[n++] = static_cast<int>(k);
            ids.SetCount(n);
            if (n > 0)
                chMesh->SetValue(ids, const_cast<Mesh*>(&buffer->models[m].mesh));
        }
    }
    chPos->BuildBoundingBox();
    chMesh->BuildMaxBoundingBox();
    return true;
}

bool OpenScatterOperator::wouldEmit() const
{
    INode* node = scatterNode();
    OpenScatterObject* scatter = getOpenScatter(node);
    return scatter && nodeRenders(node) && scatter->activeRoute() == kRouteParticleFlow;
}

OpenScatterObject* scatterOfOperator(Object* object)
{
    if (!object || object->ClassID() != OPENSCATTER_OPERATOR_CLASS_ID)
        return nullptr;
    return getOpenScatter(static_cast<OpenScatterOperator*>(object)->scatterNode());
}

// Called when a render is about to start. If what the carrier gave Particle Flow the last time is
// not what this render needs - particles left from a Corona render when Arnold is now the renderer,
// or none when it is Corona's turn - Particle Flow is told that the operator changed, which makes it
// drop what it kept and ask again. Without this the left-over particles would be rendered next to
// the instances of the other route.
void refreshOperator(Object* object)
{
    if (!object || object->ClassID() != OPENSCATTER_OPERATOR_CLASS_ID)
        return;
    OpenScatterOperator* op = static_cast<OpenScatterOperator*>(object);
    if (op->lastEmit() != op->wouldEmit())
        op->NotifyDependents(FOREVER, PART_ALL, REFMSG_CHANGE);
}

} // namespace oscmax
