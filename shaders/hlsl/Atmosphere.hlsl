// Hillaire 2020 LUTs + froxel fog. Coefficients MUST match AtmosphereProfile
// so the limb never seams against the volume.

static const float3 kBetaR = float3(5.802e-6, 13.558e-6, 33.100e-6);
static const float  kBetaM = 21.0e-6;
static const float  kHR    = 8000.0;
static const float  kHM    = 1200.0;
static const float  kG     = 0.8;

float rayleigh_phase(float mu) {
    return 3.0 / (16.0 * 3.14159265) * (1.0 + mu * mu);
}
float mie_phase(float mu, float g) {
    float g2 = g * g;
    return 3.0 / (8.0 * 3.14159265) * (1.0 - g2) * (1.0 + mu * mu)
         / ((2.0 + g2) * pow(abs(1.0 + g2 - 2.0 * g * mu), 1.5));
}

// Transmittance along a ray from radius r, mu = cos(view zenith).
float3 transmittance(float r, float mu, float Rt, float Rg) {
    // 2-point quadrature of β(h) ds. Full cooker uses 40 samples into a 256×64 LUT.
    float3 t = 0;
    const int N = 16;
    float dx = sqrt(max(0.0, Rt * Rt - r * r * (1 - mu * mu))) / N; // crude chord
    float s = 0;
    [loop] for (int i = 0; i < N; ++i) {
        float ri = sqrt(r * r + s * s + 2 * r * mu * s);
        float h  = ri - Rg;
        t += kBetaR * exp(-h / kHR) + kBetaM * exp(-h / kHM);
        s += dx;
    }
    return exp(-t * dx);
}

// Froxel slice: z = znear * (zfar/znear)^(s/S)
float froxel_z(float s, float S, float znear, float zfar) {
    return znear * pow(zfar / znear, s / S);
}

float fog_density(float h, float H, float sigma0, float biome_dust, float local) {
    return sigma0 * exp(-(h) / H) + biome_dust + local;
}

// Space↔ground sky LUT blend. k_atm from atmosphere_view_blend(h, h_atm).
float3 sky_mix(float3 L_space, float3 L_ground, float k_atm) {
    return lerp(L_space, L_ground, saturate(k_atm));
}
