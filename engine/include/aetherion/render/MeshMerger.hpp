#pragma once
// Static mesh combining. Same material + static + same probe chart → one DAG.
// Target: < 400 instances, < 80 pipelines on a planet surface.
#include "aetherion/render/VirtualGeometry.hpp"

namespace aetherion::render {

struct MergeKey {
    u16 material_id = 0;
    u16 probe_chart = 0;
    u8  static_bit  = 1;
    constexpr bool operator==(MergeKey o) const {
        return material_id == o.material_id && probe_chart == o.probe_chart &&
               static_bit == o.static_bit;
    }
};

struct MergeKeyHash {
    std::size_t operator()(MergeKey k) const {
        return (std::size_t(k.material_id) << 16) ^ (std::size_t(k.probe_chart) << 1) ^ k.static_bit;
    }
};

struct MergeInput {
    MergeKey key{};
    const vec3* verts = nullptr;
    int nverts = 0;
    const u32* indices = nullptr;
    int nindices = 0;
    vec3 world_translation{}; // applied before concatenate (pre-transform)
};

// Concatenate inputs that share a key into one ClusterDAG each.
// Vegetation / rocks MUST NOT go through here — they are instanced.
int merge_static_meshes(const MergeInput* inputs, int n,
                        ClusterDAG* out_dags, MergeKey* out_keys, int max_out);

struct DrawBudget {
    int instances  = 0;
    int pipelines  = 0;
    bool within_envelope() const { return instances <= 400 && pipelines <= 80; }
};

DrawBudget estimate_budget(int unique_materials, int merged_static_batches,
                           int instanced_groups);

} // namespace aetherion::render
