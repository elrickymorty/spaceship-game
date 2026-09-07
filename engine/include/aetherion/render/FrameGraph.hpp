#pragma once
// Transient frame graph. Resources are virtual until compile, then aliased
// onto a 256 MB ring. The visbuffer, Hi-Z and SSR history share physical tiles.
#include "aetherion/math/Types.hpp"
#include <vector>

namespace aetherion::render {

enum class ResFormat : u8 {
    R32U, D32F, RGBA16F, RGBA8, RG16F, R16F, R8, Count
};

inline u32 bytes_per_pixel(ResFormat f) {
    switch (f) {
        case ResFormat::R32U:
        case ResFormat::D32F:    return 4;
        case ResFormat::RGBA16F: return 8;
        case ResFormat::RGBA8:   return 4;
        case ResFormat::RG16F:   return 4;
        case ResFormat::R16F:    return 2;
        case ResFormat::R8:      return 1;
        default: return 4;
    }
}

struct FGResource {
    char name[32]{};
    u16  w = 1, h = 1, d = 1;
    ResFormat format = ResFormat::RGBA8;
    u16  created_by = 0xFFFF;
    u16  last_use   = 0;
    u32  alias_offset = ~0u; // filled at compile
    u32  bytes = 0;
};

enum class Queue : u8 { Graphics, Compute, Copy };

struct FGPass {
    char name[32]{};
    Queue queue = Queue::Graphics;
    u16  reads[8]{};
    u16  writes[8]{};
    u8   nreads = 0, nwrites = 0;
    bool async = false;
};

class FrameGraph {
public:
    static constexpr u32 kTransientBytes = 256u * 1024u * 1024u;

    std::vector<FGResource> resources;
    std::vector<FGPass>     passes;

    u16  add_resource(const char* name, u16 w, u16 h, ResFormat fmt, u16 d = 1);
    u16  add_pass(const char* name, Queue q);
    void reads(u16 pass, u16 res);
    void writes(u16 pass, u16 res);

    // Liveness-color and alias onto the 256 MB ring. Returns peak bytes used.
    u32  compile();

    u32 peak_bytes = 0;
    u32 alias_fails = 0; // >0 means we exceeded the ring (drop SSR history etc.)
};

} // namespace aetherion::render
