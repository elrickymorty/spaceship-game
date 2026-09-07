#include "aetherion/ui/HolographicMap.hpp"
#include <cmath>

namespace aetherion::ui {

void HolographicMap::zoom_in() {
    if (cam.layer == MapLayer::Galaxy)  { cam.layer = MapLayer::System;  cam.distance = 12; }
    else if (cam.layer == MapLayer::System) { cam.layer = MapLayer::Surface; cam.distance = 1; }
}
void HolographicMap::zoom_out() {
    if (cam.layer == MapLayer::Surface) { cam.layer = MapLayer::System;  cam.distance = 12; }
    else if (cam.layer == MapLayer::System) { cam.layer = MapLayer::Galaxy; cam.distance = 80; }
}

static vec3 nrm_cube(dvec3 p, f64 s) {
    if (s < 1e-12) s = 1;
    dvec3 q = p / s;
    return vec3{f32(clamp(q.x, -1.0, 1.0)), f32(clamp(q.y, -1.0, 1.0)), f32(clamp(q.z, -1.0, 1.0))};
}

vec3 HolographicMap::project(const Waypoint& w, const world::Galaxy& g,
                             const world::PlanetarySystem& current) const {
    switch (cam.layer) {
        case MapLayer::Galaxy: {
            dvec3 p = w.galaxy_pc;
            if (w.system_index == 0xFFFF) p = g.sol.galaxy_pos_pc;
            else if (w.system_index < g.exo.size()) p = g.exo[w.system_index].galaxy_pos_pc;
            return nrm_cube(p, cam.distance);
        }
        case MapLayer::System:
            return nrm_cube(w.system_pos_au, cam.distance);
        case MapLayer::Surface: {
            // Equirect → unit sphere, then topographic extrusion is a shader concern.
            f32 cl = f32(std::cos(w.lat)), sl = f32(std::sin(w.lat));
            f32 co = f32(std::cos(w.lon)), so = f32(std::sin(w.lon));
            return {cl * co, sl, cl * so};
        }
    }
    (void)current;
    return {};
}

void HolographicMap::add_scan_hit(Waypoint w) { waypoints.push_back(w); }

} // namespace aetherion::ui
