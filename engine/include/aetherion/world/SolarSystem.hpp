#pragma once
// Eight IAU-faithful bodies + 152 seed-locked exoplanets.
#include "aetherion/procgen/BiomeGraph.hpp"
#include "aetherion/math/PlanetWarp.hpp"
#include <vector>

namespace aetherion::world {

enum class BodyKind : u8 { Star, Rocky, IceGiant, GasGiant, Dwarf };

struct RingSystem {
    f32 inner_r_body = 1.2f;  // in body radii
    f32 outer_r_body = 2.3f;
    f32 optical_depth = 0.6f;
    f32 rgb[3]{0.85f, 0.78f, 0.62f};
};

struct CelestialBody {
    char name[32]{};
    BodyKind kind = BodyKind::Rocky;
    procgen::PlanetGenome genome{};
    math::Ellipsoid ellipsoid{};
    math::AtmosphereProfile atmo{};
    f64 mu = 0;                 // GM, m^3/s^2
    f64 sma_m = 0;
    f64 period_s = 0;
    f64 inc_rad = 0;
    f64 lan_rad = 0;            // longitude of ascending node
    f64 aop_rad = 0;            // argument of periapsis
    f64 m0_rad = 0;             // mean anomaly at epoch
    u8  has_rings = 0;
    RingSystem rings{};
    u16 exo_index = 0xFFFF;     // 0xFFFF = Sol
};

struct Star {
    char name[32]{};
    f64  mass_kg = 1.98847e30;
    f64  radius_m = 6.957e8;
    f32  t_eff_k = 5772.f;
    f32  rgb[3]{1.f, 0.95f, 0.90f};
    u8   spectral = 2; // G
};

struct PlanetarySystem {
    char name[48]{};
    u64  seed = 0;
    Star star{};
    dvec3 galaxy_pos_pc{};      // parsecs from Sol
    std::vector<CelestialBody> bodies;
};

// Kepler: solve M = E - e sin E, return position in the orbital plane.
dvec3 kepler_position(const CelestialBody& b, f64 t_s);

class Galaxy {
public:
    PlanetarySystem sol;
    std::vector<PlanetarySystem> exo; // 152

    static Galaxy build();
    const PlanetarySystem* find(const char* name) const;
    CelestialBody* body_by_name(const char* name);
};

CelestialBody make_sol_body(const char* name, BodyKind k,
                            f64 radius_eq, f64 radius_pol, f64 mass,
                            f64 sma_au, f64 period_days, f64 ecc, f64 inc_deg,
                            f64 tilt_deg, f64 rot_hours);

} // namespace aetherion::world
