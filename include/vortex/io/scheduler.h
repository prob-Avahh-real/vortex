#pragma once

#include <atomic>
#include <coroutine>
#include <cstddef>
#include <functional>
#include <memory>
#include <span>
#include <thread>
#include <vector>

#include <vortex/io/engine.h>

namespace vortex::io {

// ==========================================================================
// Coroutine Scheduler — per-worker-thread async task scheduler
// ==========================================================================

// A unit of work that the scheduler can execute.
using TaskFunc = std::function<void()>;

// Forward declaration for work-stealing queue.
namespace detail {
class WorkStealingQueue;
}

class Scheduler {
public:
    struct Config {
        int num_workers = 0;  // 0 = num CPU cores
        int cpu_affinity_base = 0;  // Starting CPU to pin workers
        EngineType engine_type = EngineType::kIoUring;
    };

    explicit Scheduler(Config config);
    ~Scheduler();

    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;

    // Start all worker threads and begin processing.
    auto start() -> Error;

    // Graceful shutdown — drain all pending tasks, then join workers.
    auto shutdown() -> Error;

    // Submit a task to this worker's local queue (caller must be on worker thread).
    void schedule_local(TaskFunc task);

    // Submit a task to the global queue (thread-safe, any caller).
    void schedule_remote(TaskFunc task);

    // Get the I/O engine for the current worker thread.
    // Must be called from within a worker thread.
    [[nodiscard]] auto engine() -> IEngine&;

private:
    struct Worker {
        int id;
        std::thread thread;
        std::unique_ptr<IEngine> engine;
        std::unique_ptr<detail::WorkStealingQueue> local_queue;
    };

    void worker_loop(Worker& w);

    Config config_;
    std::vector<Worker> workers_;
    std::unique_ptr<detail::WorkStealingQueue> global_queue_;
    std::atomic<bool> running_{false};
};

}  // namespace vortex::io
