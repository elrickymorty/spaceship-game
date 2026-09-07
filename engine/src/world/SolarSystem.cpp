#include "aetherion/world/SolarSystem.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace aetherion::world {

static void set_name(char* d, const char* s) {
    std::size_t i = 0;
    for (; i < 31 && s[i]; ++i) d[i] = s[i];
    d[i] = 0;
}

CelestialBody make_sol_body(const char* name, BodyKind k,
                            f64 radius_eq, f64 radius_pol, f64 mass,
                            f64 sma_au, f64 period_days, f64 ecc, f64 inc_deg,
                            f64 tilt_deg, f64 rot_hours) {
    CelestialBody b;
    set_name(b.name, name);
    b.kind = k;
    b.ellipsoid.r_eq = radius_eq;
    b.ellipsoid.r_pol = radius_pol;
    b.genome.mass_kg = mass;
    b.genome.radius_m = radius_eq;
    b.genome.sma_au = sma_au;
    b.genome.eccentricity = ecc;
    b.genome.axial_tilt_rad = tilt_deg * kDeg;
    b.genome.rotation_s = rot_hours * 3600.0;
    b.genome.gas_giant = u8(k == BodyKind::GasGiant || k == BodyKind::IceGiant);
    b.mu = kG * mass;
    b.sma_m = sma_au * kAu;
    b.period_s = period_days * 86400.0;
    b.inc_rad = inc_deg * kDeg;
    b.atmo = math::scaled_atmosphere(k == BodyKind::Rocky ? 255.0 : 120.0,
                                     radius_eq, mass);
    return b;
}

dvec3 kepler_position(const CelestialBody& b, f64 t_s) {
    const f64 e = b.genome.eccentricity;
    const f64 n = kTwoPi / (b.period_s > 0 ? b.period_s : 1.0);
    f64 M = b.m0_rad + n * t_s;
    M = std::fmod(M + kPi, kTwoPi) - kPi;
    // Kepler, Newton 8 iterations.
    f64 E = M + e * std::sin(M);
    for (int i = 0; i < 8; ++i) {
        f64 f = E - e * std::sin(E) - M;
        f64 fp = 1.0 - e * std::cos(E);
        E -= f / fp;
    }
    const f64 x = b.sma_m * (std::cos(E) - e);
    const f64 y = b.sma_m * std::sqrt(1.0 - e * e) * std::sin(E);
    // Rotate by aop, inc, lan.
    const f64 cO = std::cos(b.lan_rad), sO = std::sin(b.lan_rad);
    const f64 ci = std::cos(b.inc_rad), si = std::sin(b.inc_rad);
    const f64 cw = std::cos(b.aop_rad), sw = std::sin(b.aop_rad);
    const f64 x1 = x * cw - y * sw;
    const f64 y1 = x * sw + y * cw;
    return {x1 * cO - y1 * ci * sO,
            y1 * si,
            x1 * sO + y1 * ci * cO};
}

static PlanetarySystem make_sol() {
    PlanetarySystem s;
    set_name(s.name, "Sol");
    s.seed = 0;
    set_name(s.star.name, "Sol");
    s.star.mass_kg = 1.98847e30;
    s.star.radius_m = 6.957e8;
    s.star.t_eff_k = 5772.f;
    s.galaxy_pos_pc = {0, 0, 0};

    auto add = [&](CelestialBody b) { s.bodies.push_back(b); };

    add(make_sol_body("Mercury", BodyKind::Rocky,
        2439.7e3, 2439.7e3, 3.3011e23,
        0.387098, 87.969, 0.205630, 7.005, 0.034, 1407.6));
    add(make_sol_body("Venus", BodyKind::Rocky,
        6051.8e3, 6051.8e3, 4.8675e24,
        0.723332, 224.701, 0.006772, 3.394, 177.36, -5832.5));
    CelestialBody earth = make_sol_body("Earth", BodyKind::Rocky,
        6378.137e3, 6356.752e3, 5.97237e24,
        1.000000, 365.256, 0.016708, 0.00005, 23.439, 23.934);
    earth.atmo = math::AtmosphereProfile{}; // Earth defaults
    earth.genome.t_eq_k = 288.f;
    earth.genome.volatile_inventory = 0.71f;
    earth.genome.tectonic = 0.55f;
    add(earth);
    add(make_sol_body("Mars", BodyKind::Rocky,
        3396.2e3, 3376.2e3, 6.4171e23,
        1.523679, 686.980, 0.0934, 1.850, 25.19, 24.622));
    CelestialBody jup = make_sol_body("Jupiter", BodyKind::GasGiant,
        71492e3, 66854e3, 1.8982e27,
        5.2044, 4332.59, 0.0489, 1.303, 3.13, 9.925);
    jup.has_rings = 1;
    jup.rings = {1.4f, 1.8f, 0.05f, {0.7f, 0.65f, 0.55f}};
    add(jup);
    CelestialBody sat = make_sol_body("Saturn", BodyKind::GasGiant,
        60268e3, 54364e3, 5.6834e26,
        9.5826, 10759.22, 0.0565, 2.485, 26.73, 10.656);
    sat.has_rings = 1;
    sat.rings = {1.2f, 2.3f, 0.7f, {0.90f, 0.82f, 0.65f}};
    add(sat);
    CelestialBody ura = make_sol_body("Uranus", BodyKind::IceGiant,
        25559e3, 24973e3, 8.6810e25,
        19.2184, 30688.5, 0.0463, 0.773, 97.77, -17.24);
    ura.has_rings = 1;
    ura.rings = {1.5f, 2.1f, 0.15f, {0.6f, 0.7f, 0.8f}};
    add(ura);
    add(make_sol_body("Neptune", BodyKind::IceGiant,
        24764e3, 24341e3, 1.02413e26,
        30.1104, 60182.0, 0.0095, 1.770, 28.32, 16.11));
    return s;
}

static f32 star_temp(u8 spectral) {
    const f32 t[] = {3300.f, 4300.f, 5772.f, 6800.f, 8500.f};
    return t[spectral < 5 ? spectral : 2];
}
static f64 star_mass(u8 spectral) {
    const f64 m[] = {0.3, 0.7, 1.0, 1.3, 2.0};
    return m[spectral < 5 ? spectral : 2] * 1.98847e30;
}

static PlanetarySystem make_exo(u16 index) {
    PlanetarySystem s;
    const u32 h = pcg(0xC0FFEE00u ^ u32(index) * 2654435761u);
    s.seed = (u64(h) << 32) ^ pcg(h);
    s.star.spectral = u8(h % 5);
    s.star.t_eff_k = star_temp(s.star.spectral);
    s.star.mass_kg = star_mass(s.star.spectral);
    s.star.radius_m = 6.957e8 * std::pow(s.star.mass_kg / 1.98847e30, 0.8);
    char nm[32];
    std::snprintf(nm, 32, "EXO-%03u", unsigned(index + 1));
    set_name(s.name, nm);
    set_name(s.star.name, nm);
    // Spiral-arm-ish distribution around Sol, 4–220 pc.
    f32 u = hash01(h, 1, 2);
    f32 v = hash01(h, 3, 4);
    f64 r = 4.0 + f64(u) * 216.0;
    f64 th = f64(v) * kTwoPi;
    f64 z = (f64(hash01(h, 5, 6)) - 0.5) * 40.0;
    s.galaxy_pos_pc = {r * std::cos(th), z, r * std::sin(th)};

    int nplanets = 3 + int(h % 5); // 3–7 per system. We need 152 worlds total.
    (void)nplanets;
    // The catalog builder assigns exactly 152 bodies across systems.
    return s;
}

Galaxy Galaxy::build() {
    Galaxy g;
    g.sol = make_sol();

    // 152 exoplanets packed into ~40 systems (3–5 bodies each).
    u16 remaining = 152;
    u16 sys_i = 0;
    u16 body_i = 0;
    while (remaining > 0) {
        PlanetarySystem sys = make_exo(sys_i);
        u32 h = pcg(0xA53u + sys_i * 17u);
        int n = 3 + int(h % 3); // 3–5
        if (n > remaining) n = remaining;
        for (int i = 0; i < n; ++i) {
            u32 bh = pcg(h + u32(i) * 101u);
            f32 kind_u = hash01(bh, 9, 1);
            BodyKind k = kind_u < 0.62f ? BodyKind::Rocky
                       : kind_u < 0.82f ? BodyKind::IceGiant
                                        : BodyKind::GasGiant;
            f64 r_earth = 0.5 + f64(hash01(bh, 2, 3)) * (k == BodyKind::Rocky ? 1.8 : 11.0);
            f64 radius = r_earth * 6371000.0;
            f64 mass = 5.972e24 * std::pow(r_earth, k == BodyKind::Rocky ? 3.3 : 1.3);
            f64 sma = 0.15 + f64(i) * (0.4 + f64(hash01(bh, 4, 5)) * 1.2)
                             * (sys.star.mass_kg / 1.98847e30);
            // habitable-zone-ish period from Kepler III
            f64 period_days = 365.256 * std::pow(sma, 1.5) / std::sqrt(sys.star.mass_kg / 1.98847e30);
            f64 ecc = f64(hash01(bh, 6, 7)) * 0.12;
            f64 inc = f64(hash01(bh, 8, 9)) * 6.0;
            f64 tilt = f64(hash01(bh, 10, 11)) * 40.0;
            f64 rot = 8.0 + f64(hash01(bh, 12, 13)) * 40.0;
            char bn[32];
            std::snprintf(bn, 32, "EXO-%03u-%c", unsigned(sys_i + 1), 'b' + i);
            CelestialBody b = make_sol_body(bn, k, radius, radius * 0.997, mass,
                                            sma, period_days, ecc, inc, tilt, rot);
            b.exo_index = body_i;
            b.genome.seed = (u64(bh) << 32) ^ pcg(bh ^ 0x51u);
            b.genome.t_eq_k = 50.f + hash01(bh, 14, 15) * 350.f;
            b.genome.volatile_inventory = hash01(bh, 16, 17);
            b.genome.tectonic = hash01(bh, 18, 19);
            b.genome.star_class = sys.star.spectral;
            b.genome.gas_giant = u8(k != BodyKind::Rocky);
            if (k != BodyKind::Rocky && hash01(bh, 20, 21) > 0.55f) {
                b.has_rings = 1;
                b.rings = {1.2f, 2.0f + hash01(bh, 22, 23), 0.2f + hash01(bh, 24, 25) * 0.6f,
                           {0.8f, 0.75f, 0.65f}};
            }
            // Graph selection from climate.
            if (b.genome.rotation_s > 1e6) std::strncpy(b.genome.graph_id, "tide_locked", 31);
            else if (b.genome.volatile_inventory < 0.2f) std::strncpy(b.genome.graph_id, "arid_super_earth", 31);
            else std::strncpy(b.genome.graph_id, "earth_class", 31);
            sys.bodies.push_back(b);
            ++body_i;
        }
        remaining = u16(remaining - n);
        g.exo.push_back(std::move(sys));
        ++sys_i;
    }
    return g;
}

const PlanetarySystem* Galaxy::find(const char* name) const {
    if (std::strcmp(sol.name, name) == 0) return &sol;
    for (const auto& s : exo) if (std::strcmp(s.name, name) == 0) return &s;
    return nullptr;
}
CelestialBody* Galaxy::body_by_name(const char* name) {
    for (auto& b : sol.bodies) if (std::strcmp(b.name, name) == 0) return &b;
    for (auto& s : exo)
        for (auto& b : s.bodies) if (std::strcmp(b.name, name) == 0) return &b;
    return nullptr;
}

} // namespace aetherion::world
