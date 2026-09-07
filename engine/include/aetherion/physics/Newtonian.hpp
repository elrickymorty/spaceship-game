#pragma once
// Vessel: F = T + D + g(r). No arcade damping unless assist is on.
// On-foot: g n + 2 v × ω, jetpack Δv budget.
#include "aetherion/math/PlanetWarp.hpp"
#include "aetherion/world/ScaleGraph.hpp"
#include <cmath>

namespace aetherion::physics {

struct ForceTerms {
    dvec3 thrust{};
    dvec3 drag{};
    dvec3 gravity{};
    dvec3 aero{};
};

struct Vessel {
    dvec3 p_m{};          // planet-centric meters
    dvec3 v_ms{};         // m/s
    dquat q = dquat::identity();
    dvec3 w_rad{};        // body rates
    f64   mass_kg = 12000;
    f64   main_n  = 240000;   // N
    f64   rcs_n   = 8000;
    f64   CdA     = 12.0;     // m²
    bool  assist  = false;    // kill-rot / atmo-dampen
    f32   throttle = 0;
    vec3  rcs{};              // [-1,1] translation
    vec3  stick{};            // pitch yaw roll
};

inline dvec3 gravity_nbody(dvec3 p, f64 mu) {
    f64 r2 = dot(p, p);
    f64 r  = std::sqrt(r2);
    if (r < 1.0) return {};
    return p * (-mu / (r2 * r));
}

ForceTerms eval_forces(const Vessel& v, const math::AtmosphereProfile& atmo,
                       const math::Ellipsoid& e, f64 mu, dvec3 omega_planet);

void integrate_rk4(Vessel& v, const math::AtmosphereProfile& atmo,
                   const math::Ellipsoid& e, f64 mu, dvec3 omega, f64 dt);

struct Character {
    vec3 p{};
    vec3 v{};
    vec3 heading{0, 0, 1};
    f32  fuel = 1;          // jetpack 0..1
    f32  fuel_rate = 0.18f; // per second at full
    f32  jet_n = 900;       // N, ~1.2 g for 75 kg
    f32  mass = 75;
    f32  walk = 4.2f;
    f32  sprint = 7.6f;
    bool grounded = true;
    bool jet = false;
};

void tick_character(Character& c, vec3 wish, bool sprint, bool jump, bool jet,
                    vec3 ground_n, f32 g, f32 dt);

} // namespace aetherion::physics
