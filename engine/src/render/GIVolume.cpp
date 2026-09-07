#include "aetherion/render/GIVolume.hpp"
#include <algorithm>
#include <cmath>

namespace aetherion::render {

static vec3 sh1_eval(const f32 sh[12], vec3 n) {
    // RGB packed as [R0..R3, G0..G3, B0..B3]. SH1: Y00, Y1-1, Y10, Y11.
    const f32 Y[4] = {0.282095f, 0.488603f * n.y, 0.488603f * n.z, 0.488603f * n.x};
    vec3 c{};
    for (int i = 0; i < 4; ++i) {
        c.x += sh[i]     * Y[i];
        c.y += sh[4 + i] * Y[i];
        c.z += sh[8 + i] * Y[i];
    }
    return {std::max(0.f, c.x), std::max(0.f, c.y), std::max(0.f, c.z)};
}

static void sh1_accum(f32 sh[12], vec3 dir, vec3 rgb, f32 w) {
    const f32 Y[4] = {0.282095f, 0.488603f * dir.y, 0.488603f * dir.z, 0.488603f * dir.x};
    for (int i = 0; i < 4; ++i) {
        sh[i]     += rgb.x * Y[i] * w;
        sh[4 + i] += rgb.y * Y[i] * w;
        sh[8 + i] += rgb.z * Y[i] * w;
    }
}

GIVolume::GIVolume() {
    const int res[3] = {32, 16, 8};
    const f32 sp[3]  = {1.5f, 6.f, 24.f};
    for (int c = 0; c < 3; ++c) {
        cascades[c].res = res[c];
        cascades[c].spacing = sp[c];
        cascades[c].probes.resize(std::size_t(res[c] * res[c] * res[c]));
    }
    cache.surfels.reserve(4096);
}

void GIVolume::snap_to_camera(vec3 cam) {
    for (int c = 0; c < 3; ++c) {
        f32 s = cascades[c].spacing;
        auto q = [s](f32 v) { return std::floor(v / s + 0.5f) * s; };
        cascades[c].origin = {q(cam.x), q(cam.y), q(cam.z)};
        const int r = cascades[c].res;
        const f32 half = 0.5f * f32(r) * s;
        int i = 0;
        for (int z = 0; z < r; ++z)
        for (int y = 0; y < r; ++y)
        for (int x = 0; x < r; ++x, ++i) {
            cascades[c].probes[std::size_t(i)].p = {
                cascades[c].origin.x - half + (f32(x) + 0.5f) * s,
                cascades[c].origin.y - half + (f32(y) + 0.5f) * s,
                cascades[c].origin.z - half + (f32(z) + 0.5f) * s
            };
        }
    }
}

void GIVolume::relight(vec3 sun_dir, vec3 sun_radiance, u32 frame) {
    sun_dir = normalize(sun_dir);
    const int n = int(cache.surfels.size());
    const int take = std::min(RadianceCache::kRelightPerFrame, n);
    for (int k = 0; k < take; ++k) {
        int i = (cache.cursor + k) % (n > 0 ? n : 1);
        if (n == 0) break;
        Surfel& s = cache.surfels[std::size_t(i)];
        for (int t = 0; t < 12; ++t) s.sh[t] = 0;
        const f32 ndl = std::max(0.f, dot(s.n, sun_dir));
        sh1_accum(s.sh, sun_dir, sun_radiance * ndl, 1.f);
        // 8 nearest probes as second bounce.
        vec3 bounce{};
        int used = 0;
        const DDGICascade& cas = cascades[0];
        for (int p = 0; p < 8 && p < int(cas.probes.size()); ++p) {
            const Probe& pr = cas.probes[std::size_t((i + p * 17) % cas.probes.size())];
            bounce += vec3{pr.irr[0], pr.irr[1], pr.irr[2]};
            ++used;
        }
        if (used) {
            bounce = bounce * (1.f / f32(used)) * 0.4f; // 2nd bounce scale
            sh1_accum(s.sh, s.n, bounce, 1.f);
        }
        s.last_relit = frame;
    }
    if (n) cache.cursor = (cache.cursor + take) % n;

    // 8 probes / frame — cheapest possible: copy sky + sun into octahedral.
    static int probe_cursor = 0;
    for (int k = 0; k < 8; ++k) {
        Probe& pr = cascades[0].probes[std::size_t(probe_cursor % cascades[0].probes.size())];
        for (int t = 0; t < 8 * 8; ++t) {
            pr.irr[t * 3 + 0] = sun_radiance.x * 0.05f;
            pr.irr[t * 3 + 1] = sun_radiance.y * 0.05f;
            pr.irr[t * 3 + 2] = sun_radiance.z * 0.05f;
            pr.depth[t] = 4.f;
        }
        pr.last_relit = frame;
        ++probe_cursor;
    }
}

vec3 GIVolume::sample_irradiance(vec3 p, vec3 n) const {
    vec3 E{};
    int wsum_i = 0;
    f32 wsum = 0;
    // Trilinear of cascade 0, 8 neighbours.
    const DDGICascade& cas = cascades[0];
    f32 s = cas.spacing;
    vec3 o = cas.origin;
    int r = cas.res;
    f32 half = 0.5f * f32(r) * s;
    vec3 lp = (p - (o - vec3{half, half, half})) / s;
    int x0 = clamp(int(lp.x), 0, r - 2);
    int y0 = clamp(int(lp.y), 0, r - 2);
    int z0 = clamp(int(lp.z), 0, r - 2);
    auto idx = [r](int x, int y, int z) { return (z * r + y) * r + x; };
    for (int dz = 0; dz < 2; ++dz)
    for (int dy = 0; dy < 2; ++dy)
    for (int dx = 0; dx < 2; ++dx) {
        const Probe& pr = cas.probes[std::size_t(idx(x0+dx, y0+dy, z0+dz))];
        vec3 d = pr.p - p;
        f32  w = 1.f / std::max(0.2f, length(d));
        w *= std::max(0.f, dot(n, normalize(d) * -1.f + n)); // backface-weight
        E += vec3{pr.irr[0], pr.irr[1], pr.irr[2]} * w;
        wsum += w;
        ++wsum_i;
    }
    if (wsum > 0) E = E / wsum;
    // Surfel gather (8 nearest by hash — production uses a grid).
    if (!cache.surfels.empty()) {
        vec3 Es{};
        int take = std::min(8, int(cache.surfels.size()));
        for (int i = 0; i < take; ++i) {
            const Surfel& s = cache.surfels[std::size_t(i)];
            f32 nd = std::max(0.f, dot(n, s.n));
            Es += sh1_eval(s.sh, n) * nd;
        }
        E = E * 0.5f + Es * (0.5f / f32(take));
    }
    (void)wsum_i;
    return E;
}

} // namespace aetherion::render
