#pragma once
// Nanite-style cluster DAG. CPU never walks triangles.
// Offline cook produces meshlets (≤64 tris) grouped into clusters, simplified
// into parents. Runtime GPU (and this CPU reference for tests / cooking) picks
// a cut of the DAG whose projected error ≤ τ pixels.
#include "aetherion/math/Types.hpp"
#include <vector>

namespace aetherion::render {

constexpr int kMeshletMaxTris  = 64;
constexpr int kMeshletMaxVerts = 64;
constexpr int kClusterGroup    = 8;     // children per parent
constexpr f32 kPixelErrorTau   = 0.5f;  // 1.0 on low-tier

struct Meshlet {
    u32 vertex_offset = 0;
    u32 index_offset  = 0;
    u8  vertex_count  = 0;
    u8  triangle_count= 0;
    u8  _pad[2]{};
};

struct Cluster {
    vec3  sphere_c{};          // object space
    f32   sphere_r = 0;
    vec3  cone_axis{};         // outward
    f32   cone_cutoff = 1;     // cos(angle)  — 1 = never cull
    f32   error = 0;           // object-space quadric error of THIS cluster vs parent
    u32   meshlet_begin = 0;
    u16   meshlet_count = 0;
    u16   material_id = 0;
    u32   parent = ~0u;        // ~0 = root
    u32   first_child = ~0u;
    u8    child_count = 0;
    u8    flags = 0;           // 1 = leaf
    u8    _pad[2]{};
};

struct ClusterDAG {
    std::vector<Cluster> clusters;
    std::vector<Meshlet> meshlets;
    std::vector<vec3>    positions;   // object space, cooked
    std::vector<u32>     indices;     // 3 per triangle, 8-bit local packed into u32
    std::vector<u32>     child_ids;   // packed ranges, Cluster::first_child is an offset
    f32  lod_scale = 1;               // object size compensation
    u32  root = 0;
};

struct InstanceRec {
    u32 matrix_index = 0;
    u32 dag_index    = 0;
    f32 lod_scale    = 1;
    u32 flags        = 0;
};

struct CullView {
    vec3  cam_pos{};
    vec3  frustum_planes[6]{}; // xyz = normal, stored in a packed vec3 + d separately
    f32   frustum_d[6]{};
    f32   tan_half_fov = 0.5f;
    f32   screen_h     = 1080;
    f32   tau          = kPixelErrorTau;
    const f32* hiz     = nullptr; // optional 1/4-res min-z, width*height
    int   hiz_w = 0, hiz_h = 0;
};

// Projected pixel error of a cluster.
inline f32 projected_error(const Cluster& c, vec3 cam, f32 tan_half_fov,
                           f32 screen_h, f32 lod_scale) {
    vec3 d = {c.sphere_c.x - cam.x, c.sphere_c.y - cam.y, c.sphere_c.z - cam.z};
    f32  z = length(d);
    if (z < 1e-3f) z = 1e-3f;
    return (c.error * lod_scale * screen_h) / (2.f * tan_half_fov * z);
}

inline bool cone_cull(const Cluster& c, vec3 cam) {
    vec3 v = normalize(vec3{c.sphere_c.x - cam.x, c.sphere_c.y - cam.y, c.sphere_c.z - cam.z});
    // If the view direction is outside the normal cone, the cluster is back-facing.
    return dot(v, c.cone_axis) > c.cone_cutoff;
}

inline bool frustum_cull(const Cluster& c, const CullView& v) {
    for (int i = 0; i < 6; ++i) {
        f32 dist = dot(c.sphere_c, v.frustum_planes[i]) + v.frustum_d[i];
        if (dist < -c.sphere_r) return true;
    }
    return false;
}

// CPU reference of the GPU cull. Writes surviving cluster indices.
// A real backend dumps this as an indirect args buffer.
int cull_dag(const ClusterDAG& dag, const CullView& view,
             u32* out_clusters, int max_out);

// Decide HW vs SW raster: projected area < 16 px² → software.
inline bool software_raster_bin(const Cluster& c, vec3 cam, f32 tan_half_fov, f32 screen_h) {
    vec3 d = {c.sphere_c.x - cam.x, c.sphere_c.y - cam.y, c.sphere_c.z - cam.z};
    f32  z = length(d);
    if (z < 1e-3f) z = 1e-3f;
    f32 r_px = (c.sphere_r * screen_h) / (2.f * tan_half_fov * z);
    return (r_px * r_px * 3.14159f) < 16.f;
}

// Quadric-error simplification used by the cooker (CPU). Groups of K meshlets
// become a parent whose error is the max vertex deviation after collapse.
ClusterDAG build_dag_from_triangles(const vec3* verts, int nverts,
                                    const u32* indices, int nindices,
                                    u16 material_id);

} // namespace aetherion::render
