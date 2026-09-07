#include "aetherion/render/FrameGraph.hpp"
#include <algorithm>
#include <cstring>

namespace aetherion::render {

u16 FrameGraph::add_resource(const char* name, u16 w, u16 h, ResFormat fmt, u16 d) {
    FGResource r;
    std::strncpy(r.name, name, 31);
    r.w = w; r.h = h; r.d = d; r.format = fmt;
    r.bytes = u32(w) * u32(h) * u32(d) * bytes_per_pixel(fmt);
    // 256 B alignment.
    r.bytes = (r.bytes + 255u) & ~255u;
    resources.push_back(r);
    return u16(resources.size() - 1);
}
u16 FrameGraph::add_pass(const char* name, Queue q) {
    FGPass p;
    std::strncpy(p.name, name, 31);
    p.queue = q;
    p.async = (q == Queue::Compute || q == Queue::Copy);
    passes.push_back(p);
    return u16(passes.size() - 1);
}
void FrameGraph::reads(u16 pass, u16 res) {
    auto& p = passes[pass];
    if (p.nreads < 8) p.reads[p.nreads++] = res;
    auto& r = resources[res];
    r.last_use = std::max(r.last_use, pass);
}
void FrameGraph::writes(u16 pass, u16 res) {
    auto& p = passes[pass];
    if (p.nwrites < 8) p.writes[p.nwrites++] = res;
    auto& r = resources[res];
    if (r.created_by == 0xFFFF) r.created_by = pass;
    r.last_use = std::max(r.last_use, pass);
}

u32 FrameGraph::compile() {
    // Greedy alias: free a resource's range when last_use has passed.
    struct Span { u32 off, size; u16 until; };
    std::vector<Span> live;
    u32 peak = 0;
    alias_fails = 0;

    auto alloc = [&](u32 size, u16 until) -> u32 {
        // First-fit in the holes of `live`, else append.
        u32 cursor = 0;
        std::sort(live.begin(), live.end(), [](Span a, Span b) { return a.off < b.off; });
        for (auto& s : live) {
            if (s.off >= cursor + size) {
                live.push_back({cursor, size, until});
                return cursor;
            }
            cursor = std::max(cursor, s.off + s.size);
        }
        if (cursor + size > kTransientBytes) {
            ++alias_fails;
            return ~0u;
        }
        live.push_back({cursor, size, until});
        peak = std::max(peak, cursor + size);
        return cursor;
    };

    for (u16 pi = 0; pi < u16(passes.size()); ++pi) {
        live.erase(std::remove_if(live.begin(), live.end(),
                   [pi](Span s) { return s.until < pi; }), live.end());
        for (u8 w = 0; w < passes[pi].nwrites; ++w) {
            FGResource& r = resources[passes[pi].writes[w]];
            if (r.created_by != pi) continue;
            r.alias_offset = alloc(r.bytes, r.last_use);
        }
    }
    peak_bytes = peak;
    return peak;
}

} // namespace aetherion::render
