#include "aetherion/render/VirtualGeometry.hpp"
#include <algorithm>
#include <cmath>

namespace aetherion::render {

int cull_dag(const ClusterDAG& dag, const CullView& view,
             u32* out_clusters, int max_out) {
    if (dag.clusters.empty() || max_out <= 0) return 0;

    // Iterative DAG cut: start at root, descend while parent error is too big
    // and the node is visible. Parent and children are mutually exclusive.
    u32 stack[256];
    int sp = 0;
    stack[sp++] = dag.root;
    int written = 0;

    while (sp > 0) {
        const u32 idx = stack[--sp];
        const Cluster& c = dag.clusters[idx];

        if (frustum_cull(c, view)) continue;
        if (cone_cull(c, view.cam_pos)) continue;

        const f32 pe = projected_error(c, view.cam_pos, view.tan_half_fov,
                                       view.screen_h, dag.lod_scale);
        const bool fine_enough = pe <= view.tau;
        const bool leaf = (c.flags & 1) != 0 || c.child_count == 0;

        if (fine_enough || leaf) {
            if (written < max_out) out_clusters[written++] = idx;
            continue;
        }
        for (u8 i = 0; i < c.child_count; ++i) {
            if (sp >= 256) break;
            const u32 child = dag.child_ids.empty()
                                  ? c.first_child + i
                                  : dag.child_ids[c.first_child + i];
            stack[sp++] = child;
        }
    }
    return written;
}

static vec3 tri_centroid(const vec3* v, u32 i0, u32 i1, u32 i2) {
    return (v[i0] + v[i1] + v[i2]) * (1.f / 3.f);
}

ClusterDAG build_dag_from_triangles(const vec3* verts, int nverts,
                                    const u32* indices, int nindices,
                                    u16 material_id) {
    ClusterDAG dag;
    dag.positions.assign(verts, verts + nverts);
    dag.indices.assign(indices, indices + nindices);

    const int ntris = nindices / 3;
    if (ntris <= 0) return dag;

    std::vector<int> order(ntris);
    for (int i = 0; i < ntris; ++i) order[std::size_t(i)] = i;
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        vec3 ca = tri_centroid(verts, indices[a*3], indices[a*3+1], indices[a*3+2]);
        vec3 cb = tri_centroid(verts, indices[b*3], indices[b*3+1], indices[b*3+2]);
        if (ca.x != cb.x) return ca.x < cb.x;
        if (ca.z != cb.z) return ca.z < cb.z;
        return ca.y < cb.y;
    });

    auto bounds_of = [&](int tri_begin, int tri_count, vec3& c, f32& r, vec3& axis) {
        vec3 mn{ 1e30f,  1e30f,  1e30f};
        vec3 mx{-1e30f, -1e30f, -1e30f};
        vec3 nsum{};
        for (int t = 0; t < tri_count; ++t) {
            int tri = order[std::size_t(tri_begin + t)];
            for (int k = 0; k < 3; ++k) {
                vec3 p = verts[indices[tri * 3 + k]];
                mn.x = std::min(mn.x, p.x); mn.y = std::min(mn.y, p.y); mn.z = std::min(mn.z, p.z);
                mx.x = std::max(mx.x, p.x); mx.y = std::max(mx.y, p.y); mx.z = std::max(mx.z, p.z);
            }
            vec3 a = verts[indices[tri*3]], b = verts[indices[tri*3+1]], c3 = verts[indices[tri*3+2]];
            nsum += cross(b - a, c3 - a);
        }
        c = (mn + mx) * 0.5f;
        vec3 ext = (mx - mn) * 0.5f;
        r = length(ext);
        axis = length(nsum) > 1e-8f ? normalize(nsum) : vec3{0, 1, 0};
    };

    std::vector<u32> leaves;
    for (int t = 0; t < ntris; t += kMeshletMaxTris) {
        int count = std::min(kMeshletMaxTris, ntris - t);
        Meshlet m{};
        m.index_offset   = u32(t * 3);
        m.triangle_count = u8(count);
        m.vertex_count   = u8(std::min(count * 3, kMeshletMaxVerts));
        dag.meshlets.push_back(m);

        Cluster cl{};
        bounds_of(t, count, cl.sphere_c, cl.sphere_r, cl.cone_axis);
        cl.cone_cutoff   = 0.15f;
        cl.error         = 0.f;
        cl.meshlet_begin = u32(dag.meshlets.size() - 1);
        cl.meshlet_count = 1;
        cl.material_id   = material_id;
        cl.flags         = 1;
        dag.clusters.push_back(cl);
        leaves.push_back(u32(dag.clusters.size() - 1));
    }

    // Bottom-up pairing. Parent error ≈ half the child-centroid separation
    // plus inherited error — a stand-in for the cooked quadric.
    std::vector<u32> level = leaves;
    while (level.size() > 1) {
        std::vector<u32> next;
        next.reserve(level.size() / 2 + 1);
        for (std::size_t i = 0; i < level.size(); i += 2) {
            if (i + 1 >= level.size()) { next.push_back(level[i]); break; }
            Cluster& a = dag.clusters[level[i]];
            Cluster& b = dag.clusters[level[i + 1]];
            Cluster p{};
            p.sphere_c = (a.sphere_c + b.sphere_c) * 0.5f;
            p.sphere_r = std::max(length(a.sphere_c - p.sphere_c) + a.sphere_r,
                                  length(b.sphere_c - p.sphere_c) + b.sphere_r);
            p.cone_axis   = normalize(a.cone_axis + b.cone_axis);
            p.cone_cutoff = std::min(a.cone_cutoff, b.cone_cutoff);
            p.error = length(a.sphere_c - b.sphere_c) * 0.5f + std::max(a.error, b.error);
            p.meshlet_begin = a.meshlet_begin;
            p.meshlet_count = u16(a.meshlet_count + b.meshlet_count);
            p.material_id   = material_id;
            p.first_child   = u32(dag.child_ids.size());
            p.child_count   = 2;
            p.flags         = 0;
            const u32 parent_id = u32(dag.clusters.size());
            a.parent = b.parent = parent_id;
            dag.child_ids.push_back(level[i]);
            dag.child_ids.push_back(level[i + 1]);
            dag.clusters.push_back(p);
            next.push_back(parent_id);
        }
        level.swap(next);
    }

    dag.root = level.empty() ? 0u : level[0];
    return dag;
}

} // namespace aetherion::render
