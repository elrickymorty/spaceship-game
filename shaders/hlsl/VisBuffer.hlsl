// Aetherion — visibility buffer raster + material resolve.
// Cluster payload: [31:8 cluster_id][7:0 triangle_id]
// SW path: 64-lane compute, atomicMin on 64-bit depth+payload (reverse-Z).

#define CLUSTER_SHIFT 8
#define TRI_MASK      0xFFu

uint pack_vis(uint cluster, uint tri) { return (cluster << CLUSTER_SHIFT) | (tri & TRI_MASK); }

// Log reverse-Z matching math::log_depth.
float log_depth(float w, float far_plane, float kappa) {
    float num = log2(kappa * w + 1.0);
    float den = log2(kappa * far_plane + 1.0);
    return 1.0 - num / den;
}

// Visual warp (orbital / system rings). Inverse is CPU-side for picking.
float3 visual_warp(float3 p, float Rw) {
    float rho = length(p);
    return p / (1.0 + rho / Rw);
}

// Pixel error of a cluster — GPU cull mirror of render::projected_error.
float projected_error(float err, float lod_scale, float screen_h, float tan_half_fov, float z) {
    z = max(z, 1e-3);
    return (err * lod_scale * screen_h) / (2.0 * tan_half_fov * z);
}

bool software_bin(float radius, float screen_h, float tan_half_fov, float z) {
    float r_px = (radius * screen_h) / (2.0 * tan_half_fov * max(z, 1e-3));
    return (r_px * r_px * 3.14159) < 16.0;
}

// Barycentric reconstruction from visbuffer (material resolve).
float3 vis_barycentric(float3 p0, float3 p1, float3 p2, float2 pixel, float2 res, float4x4 vp) {
    float4 c0 = mul(vp, float4(p0, 1)), c1 = mul(vp, float4(p1, 1)), c2 = mul(vp, float4(p2, 1));
    float2 s0 = (c0.xy / c0.w * 0.5 + 0.5) * res;
    float2 s1 = (c1.xy / c1.w * 0.5 + 0.5) * res;
    float2 s2 = (c2.xy / c2.w * 0.5 + 0.5) * res;
    float2 v0 = s1 - s0, v1 = s2 - s0, v2 = pixel - s0;
    float den = v0.x * v1.y - v1.x * v0.y;
    float a = (v2.x * v1.y - v1.x * v2.y) / den;
    float b = (v0.x * v2.y - v2.x * v0.y) / den;
    return float3(1 - a - b, a, b);
}

// Four material classes — flags in a 32-bit word, one uber each.
// bit0 POM  bit1 SSS  bit2 aniso  bit3 clearcoat  bit4 wind  bit5 water
float3 ggx_ndf(float ndh, float a) {
    float a2 = a * a;
    float d = ndh * ndh * (a2 - 1) + 1;
    return a2 / (3.14159265 * d * d);
}
float smith_g(float ndv, float ndl, float a) {
    float k = (a + 1) * (a + 1) / 8;
    float gv = ndv / (ndv * (1 - k) + k);
    float gl = ndl / (ndl * (1 - k) + k);
    return gv * gl;
}

// Parallax occlusion — 4 steps far, 16 near. Distance in meters.
float2 pom(float2 uv, float3 tan_view, float height, float dist_m) {
    int steps = dist_m > 20.0 ? 4 : 16;
    float3 tv = normalize(tan_view);
    float2 delta = tv.xy * (height / max(tv.z, 0.08)) / steps;
    float2 p = uv;
    float layer = 1.0, stepH = 1.0 / steps;
    // Caller samples the height atlas; this is the walk.
    [loop] for (int i = 0; i < steps; ++i) { p -= delta; layer -= stepH; }
    return p;
}

// Screen-space SSS (Jensen diffusion, 3-tap).
float3 sss_jensen(float3 albedo, float ndl, float thickness) {
    float3 profile = exp(-float3(0.8, 0.4, 0.2) * thickness);
    return albedo * (max(ndl, 0) + profile * 0.35);
}
