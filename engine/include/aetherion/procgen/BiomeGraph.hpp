#pragma once
// Deterministic node DAG. Same seed + genome + graph = same planet, any machine.
#include "aetherion/math/Types.hpp"
#include <string>
#include <vector>

namespace aetherion::procgen {

enum class NodeType : u16 {
    FBM = 0, RidgedMF, Worley, DomainWarp, Terrace, Billow,
    Temperature, Moisture, WindPrevailing, Whittaker,
    HydraulicErosion, ThermalErosion, RiverGraph, LakeFill, Coast,
    SlopeMask, AltitudeBand, Blend, POMHint,
    VegetationLayer, RockLayer, FaunaField, WindReactive, OceanSpectrum,
    Count
};

enum class BiomeId : u8 {
    SubtropicalDesert = 0, Savanna, TropicalSeasonal, Rainforest,
    TemperateDesert, Grassland, Woodland, TemperateForest,
    ColdDesert, Shrub, Boreal, BorealWet,
    Ice, Tundra, TundraWet, PolarBog,
    Ocean, Lake, Beach, Peak, Lava,
    Count
};

struct NodeParam {
    char  name[32]{};
    f32   value = 0;
};

struct GraphNode {
    char     id[32]{};
    NodeType type = NodeType::FBM;
    NodeParam params[12]{};
    u8       nparams = 0;
    char     inputs[6][32]{};
    u8       ninputs = 0;
};

struct PlanetGenome {
    u64 seed = 1;
    f64 mass_kg = 5.972e24;
    f64 radius_m = 6371000;
    f64 sma_au = 1;
    f64 eccentricity = 0.0167;
    f64 axial_tilt_rad = 0.4091;
    f64 rotation_s = 86164.0;
    f32 t_eq_k = 288.f;
    f32 volatile_inventory = 0.7f;   // 0 dry, 1 ocean world
    f32 tectonic = 0.5f;
    f32 wind_u = 12.f;               // m/s
    u8  star_class = 2;              // 0=M 1=K 2=G 3=F 4=A
    u8  has_rings = 0;
    u8  gas_giant = 0;
    char graph_id[32]{"earth_class"};
};

struct SampleCoord {
    f64 lat = 0;     // rad, -pi/2..pi/2
    f64 lon = 0;     // rad, -pi..pi
    f64 h   = 0;     // m, filled by height nodes
};

struct SampleResult {
    f32     height = 0;
    f32     temperature_c = 0;
    f32     moisture = 0;
    f32     slope = 0;
    BiomeId biome = BiomeId::Grassland;
    f32     river = 0;
    f32     pom_amp = 0;
    vec3    wind{};
};

class BiomeGraph {
public:
    std::vector<GraphNode> nodes;
    PlanetGenome genome{};

    static BiomeGraph earth_class();
    static BiomeGraph arid_super_earth();
    static BiomeGraph tide_locked();

    f32  param(const GraphNode& n, const char* name, f32 def) const;
    int  find(const char* id) const;

    SampleResult evaluate(SampleCoord c) const;

    // Macro map: equirectangular height + biome. Deterministic.
    void cook_macro(int w, int h, f32* height_out, u8* biome_out) const;

private:
    f32 eval_height(SampleCoord c) const;
    f32 fbm(dvec3 p, int oct, f32 lac, f32 gain, f32 scale, u32 seed_ofs) const;
    f32 ridged(dvec3 p, int oct, f32 gain, f32 scale, u32 seed_ofs) const;
    f32 worley(dvec3 p, f32 scale, f32 jitter) const;
};

BiomeId whittaker(f32 t_c, f32 moisture);
const char* biome_name(BiomeId id);

} // namespace aetherion::procgen
