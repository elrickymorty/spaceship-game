#include "aetherion/gameplay/FirstPerson.hpp"
#include <algorithm>
#include <cmath>

namespace aetherion::gameplay {

void FirstPerson::look(f32 dx, f32 dy, f32 dt) {
    yaw   += dx * dt;
    pitch  = clamp(pitch + dy * dt, -1.4f, 1.4f);
}

void FirstPerson::tick(const physics::Character& ch, vec3 accel_ms2, f32 dt) {
    const f32 spd = length(vec3{ch.v.x, 0, ch.v.z});
    const bool run = spd > (ch.walk + ch.sprint) * 0.5f;
    const f32 freq = run ? bob.freq_run : bob.freq_walk;
    const f32 amp  = run ? bob.amp_run  : bob.amp_walk;
    if (ch.grounded && spd > 0.4f) bob.phase += freq * kPi * 2.f * f32(dt) * (spd / ch.walk);
    const f32 target = ch.grounded ? std::sin(bob.phase) * amp : 0.f;
    // Spring-damper toward the gait target.
    const f32 a = (target - bob.y) * bob.spring_k - bob.v * bob.damper;
    bob.v += a * dt;
    bob.y += bob.v * dt;

    body.g_load = length(accel_ms2) / 9.80665f;
    vec3 sway = accel_ms2 * (-0.012f); // head lags acceleration
    sway.y = clamp(sway.y, -0.08f, 0.08f);
    body.head_offset = {sway.x * 0.4f, bob.y + sway.y, sway.z * 0.3f};

    // Gait-driven foot and hand IK targets (local). Production binds these
    // to a 2-bone IK; the kernel only produces the targets.
    const f32 step = std::sin(bob.phase);
    const f32 step2= std::sin(bob.phase + 3.14159f);
    body.left_foot  = {-0.12f, -eye_height + std::max(0.f, -step) * 0.08f,  step * 0.18f};
    body.right_foot = { 0.12f, -eye_height + std::max(0.f, -step2)* 0.08f,  step2* 0.18f};
    if (in_cockpit) {
        body.left_hand  = {-0.22f, -0.18f, 0.42f}; // throttle
        body.right_hand = { 0.25f, -0.10f, 0.48f}; // stick
    } else {
        body.right_hand = {0.18f, -0.12f, 0.45f};  // weapon
        body.left_hand  = {-0.14f, -0.16f, 0.38f};
    }
}

vec3 FirstPerson::eye_local() const {
    return {body.head_offset.x, eye_height + body.head_offset.y, body.head_offset.z};
}

} // namespace aetherion::gameplay
