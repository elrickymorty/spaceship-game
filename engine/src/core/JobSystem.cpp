#include "aetherion/core/JobSystem.hpp"
#include <algorithm>
#include <cstdlib>

namespace aetherion::core {

JobSystem::JobSystem() {
    unsigned n = std::max(2u, std::thread::hardware_concurrency());
    // Leave one core for OS + game/render. Workers = N-1, min 1.
    unsigned workers = n > 2 ? n - 1 : 1;
    workers_.reserve(workers);
    for (unsigned i = 0; i < workers; ++i)
        workers_.emplace_back([this, i] { worker_loop(int(i)); });
}

JobSystem::~JobSystem() { stop(); }

void JobSystem::stop() {
    running_.store(false, std::memory_order_release);
    cv_.notify_all();
    for (auto& t : workers_) if (t.joinable()) t.join();
    workers_.clear();
}

void JobSystem::kick(void (*fn)(void*), void* user, std::atomic<u32>* counter) {
    if (counter) counter->fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> g(mu_);
        queue_.push_back(Job{fn, user, counter});
    }
    cv_.notify_one();
}

void JobSystem::kick_n(void (*fn)(void*), void** users, int n, std::atomic<u32>* counter) {
    if (n <= 0) return;
    if (counter) counter->fetch_add(u32(n), std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> g(mu_);
        queue_.reserve(queue_.size() + std::size_t(n));
        for (int i = 0; i < n; ++i)
            queue_.push_back(Job{fn, users[i], counter});
    }
    cv_.notify_all();
}

void JobSystem::wait(std::atomic<u32>* counter) {
    if (!counter) return;
    // Help out while waiting — the game thread is allowed to steal.
    Job job;
    while (counter->load(std::memory_order_acquire) != 0) {
        bool had = false;
        {
            std::lock_guard<std::mutex> g(mu_);
            had = pop(job);
        }
        if (had) {
            job.fn(job.user);
            if (job.counter) job.counter->fetch_sub(1, std::memory_order_acq_rel);
        } else {
            std::this_thread::yield();
        }
    }
}

bool JobSystem::pop(Job& out) {
    if (queue_.empty()) return false;
    out = queue_.back();
    queue_.pop_back();
    return true;
}

void JobSystem::worker_loop(int) {
    while (running_.load(std::memory_order_acquire)) {
        Job job{};
        {
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait(lk, [&] {
                return !queue_.empty() || !running_.load(std::memory_order_relaxed);
            });
            if (!running_.load(std::memory_order_relaxed) && queue_.empty()) return;
            if (!pop(job)) continue;
        }
        job.fn(job.user);
        if (job.counter) job.counter->fetch_sub(1, std::memory_order_acq_rel);
    }
}

FrameArena::FrameArena(std::size_t bytes) : cap_(bytes) {
    mem_ = static_cast<u8*>(std::aligned_alloc(64, (bytes + 63) & ~std::size_t(63)));
}
FrameArena::~FrameArena() { std::free(mem_); }
void FrameArena::reset() { cursor_ = 0; }
void* FrameArena::alloc(std::size_t bytes, std::size_t align) {
    std::size_t mask = align - 1;
    std::size_t aligned = (cursor_ + mask) & ~mask;
    if (aligned + bytes > cap_) return nullptr;
    void* p = mem_ + aligned;
    cursor_ = aligned + bytes;
    return p;
}

} // namespace aetherion::core
