// Filmic stack. Grain AFTER DoF AFTER TAA is load-bearing — it hides 1080p
// and virtual-texture pops. Do not reorder.

float3 taa_clamp(float3 history, float3 nbr_min, float3 nbr_max) {
    return clamp(history, nbr_min, nbr_max);
}

// Karis luma.
float luma(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }

// 180° cinematic shutter, 8 taps along velocity.
float3 motion_blur(float2 uv, float2 vel, Texture2D color, SamplerState s) {
    float3 acc = 0;
    [unroll] for (int i = 0; i < 8; ++i) {
        float t = (float(i) / 7.0) - 0.5;
        acc += color.Sample(s, uv + vel * t).rgb;
    }
    return acc / 8.0;
}

// ACES fitted (Narkowicz).
float3 aces(float3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

// 16 mm grain. Intensity 0.04–0.08, chroma 0.35.
float3 film_grain(float3 c, float2 uv, float frame, float intensity) {
    float n = frac(sin(dot(uv * (1 + frac(frame * 0.13)), float2(12.9898, 78.233))) * 43758.5453);
    float g = (n - 0.5) * intensity;
    float3 chroma = float3(g, g * 0.65, g * 0.45); // 16 mm dye
    return c + lerp(g.xxx, chroma, 0.35);
}

float3 chromatic_aberration(float3 c, float2 uv, Texture2D tex, SamplerState s) {
    float2 d = (uv - 0.5) * 0.0012; // 0.6 px at 1080p
    c.r = tex.Sample(s, uv + d).r;
    c.b = tex.Sample(s, uv - d).b;
    return c;
}

float vignette(float2 uv, float amt) {
    float2 d = uv * 2 - 1;
    return 1.0 - amt * dot(d, d);
}

// Anamorphic CoC. aspect 1.3.
float coc(float depth, float focus, float scale) {
    return saturate(abs(depth - focus) * scale);
}
