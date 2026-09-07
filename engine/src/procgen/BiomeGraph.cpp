#include "aetherion/procgen/BiomeGraph.hpp"
#include <cmath>
#include <cstring>

namespace aetherion::procgen {

static bool streq(const char* a, const char* b) { return std::strcmp(a, b) == 0; }

static GraphNode make_node(const char* id, NodeType t) {
    GraphNode n{};
    std::strncpy(n.id, id, 31);
    n.type = t;
    return n;
}
static void add_param(GraphNode& n, const char* name, f32 v) {
    if (n.nparams >= 12) return;
    std::strncpy(n.params[n.nparams].name, name, 31);
    n.params[n.nparams].value = v;
    ++n.nparams;
}
static void add_input(GraphNode& n, const char* id) {
    if (n.ninputs >= 6) return;
    std::strncpy(n.inputs[n.ninputs], id, 31);
    ++n.ninputs;
}

BiomeGraph BiomeGraph::earth_class() {
    BiomeGraph g;
    GraphNode continent = make_node("continent", NodeType::FBM);
    add_param(continent, "octaves", 8); add_param(continent, "lacunarity", 2.02f);
    add_param(continent, "gain", 0.5f); add_param(continent, "scale", 4e-5f);
    GraphNode mount = make_node("mount", NodeType::RidgedMF);
    add_param(mount, "octaves", 6); add_param(mount, "gain", 0.5f);
    add_param(mount, "scale", 8e-5f);
    GraphNode warp = make_node("warp", NodeType::DomainWarp);
    add_param(warp, "strength", 1200.f); add_param(warp, "octaves", 3);
    add_input(warp, "continent");
    GraphNode blend = make_node("macro_h", NodeType::Blend);
    add_param(blend, "k", 0.35f); add_input(blend, "warp"); add_input(blend, "mount");
    GraphNode eros = make_node("erosion", NodeType::HydraulicErosion);
    add_param(eros, "iterations", 32); add_input(eros, "macro_h");
    GraphNode temp = make_node("temp", NodeType::Temperature);
    add_param(temp, "k_lat", 1.2f); add_param(temp, "lapse", 6.5f);
    GraphNode moist = make_node("moist", NodeType::Moisture);
    GraphNode whit = make_node("whit", NodeType::Whittaker);
    add_input(whit, "temp"); add_input(whit, "moist");
    g.nodes = {continent, mount, warp, blend, eros, temp, moist, whit};
    return g;
}

BiomeGraph BiomeGraph::arid_super_earth() {
    BiomeGraph g = earth_class();
    g.genome.volatile_inventory = 0.12f;
    g.genome.tectonic = 0.8f;
    g.genome.t_eq_k = 310.f;
    std::strncpy(g.genome.graph_id, "arid_super_earth", 31);
    for (auto& n : g.nodes) if (n.type == NodeType::RidgedMF)
        for (u8 i = 0; i < n.nparams; ++i)
            if (streq(n.params[i].name, "gain")) n.params[i].value = 0.72f;
    return g;
}

BiomeGraph BiomeGraph::tide_locked() {
    BiomeGraph g = earth_class();
    std::strncpy(g.genome.graph_id, "tide_locked", 31);
    g.genome.rotation_s = 0; // tidally locked to star
    g.genome.axial_tilt_rad = 0;
    return g;
}

f32 BiomeGraph::param(const GraphNode& n, const char* name, f32 def) const {
    for (u8 i = 0; i < n.nparams; ++i)
        if (streq(n.params[i].name, name)) return n.params[i].value;
    return def;
}
int BiomeGraph::find(const char* id) const {
    for (int i = 0; i < int(nodes.size()); ++i)
        if (streq(nodes[std::size_t(i)].id, id)) return i;
    return -1;
}

// Value noise on a packed 3D lattice, quintic fade. Seeded.
f32 BiomeGraph::fbm(dvec3 p, int oct, f32 lac, f32 gain, f32 scale, u32 seed_ofs) const {
    p = p * f64(scale);
    f32 amp = 1, sum = 0, norm = 0;
    f32 freq = 1;
    const u32 s0 = u32(genome.seed) + seed_ofs;
    for (int o = 0; o < oct; ++o) {
        f32 x = f32(p.x) * freq, y = f32(p.y) * freq, z = f32(p.z) * freq;
        i32 ix = i32(std::floor(x)), iy = i32(std::floor(y)), iz = i32(std::floor(z));
        f32 fx = x - f32(ix), fy = y - f32(iy), fz = z - f32(iz);
        auto fade = [](f32 t) { return t * t * t * (t * (t * 6.f - 15.f) + 10.f); };
        f32 u = fade(fx), v = fade(fy), w = fade(fz);
        auto at = [&](i32 ax, i32 ay, i32 az) {
            return hash01(u32(ax) ^ s0, u32(ay) ^ (s0 * 3), u32(az) ^ (s0 * 7));
        };
        f32 n000 = at(ix, iy, iz), n100 = at(ix+1, iy, iz);
        f32 n010 = at(ix, iy+1, iz), n110 = at(ix+1, iy+1, iz);
        f32 n001 = at(ix, iy, iz+1), n101 = at(ix+1, iy, iz+1);
        f32 n011 = at(ix, iy+1, iz+1), n111 = at(ix+1, iy+1, iz+1);
        f32 nx00 = lerp(n000, n100, u), nx10 = lerp(n010, n110, u);
        f32 nx01 = lerp(n001, n101, u), nx11 = lerp(n011, n111, u);
        f32 nxy0 = lerp(nx00, nx10, v), nxy1 = lerp(nx01, nx11, v);
        f32 n = lerp(nxy0, nxy1, w) * 2.f - 1.f;
        sum += n * amp;
        norm += amp;
        amp *= gain;
        freq *= lac;
    }
    return sum / (norm > 0 ? norm : 1.f);
}

f32 BiomeGraph::ridged(dvec3 p, int oct, f32 gain, f32 scale, u32 seed_ofs) const {
    f32 s = 0, amp = 0.5f, freq = 1;
    for (int o = 0; o < oct; ++o) {
        f32 n = fbm(p, 1, 2.f, 0.5f, scale * freq, seed_ofs + u32(o) * 19);
        n = 1.f - std::fabs(n);
        n = n * n;
        s += n * amp;
        amp *= gain;
        freq *= 2.02f;
    }
    return s * 2.f - 1.f;
}

f32 BiomeGraph::worley(dvec3 p, f32 scale, f32 jitter) const {
    p = p * f64(scale);
    i32 ix = i32(std::floor(p.x)), iy = i32(std::floor(p.y)), iz = i32(std::floor(p.z));
    f32 dmin = 1e9f;
    const u32 s0 = u32(genome.seed) ^ 0xA341316Cu;
    for (i32 dz = -1; dz <= 1; ++dz)
    for (i32 dy = -1; dy <= 1; ++dy)
    for (i32 dx = -1; dx <= 1; ++dx) {
        f32 jx = hash01(u32(ix+dx)^s0, u32(iy+dy), u32(iz+dz));
        f32 jy = hash01(u32(iy+dy)^s0, u32(iz+dz), u32(ix+dx));
        f32 jz = hash01(u32(iz+dz)^s0, u32(ix+dx), u32(iy+dy));
        dvec3 cell{f64(ix+dx) + f64(jx * jitter),
                   f64(iy+dy) + f64(jy * jitter),
                   f64(iz+dz) + f64(jz * jitter)};
        f32 d = f32(length(p - cell));
        if (d < dmin) dmin = d;
    }
    return dmin;
}

f32 BiomeGraph::eval_height(SampleCoord c) const {
    // Unit sphere position from lat/lon — domain of all noise.
    dvec3 p{std::cos(c.lat) * std::cos(c.lon),
            std::sin(c.lat),
            std::cos(c.lat) * std::sin(c.lon)};
    p = p * genome.radius_m;

    f32 continent = 0, mount = 0;
    for (const auto& n : nodes) {
        if (n.type == NodeType::FBM)
            continent = fbm(p, int(param(n, "octaves", 8)), param(n, "lacunarity", 2.02f),
                            param(n, "gain", 0.5f), param(n, "scale", 4e-5f), 0);
        if (n.type == NodeType::RidgedMF)
            mount = ridged(p, int(param(n, "octaves", 6)), param(n, "gain", 0.5f),
                           param(n, "scale", 8e-5f), 11);
        if (n.type == NodeType::DomainWarp) {
            f32 s = param(n, "strength", 1200.f);
            f32 wx = fbm(p, 3, 2.f, 0.5f, 1.5e-5f, 3);
            f32 wz = fbm(p, 3, 2.f, 0.5f, 1.5e-5f, 7);
            p.x += f64(wx * s); p.z += f64(wz * s);
        }
    }
    f32 h = continent * 1400.f + mount * 2200.f * genome.tectonic;
    // Volatile inventory lifts the sea: more water → more of the hypsometry is drowned.
    h -= (genome.volatile_inventory - 0.5f) * 800.f;
    // Cheap hydraulic erosion stand-in: flatten by a low-frequency envelope.
    f32 erode = 1.f - 0.18f * std::fabs(continent);
    h *= erode;
    return h;
}

BiomeId whittaker(f32 t_c, f32 moisture) {
    moisture = saturate(moisture);
    int col = moisture < 0.15f ? 0 : moisture < 0.4f ? 1 : moisture < 0.7f ? 2 : 3;
    int row = t_c > 22.f ? 0 : t_c > 8.f ? 1 : t_c > -5.f ? 2 : 3;
    static const BiomeId table[4][4] = {
        {BiomeId::SubtropicalDesert, BiomeId::Savanna,  BiomeId::TropicalSeasonal, BiomeId::Rainforest},
        {BiomeId::TemperateDesert,   BiomeId::Grassland,BiomeId::Woodland,         BiomeId::TemperateForest},
        {BiomeId::ColdDesert,        BiomeId::Shrub,    BiomeId::Boreal,           BiomeId::BorealWet},
        {BiomeId::Ice,               BiomeId::Tundra,   BiomeId::TundraWet,        BiomeId::PolarBog}
    };
    return table[row][col];
}

SampleResult BiomeGraph::evaluate(SampleCoord c) const {
    SampleResult r;
    r.height = eval_height(c);

    const f32 k_lat = 1.2f;
    const f32 lapse = 6.5f;
    const f32 t_sea = genome.t_eq_k - 273.15f;
    r.temperature_c = t_sea * f32(std::pow(std::max(0.0, std::cos(c.lat)), f64(k_lat)))
                    - lapse * (r.height / 1000.f);

    // Tide-locked override: temperature from substellar longitude, not latitude.
    if (genome.rotation_s <= 0.0) {
        f32 day = f32(std::cos(c.lon)); // lon 0 = substellar
        r.temperature_c = (genome.t_eq_k - 273.15f) * day - lapse * (r.height / 1000.f);
    }

    // Orographic moisture: wet on the windward ocean-facing slope.
    r.wind = vec3{f32(std::cos(c.lat)), 0.f, f32(-std::sin(c.lat) * 0.2f)};
    f32 ocean = r.height < 0.f ? 1.f : saturate(1.f - r.height / 400.f);
    r.moisture = saturate(0.25f + 0.55f * ocean - 0.00025f * std::max(0.f, r.height)
                          + 0.15f * f32(std::cos(c.lat)));

    r.biome = whittaker(r.temperature_c, r.moisture);
    if (r.height < 0.f)            r.biome = BiomeId::Ocean;
    else if (r.height < 2.5f)      r.biome = BiomeId::Beach;
    const f32 snow_h = std::max(0.f, (r.temperature_c /* already includes lapse */) );
    // Peak: 0 °C isotherm already encoded in temperature_c. If T < 0 at this h → ice cap / peak.
    if (r.temperature_c < 0.f && r.height > 1800.f) r.biome = BiomeId::Peak;
    if (r.temperature_c < -18.f) r.biome = BiomeId::Ice;
    (void)snow_h;

    r.pom_amp = 0.04f + 0.08f * std::fabs(fbm(dvec3{c.lat * 100, c.lon * 100, 0}, 3, 2.f, 0.5f, 1.f, 99));
    r.slope = 0; // filled by finite differences at clipmap bake
    return r;
}

void BiomeGraph::cook_macro(int w, int h, f32* height_out, u8* biome_out) const {
    for (int y = 0; y < h; ++y) {
        const f64 lat = (0.5 - (f64(y) + 0.5) / f64(h)) * kPi;
        for (int x = 0; x < w; ++x) {
            const f64 lon = ((f64(x) + 0.5) / f64(w) - 0.5) * kTwoPi;
            SampleCoord c; c.lat = lat; c.lon = lon;
            SampleResult r = evaluate(c);
            const int i = y * w + x;
            height_out[i] = r.height;
            biome_out[i]  = u8(r.biome);
        }
    }
}

const char* biome_name(BiomeId id) {
    static const char* names[] = {
        "subtropical_desert","savanna","tropical_seasonal","rainforest",
        "temperate_desert","grassland","woodland","temperate_forest",
        "cold_desert","shrub","boreal","boreal_wet",
        "ice","tundra","tundra_wet","polar_bog",
        "ocean","lake","beach","peak","lava"
    };
    auto i = u8(id);
    return i < u8(BiomeId::Count) ? names[i] : "unknown";
}

} // namespace aetherion::procgen
