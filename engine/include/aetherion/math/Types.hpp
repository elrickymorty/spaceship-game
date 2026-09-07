#pragma once
// Aetherion — scalar and vector types. Double is the world. Float is the GPU.
#include <cmath>
#include <cstdint>
#include <cstring>

namespace aetherion {

using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i32 = std::int32_t;
using i64 = std::int64_t;
using f32 = float;
using f64 = double;

constexpr f64 kPi      = 3.14159265358979323846;
constexpr f64 kTwoPi   = 6.28318530717958647692;
constexpr f64 kDeg     = kPi / 180.0;
constexpr f64 kAu      = 1.495978707e11;      // m
constexpr f64 kParsec  = 3.0856775814913673e16;
constexpr f64 kG       = 6.67430e-11;
constexpr f64 kSolMu   = 1.32712440018e20;    // m^3/s^2

template <typename T>
struct tvec3 {
    T x, y, z;
    constexpr tvec3() : x(0), y(0), z(0) {}
    constexpr tvec3(T s) : x(s), y(s), z(s) {}
    constexpr tvec3(T x_, T y_, T z_) : x(x_), y(y_), z(z_) {}
    template <typename U>
    explicit constexpr tvec3(tvec3<U> o) : x(T(o.x)), y(T(o.y)), z(T(o.z)) {}

    T& operator[](int i) { return (&x)[i]; }
    T  operator[](int i) const { return (&x)[i]; }

    constexpr tvec3 operator+(tvec3 o) const { return {x+o.x, y+o.y, z+o.z}; }
    constexpr tvec3 operator-(tvec3 o) const { return {x-o.x, y-o.y, z-o.z}; }
    constexpr tvec3 operator*(tvec3 o) const { return {x*o.x, y*o.y, z*o.z}; }
    constexpr tvec3 operator*(T s)     const { return {x*s, y*s, z*s}; }
    constexpr tvec3 operator/(T s)     const { return {x/s, y/s, z/s}; }
    constexpr tvec3 operator-()        const { return {-x, -y, -z}; }
    tvec3& operator+=(tvec3 o) { x+=o.x; y+=o.y; z+=o.z; return *this; }
    tvec3& operator-=(tvec3 o) { x-=o.x; y-=o.y; z-=o.z; return *this; }
    tvec3& operator*=(T s)     { x*=s; y*=s; z*=s; return *this; }
};

template <typename T>
constexpr tvec3<T> operator*(T s, tvec3<T> v) { return v * s; }

using vec3  = tvec3<f32>;
using dvec3 = tvec3<f64>;
using vec2  = tvec3<f32>; // z unused; keeps headers thin. Prefer .x/.y.

template <typename T> constexpr T dot(tvec3<T> a, tvec3<T> b) {
    return a.x*b.x + a.y*b.y + a.z*b.z;
}
template <typename T> constexpr tvec3<T> cross(tvec3<T> a, tvec3<T> b) {
    return {a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x};
}
template <typename T> T length(tvec3<T> v) { return std::sqrt(dot(v, v)); }
template <typename T> tvec3<T> normalize(tvec3<T> v) {
    T l = length(v);
    return l > T(0) ? v / l : tvec3<T>{};
}
template <typename T> constexpr T lerp(T a, T b, T t) { return a + (b - a) * t; }
template <typename T> constexpr tvec3<T> lerp(tvec3<T> a, tvec3<T> b, T t) {
    return a + (b - a) * t;
}
template <typename T> constexpr T clamp(T v, T lo, T hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
template <typename T> constexpr T saturate(T v) { return clamp(v, T(0), T(1)); }
inline f32  smoothstep(f32 a, f32 b, f32 x) {
    f32 t = saturate((x - a) / (b - a));
    return t * t * (3.f - 2.f * t);
}
inline f64  smoothstep(f64 a, f64 b, f64 x) {
    f64 t = saturate((x - a) / (b - a));
    return t * t * (3.0 - 2.0 * t);
}

struct dquat {
    f64 w, x, y, z;
    static dquat identity() { return {1, 0, 0, 0}; }
    static dquat from_axis_angle(dvec3 axis, f64 rad) {
        axis = normalize(axis);
        f64 s = std::sin(rad * 0.5), c = std::cos(rad * 0.5);
        return {c, axis.x * s, axis.y * s, axis.z * s};
    }
    dvec3 rotate(dvec3 v) const {
        dvec3 qv{x, y, z};
        dvec3 t = cross(qv, v) * 2.0;
        return v + t * w + cross(qv, t);
    }
    dquat conjugate() const { return {w, -x, -y, -z}; }
};

inline dquat operator*(dquat a, dquat b) {
    return {a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z,
            a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
            a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x,
            a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w};
}

struct mat4 {
    f32 m[16]; // column-major
    static mat4 identity() {
        mat4 r{};
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.f;
        return r;
    }
};

// Integer hash — the only RNG the biome graph is allowed to use.
inline u32 pcg(u32 x) {
    x = x * 747796405u + 2891336453u;
    u32 w = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
    return (w >> 22u) ^ w;
}
inline u32 pcg3d(u32 x, u32 y, u32 z) {
    return pcg(x + pcg(y + pcg(z)));
}
inline f32 hash01(u32 x, u32 y, u32 z) {
    return f32(pcg3d(x, y, z)) * (1.f / 4294967295.f);
}

} // namespace aetherion
