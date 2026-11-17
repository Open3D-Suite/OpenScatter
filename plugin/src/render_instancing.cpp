// SPDX-License-Identifier: GPL-3.0-or-later
//
// OpenScatter plugin for 3ds Max - output through the standard render-time instancing interface
// (MaxSDK::RenderTimeInstancing::RenderTimeInstancingInterface, 3ds Max 2023 and later).
//
// The renderers that read the interface (Arnold; V-Ray through its own wrapper) get one source per
// model - the model's node, so they use its object and its material as they are - and one target per
// instance with its world transform and its stable id. Nothing is copied: a target is filled from
// the instance buffer when the renderer asks for it.

#include "scatter_object.h"

#include <Rendering/RenderTimeInstancing.h>
#include <containers/Array.h>

using namespace MaxSDK::RenderTimeInstancing;

namespace oscmax
{

namespace
{

class Target : public RenderInstanceTarget
{
  public:
    Matrix3 tm;
    __int64 id = 0;
    int numTMs = 1;

    void* GetCustomData(ChannelID) const override
    {
        return nullptr;
    }
    float GetCustomInt(ChannelID) const override
    {
        return 0.0f;
    }
    float GetCustomFloat(ChannelID) const override
    {
        return 0.0f;
    }
    Point3 GetCustomVector(ChannelID) const override
    {
        return Point3(0.0f, 0.0f, 0.0f);
    }
    Color GetCustomColor(ChannelID) const override
    {
        return Color(0.0f, 0.0f, 0.0f);
    }
    Matrix3 GetCustomTM(ChannelID) const override
    {
        Matrix3 m;
        m.IdentityMatrix();
        return m;
    }
    __int64 GetID() const override
    {
        return id;
    }
    __int64 GetInstanceID() const override
    {
        return id;
    }
    bool GetLife(float&, float&) const override
    {
        return false;
    }
    Mtl* GetMtlOverride() const override
    {
        return nullptr;
    }
    MaxSDK::Array<InstanceMatIDInfo> GetMatIDOverrides() const override
    {
        return MaxSDK::Array<InstanceMatIDInfo>();
    }
    MaxSDK::Array<InstanceUVWInfo> GetUVWOverrides() const override
    {
        return MaxSDK::Array<InstanceUVWInfo>();
    }
    MaxSDK::Array<Matrix3> GetTMs() const override
    {
        // The instances do not move: the same transform as many times as the renderer asked for.
        MaxSDK::Array<Matrix3> tms;
        for (int i = 0; i < numTMs; ++i)
            tms.append(tm);
        return tms;
    }
    Matrix3 GetTM() const override
    {
        return tm;
    }
    Point3 GetVelocity() const override
    {
        return Point3(0.0f, 0.0f, 0.0f);
    }
    AngAxis GetSpin() const override
    {
        return AngAxis(Point3(0.0f, 0.0f, 1.0f), 0.0f);
    }
};

} // namespace

class InstancingAdapter;

class Source : public RenderInstanceSource
{
  public:
    InstancingAdapter* adapter = nullptr;
    std::size_t model = 0;

    DataFlags GetFlags() const override
    {
        return df_inode;
    }
    void* GetData() const override;
    int GetVelocityMapChannel() const override
    {
        return -1;
    }
    size_t GetNumInstanceTargets() const override;
    const RenderInstanceTarget* GetRenderInstanceTarget(size_t index) const override;
};

class InstancingAdapter : public RenderTimeInstancingInterface
{
  public:
    explicit InstancingAdapter(OpenScatterObject* owner) : m_owner(owner)
    {
    }

    void UpdateInstanceData(TimeValue t, Interval& valid, MotionBlurInfo& mbinfo, const View&, const TSTR&) override
    {
        m_owner->update(t);
        std::lock_guard<std::recursive_mutex> lock(m_owner->mutex());
        const InstanceBuffer& buffer = m_owner->buffer();

        // One source per model that has instances. The list is rebuilt for every render; the
        // renderer reads the buffer through it until ReleaseInstanceData.
        m_sources.clear();
        if (m_owner->activeRoute() == kRouteInstancing)
        {
            for (std::size_t m = 0; m < buffer.models.size(); ++m)
            {
                if (!buffer.models[m].node || m >= buffer.byModel.size() || buffer.byModel[m].empty())
                    continue;
                Source source;
                source.adapter = this;
                source.model = m;
                m_sources.push_back(source);
            }
        }

        // 0 or 1: one transform. 2 or more: the renderer wants exactly that many, all equal here.
        // -1 ("any number"): we answer 1.
        m_numTMs = mbinfo.numberOfTMs >= 2 ? mbinfo.numberOfTMs : 1;
        if (mbinfo.numberOfTMs < 0)
            mbinfo.numberOfTMs = 1;
        valid &= m_owner->validity();
    }

    void ReleaseInstanceData() override
    {
    }

    MaxSDK::Array<ChannelInfo> GetChannels() const override
    {
        return MaxSDK::Array<ChannelInfo>();
    }
    // The interface declares this with the TypeID of the namespace, not ChannelInfo::TypeID.
    ChannelID GetChannelID(const TSTR&, MaxSDK::RenderTimeInstancing::TypeID) const override
    {
        return -1;
    }

    size_t GetNumInstanceSources() const override
    {
        return m_sources.size();
    }
    const RenderInstanceSource* GetRenderInstanceSource(size_t index) const override
    {
        return index < m_sources.size() ? &m_sources[index] : nullptr;
    }

    OpenScatterObject* owner() const
    {
        return m_owner;
    }
    int numTMs() const
    {
        return m_numTMs;
    }

  private:
    OpenScatterObject* m_owner;
    std::vector<Source> m_sources;
    int m_numTMs = 1;
};

void* Source::GetData() const
{
    const InstanceBuffer& buffer = adapter->owner()->buffer();
    return model < buffer.models.size() ? buffer.models[model].node : nullptr;
}

size_t Source::GetNumInstanceTargets() const
{
    const InstanceBuffer& buffer = adapter->owner()->buffer();
    return model < buffer.byModel.size() ? buffer.byModel[model].size() : 0;
}

const RenderInstanceTarget* Source::GetRenderInstanceTarget(size_t index) const
{
    // The SDK lets the object reuse the memory it returns, as long as it is kept apart per thread.
    thread_local Target target;
    OpenScatterObject* owner = adapter->owner();
    const InstanceBuffer& buffer = owner->buffer();
    if (model >= buffer.byModel.size() || index >= buffer.byModel[model].size())
        return nullptr;
    const std::uint32_t k = buffer.byModel[model][index];
    if (k >= buffer.instances.size())
        return nullptr;
    const osc::Instance& inst = buffer.instances[k];
    // A node source is drawn from its object space; the instance places the model's pivot.
    target.tm = buffer.models[model].pivotTM * toMatrix3(inst.transform);
    target.id = static_cast<__int64>(inst.id & 0x7fffffffffffffffull);
    target.numTMs = adapter->numTMs();
    owner->report().instancingTargets.fetch_add(1, std::memory_order_relaxed);
    return &target;
}

InstancingAdapter* createInstancingAdapter(OpenScatterObject* owner)
{
    return new InstancingAdapter(owner);
}

void destroyInstancingAdapter(InstancingAdapter* adapter)
{
    delete adapter;
}

BaseInterface* instancingInterface(InstancingAdapter* adapter)
{
    return adapter;
}

} // namespace oscmax
