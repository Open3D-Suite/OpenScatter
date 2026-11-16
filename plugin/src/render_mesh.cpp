// SPDX-License-Identifier: GPL-3.0-or-later
//
// OpenScatter plugin for 3ds Max - the aggregate mesh.
//
// One mesh with a copy of the model at every instance: what a renderer with no instancing route
// gets from GetRenderMesh, and what collapsing the object gives. It duplicates geometry, so it is
// capped ("meshMaxFaces"): above the cap the instances are thinned evenly by rank.

#include "scatter_object.h"

#include <algorithm>

namespace oscmax
{

std::pair<std::uint64_t, std::uint64_t> OpenScatterObject::buildAggregate(Mesh& out, const Matrix3& worldToObject, int maxFaces)
{
    const InstanceBuffer& buffer = m_buffer;
    out.setNumVerts(0);
    out.setNumFaces(0);
    out.setMapSupport(1, FALSE);

    const std::size_t numModels = buffer.models.size();
    const std::size_t numInstances = buffer.instances.size();
    if (numModels == 0 || numInstances == 0)
        return { 0, 0 };

    // Count what the whole buffer would take, then thin by rank until it fits the cap. Models differ
    // in size, so the threshold is tightened until the faces really fit.
    const auto countFaces = [&](float threshold, std::uint64_t& faces, std::uint64_t& verts, std::uint64_t& kept) {
        faces = verts = kept = 0;
        for (const osc::Instance& inst : buffer.instances)
        {
            if (inst.model >= numModels || !(inst.rank < threshold))
                continue;
            const Mesh& mesh = buffer.models[inst.model].mesh;
            faces += static_cast<std::uint64_t>(mesh.getNumFaces());
            verts += static_cast<std::uint64_t>(mesh.getNumVerts());
            ++kept;
        }
    };
    // A Mesh counts its vertices and faces with an int.
    const std::uint64_t hardLimit = 0x7fffffffull / 4;
    const std::uint64_t limit = maxFaces > 0 ? std::min<std::uint64_t>(static_cast<std::uint64_t>(maxFaces), hardLimit) : hardLimit;
    float threshold = 2.0f;
    std::uint64_t faces = 0, verts = 0, kept = 0;
    countFaces(threshold, faces, verts, kept);
    std::size_t keep = numInstances;
    for (int pass = 0; pass < 64 && (faces > limit || verts > hardLimit) && keep > 0; ++pass)
    {
        const double ratio = std::min(static_cast<double>(limit) / static_cast<double>(faces),
                                      static_cast<double>(hardLimit) / static_cast<double>(std::max<std::uint64_t>(verts, 1)));
        keep = static_cast<std::size_t>(static_cast<double>(kept) * std::min(ratio, 1.0) * 0.98);
        threshold = rankThreshold(buffer, keep);
        countFaces(threshold, faces, verts, kept);
    }
    if (faces == 0 || faces > limit || verts > hardLimit)
        return { 0, 0 };

    bool anyUVs = false;
    for (const ModelData& model : buffer.models)
        anyUVs = anyUVs || (model.mesh.getNumFaces() > 0 && model.mesh.mapSupport(1) && model.mesh.getNumMapVerts(1) > 0);
    std::uint64_t mapVertsTotal = 0;
    if (anyUVs)
    {
        for (const osc::Instance& inst : buffer.instances)
        {
            if (inst.model >= numModels || !(inst.rank < threshold))
                continue;
            const Mesh& mesh = buffer.models[inst.model].mesh;
            if (mesh.getNumFaces() == 0)
                continue;
            mapVertsTotal += mesh.mapSupport(1) ? static_cast<std::uint64_t>(std::max(mesh.getNumMapVerts(1), 1)) : 1;
        }
        if (mapVertsTotal > hardLimit)
            anyUVs = false;
    }

    if (!out.setNumVerts(static_cast<int>(verts)) || !out.setNumFaces(static_cast<int>(faces)))
    {
        out.setNumVerts(0);
        out.setNumFaces(0);
        return { 0, 0 };
    }
    UVVert* mapVerts = nullptr;
    TVFace* mapFaces = nullptr;
    if (anyUVs)
    {
        out.setMapSupport(1, TRUE);
        out.setNumMapVerts(1, static_cast<int>(mapVertsTotal));
        out.setNumMapFaces(1, static_cast<int>(faces));
        mapVerts = out.mapVerts(1);
        mapFaces = out.mapFaces(1);
    }

    int v0 = 0;
    int f0 = 0;
    int t0 = 0;
    for (const osc::Instance& inst : buffer.instances)
    {
        if (inst.model >= numModels || !(inst.rank < threshold))
            continue;
        const Mesh& src = buffer.models[inst.model].mesh;
        const int nv = src.getNumVerts();
        const int nf = src.getNumFaces();
        if (nf == 0)
            continue;
        const Matrix3 tm = toMatrix3(inst.transform) * worldToObject;
        for (int v = 0; v < nv; ++v)
            out.verts[v0 + v] = src.verts[v] * tm;
        const bool srcUVs = src.mapSupport(1) && src.getNumMapVerts(1) > 0;
        const TVFace* srcMapFaces = srcUVs ? src.mapFaces(1) : nullptr;
        for (int f = 0; f < nf; ++f)
        {
            Face face = src.faces[f];   // smoothing group, edge visibility and material slot come along
            face.v[0] += static_cast<DWORD>(v0);
            face.v[1] += static_cast<DWORD>(v0);
            face.v[2] += static_cast<DWORD>(v0);
            out.faces[f0 + f] = face;
            if (mapFaces)
            {
                if (srcMapFaces)
                    mapFaces[f0 + f].setTVerts(static_cast<DWORD>(t0) + srcMapFaces[f].t[0], static_cast<DWORD>(t0) + srcMapFaces[f].t[1],
                                               static_cast<DWORD>(t0) + srcMapFaces[f].t[2]);
                else
                    mapFaces[f0 + f].setTVerts(static_cast<DWORD>(t0), static_cast<DWORD>(t0), static_cast<DWORD>(t0));
            }
        }
        if (mapVerts)
        {
            if (srcUVs)
            {
                const int nt = src.getNumMapVerts(1);
                const UVVert* srcMapVerts = src.mapVerts(1);
                for (int k = 0; k < nt; ++k)
                    mapVerts[t0 + k] = srcMapVerts[k];
                t0 += nt;
            }
            else
            {
                mapVerts[t0] = UVVert(0.0f, 0.0f, 0.0f);
                t0 += 1;
            }
        }
        v0 += nv;
        f0 += nf;
    }
    out.InvalidateGeomCache();
    out.InvalidateTopologyCache();

    return { kept, faces };
}

Mesh* OpenScatterObject::GetRenderMesh(TimeValue t, INode* inode, View&, BOOL& needDelete)
{
    needDelete = FALSE;
    // Only when the mesh is the active route: with any other one the instances reach the renderer
    // another way, and a mesh here would render them twice.
    if (activeRoute() != kRouteMesh)
        return &m_empty;

    update(t);
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    // The instances are in world space; the renderer places this mesh with the node's transform.
    Matrix3 worldToObject;
    worldToObject.IdentityMatrix();
    if (inode)
        worldToObject = Inverse(inode->GetObjectTM(t));
    const auto built = buildAggregate(m_aggregate, worldToObject, pblock ? pblock->GetInt(p_meshMaxFaces, t) : 0);
    m_report.meshInstances = built.first;
    m_report.meshFaces = built.second;
    return &m_aggregate;
}

} // namespace oscmax
