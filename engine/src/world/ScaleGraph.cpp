#include "aetherion/world/ScaleGraph.hpp"
#include <cmath>

namespace aetherion::world {

ScaleGraph::ScaleGraph() {
    galaxy.ring = Ring::Galaxy;
    galaxy.meters_per_unit = kParsec;
    galaxy.parent = nullptr;

    system.ring = Ring::System;
    system.meters_per_unit = kAu;
    system.parent = &galaxy;

    orbital.ring = Ring::Orbital;
    orbital.meters_per_unit = 1000.0;
    orbital.parent = &system;

    surface.ring = Ring::Surface;
    surface.meters_per_unit = 1.0;
    surface.parent = &orbital;
}

void ScaleGraph::set_planet(math::Ellipsoid e, math::AtmosphereProfile a) {
    planet = e;
    atmo = a;
}

f64 ScaleGraph::camera_distance_to_planet() const {
    return length(camera_world_m); // callers keep camera_world_m planet-centric in orbital/surface
}

f64 ScaleGraph::altitude() const {
    return planet.altitude(camera_world_m);
}

Ring ScaleGraph::select_ring(f64 altitude_m) const {
    const f64 R = planet.r_eq;
    const f64 h_atm = atmo.h_atm;
    // Hysteresis: leaving a ring is harder than entering.
    const f64 leave = (last_ == Ring::Surface) ? 1.10 : 1.00;
    if (altitude_m < h_atm * 1.4 * leave) return Ring::Surface;
    if (altitude_m < 40.0 * R)            return Ring::Orbital;
    // Hill-sphere test is the caller's (system vs galaxy). Default: system.
    return Ring::System;
}

RebaseEvent ScaleGraph::tick(dvec3 camera_planet_relative_m, f64) {
    camera_world_m = camera_planet_relative_m;
    const f64 h = altitude();
    active = select_ring(h);
    last_ = active;

    RebaseEvent ev{};
    if (active != Ring::Surface) return ev;

    dvec3 cam_rel = camera_planet_relative_m - surface_origin_m_;
    const f64 dist = length(cam_rel);
    if (dist > 2048.0) {
        // Quantize to 512 m so we don't rebase every frame at the threshold.
        auto q = [](f64 v) { return std::floor(v / 512.0 + 0.5) * 512.0; };
        dvec3 new_origin{q(camera_planet_relative_m.x),
                         q(camera_planet_relative_m.y),
                         q(camera_planet_relative_m.z)};
        ev.delta_m   = new_origin - surface_origin_m_;
        ev.happened  = length(ev.delta_m) > 0.0;
        ev.ring      = Ring::Surface;
        surface_origin_m_ = new_origin;
        surface.translation = surface_origin_m_ / 1000.0; // parent is orbital (km)
    }
    return ev;
}

vec3 ScaleGraph::visual_position(dvec3 world_m) const {
    dvec3 rel = world_m - camera_world_m;
    auto w = math::warp_for_planet(planet.r_eq, int(active));
    dvec3 vis = w.forward(rel);
    return vec3(vis);
}

math::PlanetStageWeights ScaleGraph::stages(f64 screen_h_px, f64 fov_y_rad) const {
    const f64 r = camera_distance_to_planet();
    const f64 alpha_rad = math::angular_diameter(planet.r_eq, std::max(r, planet.r_eq + 1.0));
    const f64 alpha_deg = alpha_rad * (180.0 / kPi);
    const f64 alpha_px  = alpha_rad / fov_y_rad * screen_h_px;
    return math::evaluate_stages_ex(alpha_deg, alpha_px, altitude(), atmo.h_atm);
}

} // namespace aetherion::world
