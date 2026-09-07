#pragma once
// True first-person: visible legs, IK hands, spring-damper head-bob, G-load sway.
#include "aetherion/physics/Newtonian.hpp"

namespace aetherion::gameplay {

struct HeadBob {
    f32 spring_k = 72.f;
    f32 damper   = 11.f;
    f32 amp_walk = 0.035f;
    f32 amp_run  = 0.055f;
    f32 freq_walk= 1.8f;
    f32 freq_run = 2.4f;
    f32 y = 0, v = 0, phase = 0;
};

struct BodyAwareness {
    vec3  left_hand{};      // local, cockpit or weapon
    vec3  right_hand{};
    vec3  left_foot{};
    vec3  right_foot{};
    vec3  head_offset{};    // bob + g-sway, applied to camera
    f32   g_load = 1;       // |a|/g
};

class FirstPerson {
public:
    HeadBob bob{};
    BodyAwareness body{};
    f32 yaw = 0, pitch = 0;
    f32 eye_height = 1.68f;
    bool in_cockpit = false;

    void look(f32 dx, f32 dy, f32 dt);
    void tick(const physics::Character& ch, vec3 accel_ms2, f32 dt);
    vec3 eye_local() const; // camera offset in character space
};

} // namespace aetherion::gameplay
