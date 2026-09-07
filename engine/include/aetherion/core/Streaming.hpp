#pragma once
// Virtual texture + meshlet page residency. Working set ≤ 2.8 GB.
// Missing pages MUST display the parent mip — the frame never stalls.
#include "aetherion/math/Types.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace aetherion::core {

enum class PageKind : u8 { TextureTile, MeshletCluster, HeightClipmap, ProbeBrick };

struct PageKey {
    u64  asset : 40;
    u32  lod   : 8;
    u32  x     : 8;
    u32  y     : 8;
    constexpr bool operator==(PageKey o) const {
        return asset == o.asset && lod == o.lod && x == o.x && y == o.y;
    }
};

struct PageKeyHash {
    std::size_t operator()(PageKey k) const {
        u64 v = (u64(k.asset) << 24) ^ (u64(k.lod) << 16) ^ (u64(k.x) << 8) ^ k.y;
        v ^= v >> 33; v *= 0xff51afd7ed558ccdULL;
        v ^= v >> 33; v *= 0xc4ceb9fe1a85ec53ULL;
        v ^= v >> 33;
        return std::size_t(v);
    }
};

struct PageSlot {
    PageKey key{};
    PageKind kind = PageKind::TextureTile;
    u32  gpu_handle = ~0u;
    u32  bytes      = 0;
    u32  last_frame = 0;
    u8   mip_parent = 0;     // always-resident ancestor lod
    bool ready      = false;
    bool pinned     = false; // clipmap 0-2, body, weapon, interior
};

// Feedback from last frame's visbuffer: which VT tiles were sampled.
struct FeedbackSample {
    PageKey key{};
    f32     desired_lod = 0; // fractional
};

class ResidencyManager {
public:
    static constexpr u64 kBudgetBytes     = 2800ull * 1024ull * 1024ull;
    static constexpr u32 kMaxPages        = 65536;
    static constexpr u32 kUnpinAfterFrames = 45;
    static constexpr f32 kFrustumDilate    = 1.4f;

    ResidencyManager();

    // Called on the render thread with last frame's GPU feedback.
    void ingest_feedback(const FeedbackSample* samples, int count, u32 frame);

    // Promote requested pages, demote LRU outside the dilated frustum.
    // Returns DMA records for the copy queue. Never blocks on IO.
    struct DmaRequest {
        PageKey key{};
        PageKind kind = PageKind::TextureTile;
        u32  bytes = 0;
        u32  dst_handle = 0;
    };
    int  compile_uploads(DmaRequest* out, int max_out, u32 frame);

    // GC sweeper — every 4 frames. Unmaps GPU memory of cold unpinned pages.
    int  garbage_collect(u32 frame);

    bool is_ready(PageKey k) const;
    u32  parent_fallback(PageKey k) const; // lod of always-resident ancestor

    u64  bytes_resident() const { return bytes_; }
    u32  page_count()     const { return u32(used_); }

    void pin(PageKey k);
    void unpin(PageKey k);

private:
    PageSlot* find(PageKey k);
    const PageSlot* find(PageKey k) const;
    PageSlot* alloc_slot(PageKey k, PageKind kind, u32 bytes, u32 frame);

    std::vector<PageSlot> slots_;
    std::vector<u32>      lru_;          // indices, front = cold
    u64  bytes_ = 0;
    int  used_  = 0;
};

// Mip that must always be in memory so a miss never stalls.
inline u32 mandatory_mip(PageKind k) {
    switch (k) {
        case PageKind::TextureTile:    return 8;  // 128k / 2^8 = 512 → 4×4 tiles
        case PageKind::HeightClipmap:  return 4;
        case PageKind::MeshletCluster: return 3;
        case PageKind::ProbeBrick:     return 2;
    }
    return 8;
}

} // namespace aetherion::core
