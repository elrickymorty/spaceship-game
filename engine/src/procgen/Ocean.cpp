#include "aetherion/procgen/Ocean.hpp"
#include <cmath>

namespace aetherion::procgen {

OceanSpectrum spectrum_from_genome(f32 wind_u, f32 depth, u64 seed) {
    OceanSpectrum s;
    s.wind_u = wind_u;
    s.depth = depth;
    s.seed = seed;
    const f32 g = 9.80665f;
    // Pierson–Moskowitz peak: ωp ≈ 0.86 g / U
    const f32 wp = 0.86f * g / std::max(2.f, wind_u);
    for (int i = 0; i < 6; ++i) {
        u32 h = pcg(u32(seed) + u32(i) * 17u);
        f32 frac = 0.55f + 0.25f * f32(i) + (hash01(h, 1, 2) - 0.5f) * 0.08f;
        f32 omega = wp * frac;
        f32 k = omega * omega / g; // deep-water inverse
        f32 ang = (hash01(h, 3, 4) - 0.5f) * 0.7f; // ±20° around wind (x)
        GerstnerWave w;
        w.dir = normalize(vec3{std::cos(ang), 0, std::sin(ang)});
        w.k = k;
        w.omega = dispersion(k, depth);
        w.amplitude = (wind_u * wind_u) / g * 0.018f / (1.f + f32(i) * 0.65f);
        w.phase = hash01(h, 5, 6) * 6.283185f;
        w.Q = 0;
        s.waves[i] = w;
    }
    // Steepness Q_i = 1 / (k_i A_i N)
    f32 acc = 0;
    for (int i = 0; i < 6; ++i) acc += s.waves[i].k * s.waves[i].amplitude;
    for (int i = 0; i < 6; ++i) {
        f32 denom = acc > 1e-6f ? acc : 1e-6f;
        s.waves[i].Q = 1.f / denom;
        s.waves[i].Q = clamp(s.waves[i].Q, 0.f, 0.9f);
    }
    s.count = 6;
    return s;
}

OceanSample sample_ocean(const OceanSpectrum& s, f32 x, f32 z, f32 t) {
    OceanSample o;
    vec3 dPdx{1, 0, 0}, dPdz{0, 0, 1};
    vec3 pos{x, 0, z};
    for (int i = 0; i < s.count; ++i) {
        const GerstnerWave& w = s.waves[i];
        f32 d = w.dir.x * x + w.dir.z * z;
        f32 theta = w.k * d - w.omega * t + w.phase;
        f32 sth = std::sin(theta), cth = std::cos(theta);
        f32 Qa = w.Q * w.amplitude;
        pos.x += w.dir.x * Qa * cth;
        pos.z += w.dir.z * Qa * cth;
        pos.y += w.amplitude * sth;

        // Jacobian bits for foam and normals.
        f32 wa = w.k * w.amplitude;
        f32 qwa = w.Q * wa;
        dPdx.x -= qwa * cth * w.dir.x * w.dir.x;
        dPdx.z -= qwa * cth * w.dir.x * w.dir.z;
        dPdx.y += wa * cth * w.dir.x;
        dPdz.x -= qwa * cth * w.dir.z * w.dir.x;
        dPdz.z -= qwa * cth * w.dir.z * w.dir.z;
        dPdz.y += wa * cth * w.dir.z;
    }
    o.displacement = pos - vec3{x, 0, z};
    o.height = pos.y;
    o.normal = normalize(cross(dPdz, dPdx)); // right-handed X×Z → Y
    f32 J = dPdx.x * dPdz.z - dPdx.z * dPdz.x;
    o.foam = std::max(0.f, 0.8f - J); // folds when J < 0.8
    return o;
}

} // namespace aetherion::procgen
