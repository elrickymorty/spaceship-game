#include "aetherion/core/Streaming.hpp"
#include <algorithm>

namespace aetherion::core {

ResidencyManager::ResidencyManager() {
    slots_.resize(kMaxPages);
    lru_.reserve(kMaxPages);
}

PageSlot* ResidencyManager::find(PageKey k) {
    for (int i = 0; i < used_; ++i)
        if (slots_[std::size_t(i)].key == k) return &slots_[std::size_t(i)];
    return nullptr;
}
const PageSlot* ResidencyManager::find(PageKey k) const {
    for (int i = 0; i < used_; ++i)
        if (slots_[std::size_t(i)].key == k) return &slots_[std::size_t(i)];
    return nullptr;
}

PageSlot* ResidencyManager::alloc_slot(PageKey k, PageKind kind, u32 bytes, u32 frame) {
    if (bytes_ + bytes > kBudgetBytes) {
        for (int i = 0; i < used_ && bytes_ + bytes > kBudgetBytes; ++i) {
            PageSlot& s = slots_[std::size_t(i)];
            if (s.pinned || !s.ready) continue;
            if (s.last_frame + 8 > frame) continue;
            bytes_ -= s.bytes;
            s = PageSlot{};
        }
    }
    if (used_ >= int(kMaxPages)) return nullptr;
    for (int i = 0; i < used_; ++i) {
        if (slots_[std::size_t(i)].bytes == 0 && slots_[std::size_t(i)].key.asset == 0) {
            PageSlot& s = slots_[std::size_t(i)];
            s.key = k; s.kind = kind; s.bytes = bytes; s.last_frame = frame;
            s.mip_parent = u8(mandatory_mip(kind));
            s.ready = false; s.pinned = false;
            bytes_ += bytes;
            return &s;
        }
    }
    PageSlot& s = slots_[std::size_t(used_++)];
    s.key = k; s.kind = kind; s.bytes = bytes; s.last_frame = frame;
    s.mip_parent = u8(mandatory_mip(kind));
    s.ready = false; s.pinned = false;
    bytes_ += bytes;
    return &s;
}

void ResidencyManager::ingest_feedback(const FeedbackSample* samples, int count, u32 frame) {
    for (int i = 0; i < count; ++i) {
        const FeedbackSample& f = samples[i];
        PageKey k = f.key;
        k.lod = u32(f.desired_lod);
        if (PageSlot* s = find(k)) {
            s->last_frame = frame;
        } else {
            u32 bytes = 96u * 1024u;
            if (k.lod > 4) bytes /= 4;
            alloc_slot(k, PageKind::TextureTile, bytes, frame);
        }
    }
}

int ResidencyManager::compile_uploads(DmaRequest* out, int max_out, u32 frame) {
    int n = 0;
    for (int i = 0; i < used_ && n < max_out; ++i) {
        PageSlot& s = slots_[std::size_t(i)];
        if (s.ready || s.bytes == 0) continue;
        out[n].key = s.key;
        out[n].kind = s.kind;
        out[n].bytes = s.bytes;
        out[n].dst_handle = s.gpu_handle;
        s.ready = true;
        s.last_frame = frame;
        ++n;
    }
    return n;
}

int ResidencyManager::garbage_collect(u32 frame) {
    int freed = 0;
    for (int i = 0; i < used_; ++i) {
        PageSlot& s = slots_[std::size_t(i)];
        if (s.pinned || s.bytes == 0) continue;
        if (s.last_frame + kUnpinAfterFrames >= frame) continue;
        bytes_ -= s.bytes;
        s = PageSlot{};
        ++freed;
    }
    return freed;
}

bool ResidencyManager::is_ready(PageKey k) const {
    const PageSlot* s = find(k);
    return s && s->ready;
}
u32 ResidencyManager::parent_fallback(PageKey k) const {
    return mandatory_mip(PageKind::TextureTile) < k.lod
               ? k.lod
               : mandatory_mip(PageKind::TextureTile);
}
void ResidencyManager::pin(PageKey k) {
    if (PageSlot* s = find(k)) s->pinned = true;
}
void ResidencyManager::unpin(PageKey k) {
    if (PageSlot* s = find(k)) s->pinned = false;
}

} // namespace aetherion::core
