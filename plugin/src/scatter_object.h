// SPDX-License-Identifier: GPL-3.0-or-later
//
// OpenScatter plugin for 3ds Max - the OpenScatter object.
//
// One geometry object whose settings live in a ParamBlock2 (so MAXScript and Python read and write
// them with no extra work) and whose surfaces and models are ordinary 3ds Max references. It owns
// the instance buffer; the output adapters (instancing interface, Particle Flow operator, aggregate
// mesh, viewport) only read it.

#pragma once

#include "plugin.h"

namespace MaxSDK
{
namespace RenderTimeInstancing
{
class RenderTimeInstancingInterface;
}
} // namespace MaxSDK

namespace oscmax
{

class ViewportDisplay;   // viewport.cpp
class InstancingAdapter; // render_instancing.cpp

// The functions the object publishes to MAXScript and Python, next to its parameters.
class IOpenScatter : public FPMixinInterface
{
  public:
    enum
    {
        kFnUpdate = 0,
        kFnGetInstanceCount,
        kFnGetInstanceHash,
        kFnGetStatus,
        kFnGetStatistics,
        kFnGetActiveRoute,
        kFnGetRenderReport,
        kFnGetViewportReport,
        kFnGetCarrier,
        kFnSyncCarrier,
        kFnGetModelInstanceCount,
        kFnGetInstanceTransform,
        kFnGetInstanceModel,
        kFnGetVersion,
    };

    virtual void fnUpdate() = 0;
    virtual int fnGetInstanceCount() = 0;
    virtual MSTR fnGetInstanceHash() = 0;
    virtual MSTR fnGetStatus() = 0;
    virtual MSTR fnGetStatistics() = 0;
    virtual MSTR fnGetActiveRoute() = 0;
    virtual MSTR fnGetRenderReport() = 0;
    virtual MSTR fnGetViewportReport() = 0;
    virtual INode* fnGetCarrier() = 0;
    virtual void fnSyncCarrier() = 0;
    virtual int fnGetModelInstanceCount(int model) = 0;
    virtual Matrix3 fnGetInstanceTransform(int index) = 0;
    virtual int fnGetInstanceModel(int index) = 0;
    virtual MSTR fnGetVersion() = 0;

    BEGIN_FUNCTION_MAP
    VFN_0(kFnUpdate, fnUpdate);
    FN_0(kFnGetInstanceCount, TYPE_INT, fnGetInstanceCount);
    FN_0(kFnGetInstanceHash, TYPE_TSTR_BV, fnGetInstanceHash);
    FN_0(kFnGetStatus, TYPE_TSTR_BV, fnGetStatus);
    FN_0(kFnGetStatistics, TYPE_TSTR_BV, fnGetStatistics);
    FN_0(kFnGetActiveRoute, TYPE_TSTR_BV, fnGetActiveRoute);
    FN_0(kFnGetRenderReport, TYPE_TSTR_BV, fnGetRenderReport);
    FN_0(kFnGetViewportReport, TYPE_TSTR_BV, fnGetViewportReport);
    FN_0(kFnGetCarrier, TYPE_INODE, fnGetCarrier);
    VFN_0(kFnSyncCarrier, fnSyncCarrier);
    FN_1(kFnGetModelInstanceCount, TYPE_INT, fnGetModelInstanceCount, TYPE_INDEX);
    FN_1(kFnGetInstanceTransform, TYPE_MATRIX3_BV, fnGetInstanceTransform, TYPE_INDEX);
    FN_1(kFnGetInstanceModel, TYPE_INDEX, fnGetInstanceModel, TYPE_INDEX);
    FN_0(kFnGetVersion, TYPE_TSTR_BV, fnGetVersion);
    END_FUNCTION_MAP

    FPInterfaceDesc* GetDesc() override;
};

class OpenScatterObject : public GeomObject, public IOpenScatter
{
  public:
    explicit OpenScatterObject(BOOL loading);
    ~OpenScatterObject() override;

    // --- the instance buffer -----------------------------------------------------------------
    // Brings the buffer up to date for time t. Does nothing when no input changed. Evaluates scene
    // nodes, so it belongs on the thread that is allowed to do that (the main one, or the one the
    // renderer translates the scene on).
    void update(TimeValue t);

    // Readers hold this while they use the buffer.
    std::recursive_mutex& mutex()
    {
        return m_mutex;
    }
    const InstanceBuffer& buffer() const
    {
        return m_buffer;
    }
    Interval validity() const
    {
        return m_scatterValid & m_modelsValid;
    }

    // The one adapter that emits in a render started now.
    RenderRoute activeRoute() const;
    RenderReport& report()
    {
        return m_report;
    }

    // The Multi/Sub-Object material that gathers the models' materials, as the Particle Flow carrier
    // and the aggregate mesh need it. May be null until the first carrier sync.
    Mtl* combinedMaterial() const;
    // Rebuilds it from the models' node materials. Main thread only; returns true when it changed.
    bool syncCombinedMaterial();

    IParamBlock2* params() const
    {
        return pblock;
    }

    // --- Animatable --------------------------------------------------------------------------
    void DeleteThis() override
    {
        delete this;
    }
    Class_ID ClassID() override
    {
        return OPENSCATTER_CLASS_ID;
    }
    SClass_ID SuperClassID() override
    {
        return GEOMOBJECT_CLASS_ID;
    }
    void GetClassName(MSTR& s, bool) const override
    {
        s = _T("OpenScatter");
    }
    int NumSubs() override
    {
        return 1;
    }
    Animatable* SubAnim(int) override
    {
        return pblock;
    }
    MSTR SubAnimName(int, bool) override
    {
        return _T("Parameters");
    }
    int NumParamBlocks() override
    {
        return 1;
    }
    IParamBlock2* GetParamBlock(int) override
    {
        return pblock;
    }
    IParamBlock2* GetParamBlockByID(BlockID id) override
    {
        return (pblock && pblock->ID() == id) ? pblock : nullptr;
    }
    void* GetInterface(ULONG id) override
    {
        return GeomObject::GetInterface(id);
    }
    BaseInterface* GetInterface(Interface_ID id) override;
    void BeginEditParams(IObjParam* ip, ULONG flags, Animatable* prev) override;
    void EndEditParams(IObjParam* ip, ULONG flags, Animatable* next) override;

    // --- ReferenceMaker / ReferenceTarget ----------------------------------------------------
    int NumRefs() override
    {
        return 1;
    }
    RefTargetHandle GetReference(int) override
    {
        return pblock;
    }
    void SetReference(int, RefTargetHandle r) override
    {
        pblock = static_cast<IParamBlock2*>(r);
    }
    RefResult NotifyRefChanged(const Interval& changeInt, RefTargetHandle hTarget, PartID& partID, RefMessage message,
                               BOOL propagate) override;
    RefTargetHandle Clone(RemapDir& remap) override;
    IOResult Load(ILoad* iload) override;

    // --- BaseObject --------------------------------------------------------------------------
    CreateMouseCallBack* GetCreateMouseCallBack() override;
    const MCHAR* GetObjectName(bool) const override
    {
        return _T("OpenScatter");
    }
    int Display(TimeValue t, INode* inode, ViewExp* vpt, int flags) override;
    int HitTest(TimeValue t, INode* inode, int type, int crossing, int flags, IPoint2* p, ViewExp* vpt) override;
    void GetWorldBoundBox(TimeValue t, INode* inode, ViewExp* vpt, Box3& box) override;
    void GetLocalBoundBox(TimeValue t, INode* inode, ViewExp* vpt, Box3& box) override;
    unsigned long GetObjectDisplayRequirement() const override;
    bool PrepareDisplay(const MaxSDK::Graphics::UpdateDisplayContext& context) override;
    bool UpdatePerNodeItems(const MaxSDK::Graphics::UpdateDisplayContext& context,
                            MaxSDK::Graphics::UpdateNodeContext& nodeContext,
                            MaxSDK::Graphics::IRenderItemContainer& container) override;

    // --- Object ------------------------------------------------------------------------------
    ObjectState Eval(TimeValue) override
    {
        return ObjectState(this);
    }
    Interval ObjectValidity(TimeValue t) override;
    void InitNodeName(MSTR& s) override
    {
        s = _T("OpenScatter");
    }
    int IsRenderable() override
    {
        return TRUE;
    }
    int IsDeformable() override
    {
        return FALSE;
    }
    int CanConvertToType(Class_ID obtype) override;
    Object* ConvertToType(TimeValue t, Class_ID obtype) override;
    void GetDeformBBox(TimeValue t, Box3& box, Matrix3* tm, BOOL useSel) override;

    // --- GeomObject --------------------------------------------------------------------------
    Mesh* GetRenderMesh(TimeValue t, INode* inode, View& view, BOOL& needDelete) override;

    // --- IOpenScatter ------------------------------------------------------------------------
    void fnUpdate() override;
    int fnGetInstanceCount() override;
    MSTR fnGetInstanceHash() override;
    MSTR fnGetStatus() override;
    MSTR fnGetStatistics() override;
    MSTR fnGetActiveRoute() override;
    MSTR fnGetRenderReport() override;
    MSTR fnGetViewportReport() override;
    INode* fnGetCarrier() override;
    void fnSyncCarrier() override;
    int fnGetModelInstanceCount(int model) override;
    Matrix3 fnGetInstanceTransform(int index) override;
    int fnGetInstanceModel(int index) override;
    MSTR fnGetVersion() override;

  private:
    void refreshModels(TimeValue t);
    void refreshScatter(TimeValue t);
    // The aggregate mesh of every instance (thinned by rank to stay within "maxFaces"), with its
    // vertices taken from the world to the space "worldToObject" leads to.
    // Returns how many instances and faces went in.
    std::pair<std::uint64_t, std::uint64_t> buildAggregate(Mesh& out, const Matrix3& worldToObject, int maxFaces);

    IParamBlock2* pblock = nullptr;

    mutable std::recursive_mutex m_mutex;
    InstanceBuffer m_buffer;
    std::atomic<bool> m_scatterDirty{ true };
    std::atomic<bool> m_modelsDirty{ true };
    Interval m_scatterValid = NEVER;
    Interval m_modelsValid = NEVER;
    std::vector<float> m_engineWeights; // the model weights the current instances were made with
    std::uint64_t m_scatterRuns = 0;    // how many times the engine ran, and the models were read:
    std::uint64_t m_modelRuns = 0;      // lets a test see that nothing is recomputed without a reason

    Mesh m_aggregate; // what GetRenderMesh returned last
    Mesh m_empty;
    RenderReport m_report;

    InstancingAdapter* m_instancing = nullptr;
    ViewportDisplay* m_viewport = nullptr;

    friend class ViewportDisplay;
};

// render_instancing.cpp
InstancingAdapter* createInstancingAdapter(OpenScatterObject* owner);
void destroyInstancingAdapter(InstancingAdapter* adapter);
BaseInterface* instancingInterface(InstancingAdapter* adapter);

// viewport.cpp
ViewportDisplay* createViewportDisplay(OpenScatterObject* owner);
void destroyViewportDisplay(ViewportDisplay* display);
bool viewportUpdateItems(ViewportDisplay* display, const MaxSDK::Graphics::UpdateDisplayContext& context,
                         MaxSDK::Graphics::UpdateNodeContext& nodeContext,
                         MaxSDK::Graphics::IRenderItemContainer& container);
MSTR viewportReport(ViewportDisplay* display);

// carrier.cpp: the carrier (a PF Source node) of the scatter object, or null.
INode* findCarrier(OpenScatterObject* object);

} // namespace oscmax
