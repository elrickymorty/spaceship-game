#include "aetherion/physics/Newtonian.hpp"
#include <algorithm>
#include <cmath>

namespace aetherion::physics {

ForceTerms eval_forces(const Vessel& v, const math::AtmosphereProfile& atmo,
                       const math::Ellipsoid& e, f64 mu, dvec3 omega_planet) {
    ForceTerms f;
    f.gravity = gravity_nbody(v.p_m, mu) * v.mass_kg;

    dvec3 fwd = v.q.rotate(dvec3{0, 0, 1});
    f.thrust = fwd * (v.main_n * f64(v.throttle));
    f.thrust += v.q.rotate(dvec3{v.rcs.x, v.rcs.y, v.rcs.z}) * v.rcs_n;

    const f64 h = e.altitude(v.p_m);
    const f64 rho = math::air_density(atmo, h);
    const dvec3 v_rel = v.v_ms - cross(omega_planet, v.p_m); // atmosphere co-rotates
    const f64 speed = length(v_rel);
    if (speed > 1e-6 && rho > 1e-8) {
        dvec3 dir = v_rel / speed;
        f.aero = dir * (-0.5 * rho * speed * speed * v.CdA);
        if (v.assist) f.aero += v.v_ms * (-rho * 40.0); // extra damp, opt-in
    }
    f.drag = {}; // vacuum drag reserved for solar wind / mag-sail mods
    return f;
}

void integrate_rk4(Vessel& v, const math::AtmosphereProfile& atmo,
                   const math::Ellipsoid& e, f64 mu, dvec3 omega, f64 dt) {
    auto accel = [&](dvec3 p, dvec3 vel) {
        Vessel tmp = v;
        tmp.p_m = p;
        tmp.v_ms = vel;
        ForceTerms f = eval_forces(tmp, atmo, e, mu, omega);
        return (f.thrust + f.drag + f.gravity + f.aero) / v.mass_kg;
    };
    dvec3 p = v.p_m, vel = v.v_ms;
    dvec3 k1v = accel(p, vel);
    dvec3 k1p = vel;
    dvec3 k2v = accel(p + k1p * (dt * 0.5), vel + k1v * (dt * 0.5));
    dvec3 k2p = vel + k1v * (dt * 0.5);
    dvec3 k3v = accel(p + k2p * (dt * 0.5), vel + k2v * (dt * 0.5));
    dvec3 k3p = vel + k2v * (dt * 0.5);
    dvec3 k4v = accel(p + k3p * dt, vel + k3v * dt);
    dvec3 k4p = vel + k3v * dt;
    v.p_m  = p   + (k1p + k2p * 2.0 + k3p * 2.0 + k4p) * (dt / 6.0);
    v.v_ms = vel + (k1v + k2v * 2.0 + k3v * 2.0 + k4v) * (dt / 6.0);

    // Attitude: integrate body rates. Assist kill-rot damps w.
    if (v.assist) v.w_rad = v.w_rad * std::max(0.0, 1.0 - 4.0 * dt);
    v.w_rad += dvec3{v.stick.x, v.stick.y, v.stick.z} * (1.2 * dt);
    const f64 wlen = length(v.w_rad);
    if (wlen > 1e-8) {
        dquat dq = dquat::from_axis_angle(v.w_rad / wlen, wlen * dt);
        v.q = dq * v.q;
        f64 n = std::sqrt(v.q.w*v.q.w + v.q.x*v.q.x + v.q.y*v.q.y + v.q.z*v.q.z);
        v.q.w /= n; v.q.x /= n; v.q.y /= n; v.q.z /= n;
    }
}

void tick_character(Character& c, vec3 wish, bool sprint, bool jump, bool jet,
                    vec3 ground_n, f32 g, f32 dt) {
    const f32 speed = sprint ? c.sprint : c.walk;
    wish.y = 0;
    if (length(wish) > 1e-4f) wish = normalize(wish) * speed;

    if (c.grounded) {
        c.v.x = wish.x; c.v.z = wish.z;
        c.v.y = 0;
        if (jump) { c.v.y = 4.2f; c.grounded = false; }
    } else {
        c.v.y -= g * dt;
        c.v.x = lerp(c.v.x, wish.x, 1.f - std::exp(-2.f * dt));
        c.v.z = lerp(c.v.z, wish.z, 1.f - std::exp(-2.f * dt));
    }
    if (jet && c.fuel > 0.f) {
        c.v.y += (c.jet_n / c.mass) * dt;
        c.fuel = std::max(0.f, c.fuel - c.fuel_rate * dt);
        c.grounded = false;
    } else if (c.grounded) {
        c.fuel = std::min(1.f, c.fuel + 0.12f * dt);
    }
    c.p += c.v * dt;
    // Snap to ground plane (caller supplies a heightfield sample via p.y floor).
    (void)ground_n;
}

} // namespace aetherion::physics
