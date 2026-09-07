#pragma once
// Three nested SDF views: galaxy point cloud, Kepler orrery, topographic scan.
// Waypoints persist across ScaleGraph rings via a stable BodyId + geodetic.
#include "aetherion/world/SolarSystem.hpp"
#include "aetherion/world/ScaleGraph.hpp"
#include <vector>

namespace aetherion::ui {

enum class MapLayer : u8 { Galaxy, System, Surface };

struct Waypoint {
    char  label[48]{};
    u16   system_index = 0xFFFF; // 0xFFFF = Sol
    u16   body_index   = 0;
    f64   lat = 0, lon = 0, h = 0; // geodetic, surface layer
    dvec3 system_pos_au{};         // system layer
    dvec3 galaxy_pc{};             // galaxy layer
    u8    kind = 0;                // 0 fauna 1 resource 2 anomaly 3 custom
};

struct ScanOverlay {
    bool topography = true;
    bool fauna      = true;
    bool resources  = true;
    bool anomalies  = true;
    f32  contour_m  = 50.f;
};

struct MapCamera {
    MapLayer layer = MapLayer::System;
    dvec3    look{};
    f64      distance = 12; // units of the active layer
    f32      yaw = 0, pitch = 0.4f;
};

class HolographicMap {
public:
    MapCamera cam{};
    ScanOverlay scan{};
    std::vector<Waypoint> waypoints;

    void zoom_in();   // Galaxy → System → Surface
    void zoom_out();

    // Project a waypoint into the active layer's hologram space (unit cube).
    vec3 project(const Waypoint& w, const world::Galaxy& g,
                 const world::PlanetarySystem& current) const;

    void add_scan_hit(Waypoint w);
};

} // namespace aetherion::ui
