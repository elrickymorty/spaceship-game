#pragma once
// Finite-sky mapping, cube-sphere, log-depth, spherical clipmaps.
// Source of truth: docs/SPACE_TO_PLANET_TRANSITION.md
#include "aetherion/math/Types.hpp"

namespace aetherion::math {

// --- Logarithmic reverse-Z ------------------------------------------------
inline f32 log_depth(f32 w, f32 far_plane, f32 kappa) {
    // d = log2(κ w + 1) / log2(κ far + 1)   written as reverse-Z (closer = 1)
    const f32 num = std::log2(kappa * w + 1.f);
    const f32 den = std::log2(kappa * far_plane + 1.f);
    return 1.f - num / den;
}

inline f32 log_depth_kappa_for_ring(int ring) {
    // Surface (3): κ=1. Orbital (2): 1e-5. System/Galaxy: 1e-8.
    switch (ring) {
        case 3: return 1.f;
        case 2: return 1e-5f;
        default: return 1e-8f;
    }
}

// --- Visual warp: P_vis = P / (1 + |P|/R_w)  so the GPU never sees 1e7 units.
struct VisualWarp {
    f64 radius = 1.0e6; // R_w, meters in the current ring's unit *converted to m*

    dvec3 forward(dvec3 p) const {
        const f64 rho = length(p);
        return p * (1.0 / (1.0 + rho / radius));
    }
    dvec3 inverse(dvec3 p_vis) const {
        const f64 rho = length(p_vis);
        const f64 denom = 1.0 - rho / radius;
        return p_vis / (denom > 1e-12 ? denom : 1e-12);
    }
    // Jacobian scale used to keep tessellation error ≤ 0.3°.
    f64 scale_at(dvec3 p) const {
        const f64 rho = length(p);
        const f64 s = 1.0 / (1.0 + rho / radius);
        return s * s; // dP_vis/dP along radial
    }
};

inline VisualWarp warp_for_planet(f64 planet_radius_m, int ring) {
    VisualWarp w;
    if (ring >= 3)      w.radius = 1.0e6;
    else if (ring == 2) w.radius = 2.0 * planet_radius_m;
    else                w.radius = 1.0e12;
    return w;
}

// --- Cube-sphere (C1, Nowell 2008 / GPU Gems 3 ch. 1 variant) --------------
inline dvec3 cube_to_sphere(dvec3 p) {
    // p on cube face, components in [-1,1], |p|_inf = 1
    const f64 x2 = p.x * p.x, y2 = p.y * p.y, z2 = p.z * p.z;
    return {
        p.x * std::sqrt(1.0 - y2 * 0.5 - z2 * 0.5 + y2 * z2 / 3.0),
        p.y * std::sqrt(1.0 - z2 * 0.5 - x2 * 0.5 + z2 * x2 / 3.0),
        p.z * std::sqrt(1.0 - x2 * 0.5 - y2 * 0.5 + x2 * y2 / 3.0)
    };
}

struct Ellipsoid {
    f64 r_eq  = 6371000.0;
    f64 r_pol = 6356752.0;
    f64 flattening() const { return 1.0 - r_pol / r_eq; }
    dvec3 from_unit(dvec3 u) const {
        return {u.x * r_eq, u.y * r_eq, u.z * r_pol};
    }
    // Geodetic outward unit (not the parametric normal).
    dvec3 geodetic_normal(dvec3 p) const {
        return normalize(dvec3{p.x / (r_eq * r_eq),
                               p.y / (r_eq * r_eq),
                               p.z / (r_pol * r_pol)});
    }
    f64 radius_along(dvec3 dir) const {
        dvec3 u = normalize(dir);
        // r(θ) of ellipsoid
        const f64 c = u.z; // sin geocentric lat
        const f64 s2 = 1.0 - c * c;
        const f64 r2 = (r_eq * r_eq * r_pol * r_pol) /
                       (r_pol * r_pol * s2 + r_eq * r_eq * c * c);
        return std::sqrt(r2);
    }
    f64 altitude(dvec3 p) const {
        return length(p) - radius_along(p);
    }
};

// Tessellation patches-per-face from apparent angular diameter (degrees).
inline int ellipsoid_tessellation(f64 angular_diameter_deg) {
    int t = int(angular_diameter_deg * 24.0 + 0.5);
    return t < 8 ? 8 : (t > 256 ? 256 : t);
}

// Apparent angular diameter α = 2 arcsin(R / r) in radians. r = |camera-center|.
inline f64 angular_diameter(f64 radius, f64 distance) {
    const f64 x = clamp(radius / distance, 0.0, 1.0);
    return 2.0 * std::asin(x);
}

// --- Stage weights (optical, not geometric) --------------------------------
struct PlanetStageWeights {
    f32 impostor  = 1; // A
    f32 ellipsoid = 0; // B
    f32 clipmap   = 0; // C/D
    f32 surface   = 0; // E  (floating origin live)
    f32 atmosphere= 0; // aerial-perspective volume
};

inline PlanetStageWeights evaluate_stages_ex(f64 alpha_deg, f64 alpha_px,
                                             f64 h, f64 h_atm) {
    PlanetStageWeights w;
    w.ellipsoid = f32(smoothstep(1.5, 3.0, alpha_px));
    w.impostor  = 1.f - w.ellipsoid;
    const f32 clip_ang = f32(smoothstep(4.0, 6.0, alpha_deg));
    const f32 clip_alt = f32(smoothstep(h_atm * 10.0, h_atm * 1.4, h));
    w.clipmap = clip_ang * clip_alt;
    w.surface = f32(smoothstep(12000.0, 8000.0, h));
    w.atmosphere = f32(saturate((h_atm * 1.2 - h) / (h_atm * 0.4)));
    return w;
}

// α_px = angular diameter in pixels. h = altitude m. h_atm = atmosphere top m.
inline PlanetStageWeights evaluate_stages(f64 alpha_px, f64 h, f64 h_atm) {
    // Assume 1080p / 70° vertical FOV when the caller only has pixels.
    const f64 alpha_deg = alpha_px * (70.0 / 1080.0);
    return evaluate_stages_ex(alpha_deg, alpha_px, h, h_atm);
}

// --- Spherical clipmap ring ------------------------------------------------
struct ClipmapRing {
    int    index      = 0;     // 0 = finest
    f64    radius_m   = 48.0;  // coverage of this ring
    int    resolution = 255;   // verts per side (odd)
    dvec3  snapped_origin{};   // quantized tangent-space origin (world)
};

inline f64 clipmap_radius(int ring) {
    // r_i = 48 * 4^i meters
    f64 r = 48.0;
    for (int i = 0; i < ring; ++i) r *= 4.0;
    return r;
}

inline dvec3 snap_origin(dvec3 origin, f64 radius, int res) {
    const f64 cell = radius / f64(res / 2);
    auto snap = [cell](f64 v) { return std::floor(v / cell + 0.5) * cell; };
    return {snap(origin.x), snap(origin.y), snap(origin.z)};
}

// Project a tangent-plane vertex onto the ellipsoid and displace by height.
inline dvec3 clipmap_vertex(const Ellipsoid& e, dvec3 planet_center,
                            dquat tangent_to_world, dvec3 origin_world,
                            f64 u, f64 v, f64 radius, f64 height) {
    dvec3 local{u * radius, 0.0, v * radius};
    dvec3 world = origin_world + tangent_to_world.rotate(local);
    dvec3 rel   = world - planet_center;
    dvec3 n     = e.geodetic_normal(rel);
    dvec3 on_ellip = planet_center + e.from_unit(normalize(dvec3{
        rel.x / e.r_eq, rel.y / e.r_eq, rel.z / e.r_pol}));
    return on_ellip + n * height;
}

// Horizon-skirt blend (stage E). ρ_tangent = distance on the tangent plane.
inline f32 skirt_blend(f64 rho_tangent, f64 ring4_radius) {
    return f32(smoothstep(ring4_radius - 512.0, ring4_radius - 64.0, rho_tangent));
}

// --- Atmosphere density (shared by fog AND aero) ---------------------------
struct AtmosphereProfile {
    f64 h_atm   = 100000.0;           // m, density ~ 1e-4 of sea level
    f64 H_rayleigh = 8000.0;
    f64 H_mie      = 1200.0;
    dvec3 beta_rayleigh{5.802e-6, 13.558e-6, 33.100e-6};
    f64 beta_mie = 21.0e-6;
    f64 mie_g    = 0.8;
    f64 rho0     = 1.225;             // kg/m^3 sea level
    f64 scale_height_air = 8500.0;
};

inline f64 air_density(const AtmosphereProfile& a, f64 h) {
    if (h < 0.0) h = 0.0;
    return a.rho0 * std::exp(-h / a.scale_height_air);
}

// Scale an Earth-class profile to an exoplanet genome.
inline AtmosphereProfile scaled_atmosphere(f64 t_air_k, f64 radius_m,
                                           f64 mass_kg, f64 g0_earth = 9.80665) {
    AtmosphereProfile a;
    const f64 g = kG * mass_kg / (radius_m * radius_m);
    const f64 lapse_scale = (g / g0_earth);
    a.H_rayleigh *= (t_air_k / 288.0) / (lapse_scale > 1e-6 ? lapse_scale : 1e-6);
    a.H_mie      *= a.H_rayleigh / 8000.0;
    a.scale_height_air = a.H_rayleigh;
    a.h_atm = a.H_rayleigh * 12.5;
    return a;
}

// Sky LUT blend (space view ↔ ground view).
inline f32 atmosphere_view_blend(f64 h, f64 h_atm) {
    return f32(saturate((h_atm * 1.2 - h) / (h_atm * 0.4)));
}

} // namespace aetherion::math
