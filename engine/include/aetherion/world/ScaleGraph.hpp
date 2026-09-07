#pragma once
// Nested coordinate rings + floating origin. The camera never lives in a
// single float frame that spans a galaxy and a fingerprint.
#include "aetherion/math/PlanetWarp.hpp"

namespace aetherion::world {

enum class Ring : int { Galaxy = 0, System = 1, Orbital = 2, Surface = 3 };

struct ScaleAnchor {
    Ring   ring = Ring::System;
    f64    meters_per_unit = kAu;
    dvec3  translation{};          // in parent units
    dquat  rotation = dquat::identity();
    ScaleAnchor* parent = nullptr;
};

struct RebaseEvent {
    Ring  ring = Ring::Surface;
    dvec3 delta_m{};               // subtracted from all resident actors
    bool  happened = false;
};

class ScaleGraph {
public:
    ScaleAnchor galaxy, system, orbital, surface;
    math::Ellipsoid         planet{};
    math::AtmosphereProfile atmo{};
    dvec3 camera_world_m{};        // always meters from galaxy origin (double)
    Ring  active = Ring::System;

    ScaleGraph();

    // Choose the finest ring that keeps camera-relative positions inside
    // [-2^14, 2^14] in float. Hysteresis 10 %.
    Ring select_ring(f64 altitude_m) const;

    // Rebase surface floating origin if |cam| > 2048 m. Returns the delta
    // the gameplay layer must apply to actors.
    RebaseEvent tick(dvec3 camera_planet_relative_m, f64 dt);

    // Camera-relative position in the active ring, already warped for the GPU.
    vec3 visual_position(dvec3 world_m) const;

    math::PlanetStageWeights stages(f64 screen_h_px, f64 fov_y_rad) const;

    f64 altitude() const;
    f64 camera_distance_to_planet() const;

    void set_planet(math::Ellipsoid e, math::AtmosphereProfile a);

private:
    dvec3 surface_origin_m_{};     // floating origin, meters from planet center
    Ring  last_ = Ring::System;
};

// Transform orbital velocity (km/s) into surface m/s at the rebase, conserving
// momentum. ω is planet spin rad/s in world.
inline vec3 orbital_to_surface_velocity(dvec3 v_orbital_kms, dvec3 r_m,
                                        dvec3 omega, dquat tangent_to_world) {
    dvec3 v_ms = v_orbital_kms * 1000.0 - cross(omega, r_m);
    dvec3 local = tangent_to_world.conjugate().rotate(v_ms);
    return vec3(local);
}

} // namespace aetherion::world
