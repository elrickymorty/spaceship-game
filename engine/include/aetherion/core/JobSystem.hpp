#pragma once
// Fiber-style job system. Procgen, physics islands, fauna, mesh-merge, GC
// never run on the game or render thread.
#include "aetherion/math/Types.hpp"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace aetherion::core {

struct Job {
    void (*fn)(void*) = nullptr;
    void*  user       = nullptr;
    std::atomic<u32>* counter = nullptr;
};

class JobSystem {
public:
    JobSystem();
    ~JobSystem();
    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    int worker_count() const { return int(workers_.size()); }

    // Enqueue. If counter != null it is incremented once per job and
    // decremented when the job returns. Wait on counter==0.
    void kick(void (*fn)(void*), void* user, std::atomic<u32>* counter = nullptr);
    void kick_n(void (*fn)(void*), void** users, int n, std::atomic<u32>* counter);

    void wait(std::atomic<u32>* counter);
    void stop();

private:
    void worker_loop(int index);
    bool pop(Job& out);

    std::mutex              mu_;
    std::condition_variable cv_;
    std::vector<Job>        queue_;
    std::vector<std::thread> workers_;
    std::atomic<bool>       running_{true};
};

// Frame bump allocator. Render thread is forbidden from heap-allocating.
class FrameArena {
public:
    explicit FrameArena(std::size_t bytes);
    ~FrameArena();
    void  reset();
    void* alloc(std::size_t bytes, std::size_t align = 16);
    template <typename T>
    T* alloc_array(std::size_t n) {
        return static_cast<T*>(alloc(sizeof(T) * n, alignof(T)));
    }
    std::size_t used() const { return cursor_; }
    std::size_t cap()  const { return cap_; }
private:
    u8*         mem_    = nullptr;
    std::size_t cap_    = 0;
    std::size_t cursor_ = 0;
};

} // namespace aetherion::core
