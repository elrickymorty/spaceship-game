#pragma once
// Gerstner spectrum + foam Jacobian. Shared by the water uber-shader and the
// CPU physics (buoyancy samples the same function).
#include "aetherion/math/Types.hpp"
#include <cmath>

namespace aetherion::procgen {

struct GerstnerWave {
    vec3  dir{1, 0, 0};   // xz
    f32   amplitude = 0.4f;
    f32   k = 0.25f;      // rad/m
    f32   omega = 1.6f;   // rad/s
    f32   phase = 0;
    f32   Q = 0.4f;       // steepness
};

struct OceanSpectrum {
    GerstnerWave waves[6];
    int   count = 6;
    f32   depth = 4000.f;
    f32   wind_u = 12.f;
    u64   seed = 1;
};

OceanSpectrum spectrum_from_genome(f32 wind_u, f32 depth, u64 seed);

// Deep-water dispersion ω² = g k. Q_i = 1 / (k_i A_i N) to prevent loops.
inline f32 dispersion(f32 k, f32 depth, f32 g = 9.80665f) {
    return std::sqrt(g * k * std::tanh(k * depth));
}

struct OceanSample {
    vec3 displacement{}; // xyz, Gerstner
    vec3 normal{0, 1, 0};
    f32  height = 0;
    f32  foam = 0;       // max(0, J - 0.8)
};

OceanSample sample_ocean(const OceanSpectrum& s, f32 x, f32 z, f32 t);

} // namespace aetherion::procgen
