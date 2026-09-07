#include "aetherion/render/MeshMerger.hpp"
#include <unordered_map>
#include <vector>

namespace aetherion::render {

int merge_static_meshes(const MergeInput* inputs, int n,
                        ClusterDAG* out_dags, MergeKey* out_keys, int max_out) {
    struct Acc {
        std::vector<vec3> verts;
        std::vector<u32>  idx;
        MergeKey key{};
    };
    std::unordered_map<MergeKey, Acc, MergeKeyHash> bins;
    bins.reserve(std::size_t(n));

    for (int i = 0; i < n; ++i) {
        const MergeInput& in = inputs[i];
        if (!in.key.static_bit) continue; // dynamic: never merge
        Acc& a = bins[in.key];
        a.key = in.key;
        const u32 base = u32(a.verts.size());
        a.verts.reserve(a.verts.size() + std::size_t(in.nverts));
        for (int v = 0; v < in.nverts; ++v)
            a.verts.push_back(in.verts[v] + in.world_translation);
        a.idx.reserve(a.idx.size() + std::size_t(in.nindices));
        for (int k = 0; k < in.nindices; ++k)
            a.idx.push_back(in.indices[k] + base);
    }

    int written = 0;
    for (auto& [key, acc] : bins) {
        if (written >= max_out) break;
        if (acc.verts.empty()) continue;
        out_dags[written] = build_dag_from_triangles(acc.verts.data(), int(acc.verts.size()),
                                                     acc.idx.data(), int(acc.idx.size()),
                                                     key.material_id);
        out_keys[written] = key;
        ++written;
    }
    return written;
}

DrawBudget estimate_budget(int unique_materials, int merged_static_batches,
                           int instanced_groups) {
    DrawBudget b;
    b.pipelines = unique_materials;          // 4 uber-shaders × permutations ≤ 80
    b.instances = merged_static_batches + instanced_groups;
    return b;
}

} // namespace aetherion::render
