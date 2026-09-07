#pragma once
// Lumen-class cues at probe cost: radiance cache + DDGI clipmaps + SSGI/SSR.
// Triangles are never ray-traced on the low-end path. GI traces an SDF clipmap.
#include "aetherion/math/Types.hpp"
#include <vector>

namespace aetherion::render {

struct Surfel {
    vec3 p{};
    vec3 n{};
    f32  radius = 0.4f;
    f32  sh_r[9]{}; // SH2 incoming, 3 bands × 3 RGB packed as 9×R then... keep RGB SH2 = 27 f32? too fat.
    // SH1 (4 coeff) × RGB = 12 f32. Enough for first bounce.
    f32  sh[12]{};
    u32  last_relit = 0;
};

struct Probe {
    vec3 p{};
    // 16×16 octahedral irradiance RGB9E5 would be GPU-side. CPU ref: 8×8 RGB.
    f32  irr[8 * 8 * 3]{};
    f32  depth[8 * 8]{};
    u32  last_relit = 0;
};

struct DDGICascade {
    int  res       = 32;
    f32  spacing   = 1.5f;
    vec3 origin{};             // snapped to spacing
    std::vector<Probe> probes;
};

struct RadianceCache {
    static constexpr int kMaxSurfels = 65536;
    static constexpr int kRelightPerFrame = 4096;
    std::vector<Surfel> surfels;
    int  cursor = 0; // ring for relight
};

struct GIVolume {
    RadianceCache cache;
    DDGICascade   cascades[3];
    GIVolume();

    void snap_to_camera(vec3 cam);
    // Relight at most kRelightPerFrame surfels + 8 probes. Worker-thread safe.
    void relight(vec3 sun_dir, vec3 sun_radiance, u32 frame);
    // Sample irradiance at a world point (material resolve).
    vec3 sample_irradiance(vec3 p, vec3 n) const;
};

// Energy combine used by the deferred light pass.
inline vec3 compose_lighting(vec3 albedo, vec3 L_direct, vec3 E_ssgi, vec3 E_probe,
                             f32 ss_confidence, vec3 spec_ssr, vec3 spec_probe,
                             f32 ssr_conf) {
    vec3 diffuse = albedo * (E_ssgi + E_probe * (1.f - ss_confidence));
    vec3 spec    = spec_ssr + spec_probe * (1.f - ssr_conf);
    return L_direct + diffuse + spec;
}

} // namespace aetherion::render
