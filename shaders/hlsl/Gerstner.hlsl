// Six Gerstner components + 2 FFT ripple tiles on LOD0.
// ω² = g k tanh(k depth). Q_i = 1/(k_i A_i N). Foam = max(0, 0.8 - J).

struct Wave { float3 dir; float A, k, omega, phase, Q; };

void gerstner(in Wave w[6], int n, float2 xz, float t,
              out float3 pos, out float3 nrm, out float foam) {
    pos = float3(xz.x, 0, xz.y);
    float3 dPdx = float3(1, 0, 0);
    float3 dPdz = float3(0, 0, 1);
    [unroll] for (int i = 0; i < 6; ++i) {
        if (i >= n) break;
        float theta = w[i].k * dot(w[i].dir.xz, xz) - w[i].omega * t + w[i].phase;
        float s = sin(theta), c = cos(theta);
        float Qa = w[i].Q * w[i].A;
        pos.xz += w[i].dir.xz * Qa * c;
        pos.y  += w[i].A * s;
        float wa = w[i].k * w[i].A;
        float qwa = w[i].Q * wa;
        dPdx.x -= qwa * c * w[i].dir.x * w[i].dir.x;
        dPdx.z -= qwa * c * w[i].dir.x * w[i].dir.z;
        dPdx.y += wa  * c * w[i].dir.x;
        dPdz.x -= qwa * c * w[i].dir.z * w[i].dir.x;
        dPdz.z -= qwa * c * w[i].dir.z * w[i].dir.z;
        dPdz.y += wa  * c * w[i].dir.z;
    }
    nrm = normalize(cross(dPdz, dPdx));
    float J = dPdx.x * dPdz.z - dPdx.z * dPdz.x;
    foam = max(0.0, 0.8 - J);
}

// Vegetation wind — GPU vertex, no CPU.
float3 wind_displace(float3 p, float amp, float2 wind_dir, float t, uint id) {
    float phase = frac(sin(id * 91.345) * 47453.3) * 6.28318;
    float2 xz = amp * sin(dot(wind_dir, p.xz) * 0.35 - 1.7 * t + phase);
    float gust = amp * 0.4 * sin(t * 0.7 + phase * 2);
    return float3(xz.x + wind_dir.x * gust, 0, xz.y + wind_dir.y * gust);
}
