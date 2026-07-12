#include <vortex/io/scheduler.h>
#include <vortex/io/mpmc_queue.h>

#include <algorithm>
#include <coroutine>
#include <iostream>
#include <thread>

#ifdef __linux__
#include <pthread.h>
#endif

using namespace vortex::io::detail;

namespace vortex::io {

// ==========================================================================
// Task type for the global queue
// ==========================================================================

struct ScheduledTask {
    void (*func)(void*);
    void* arg;
};

using GlobalQueue = MPMCQueue<ScheduledTask, 1024>;    // Power of 2
using LocalQueue = WorkStealingQueue<ScheduledTask, 256>;  // Power of 2

// ==========================================================================
// Scheduler implementation
// ==========================================================================

struct Scheduler::Worker {
    int id = 0;
    std::thread thread;
    std::unique_ptr<IEngine> engine;
    std::unique_ptr<LocalQueue> local_queue;
    bool running = true;
};

Scheduler::Scheduler(Config config) : config_(config) {
    if (config_.num_workers <= 0) {
        config_.num_workers = static_cast<int>(std::thread::hardware_concurrency());
        if (config_.num_workers <= 0) {
            config_.num_workers = 4;  // fallback
        }
    }

    workers_.reserve(config_.num_workers);
    global_queue_ = std::make_unique<GlobalQueue>();
}

Scheduler::~Scheduler() {
    if (running_.load(std::memory_order_acquire)) {
        shutdown();
    }
}

// ==========================================================================
// Start / Shutdown
// ==========================================================================

auto Scheduler::start() -> Error {
    if (running_.load(std::memory_order_acquire)) {
        return Error{Error::Code::kInvalidArgument, "scheduler already running"};
    }

    running_.store(true, std::memory_order_release);

    for (int i = 0; i < config_.num_workers; ++i) {
        auto w = std::make_unique<Scheduler::Worker>();
        w->id = i;
        w->engine = create_engine(config_.engine_type);
        w->local_queue = std::make_unique<LocalQueue>();

        auto* wp = w.get();
        w->thread = std::thread([this, wp]() {
            set_cpu_affinity(wp->id);
            worker_loop(*wp);
        });

        workers_.push_back(std::move(*w));  // Move into place
    }

    return Error{};
}

auto Scheduler::shutdown() -> Error {
    running_.store(false, std::memory_order_release);

    // Enqueue sentinel tasks to wake idle workers
    for (auto& w : workers_) {
        w.running = false;
    }

    // Join all workers
    for (auto& w : workers_) {
        if (w.thread.joinable()) {
            w.thread.join();
        }
    }

    workers_.clear();
    return Error{};
}

// ==========================================================================
// Task submission
// ==========================================================================

void Scheduler::schedule_local(TaskFunc task) {
    // This is called from a worker thread for its own queue.
    // We wrap the std::function in a heap-allocated copy and pass a function
    // pointer + arg so it can go through the lock-free queue.
    //
    // In production, we'd use a proper task abstraction (e.g., function_view +
    // intrusive list). For now, we use a simple heap allocation.
    auto* f = new TaskFunc(std::move(task));
    ScheduledTask t{[](void* arg) {
        auto* f = static_cast<TaskFunc*>(arg);
        (*f)();       // Execute
        delete f;     // Clean up
    }, f};

    // Push to the current worker's local queue.
    // We need to identify which worker we are — simplified: push to global.
    // In a real implementation, each worker would have a thread-local pointer.
    global_queue_->try_enqueue(t);
}

void Scheduler::schedule_remote(TaskFunc task) {
    auto* f = new TaskFunc(std::move(task));
    ScheduledTask t{[](void* arg) {
        auto* f = static_cast<TaskFunc*>(arg);
        (*f)();
        delete f;
    }, f};

    global_queue_->try_enqueue(t);
}

// ==========================================================================
// Worker loop
// ==========================================================================

void Scheduler::worker_loop(Worker& w) {
    // Buffer for I/O completions
    IoCompletion completion_buffer[64];

    while (w.running || global_queue_->approx_size() > 0) {
        int tasks_processed = 0;

        // --- Phase 1: Process I/O completions ---
        auto n = w.engine->wait_completions(
            std::span<IoCompletion>(completion_buffer, 64),
            0,  // non-blocking — return whatever is ready
            0); // zero timeout

        for (int i = 0; i < n; ++i) {
            auto& comp = completion_buffer[i];

            // If user_data is a function pointer, call it.
            // This is where coroutine resumption would happen — the
            // user_data carries the coroutine_handle to resume.
            if (comp.user_data) {
                // In Phase 2, user_data would be a coroutine_handle<>.
                // For now, we leave it as a hook point.
            }
            tasks_processed++;
        }

        // --- Phase 2: Drain local queue ---
        for (int i = 0; i < 16; ++i) {  // Process up to 16 local tasks per round
            auto task = w.local_queue->pop();
            if (!task.has_value()) break;

            task->func(task->arg);
            tasks_processed++;
        }

        // --- Phase 3: Drain global queue ---
        for (int i = 0; i < 8; ++i) {
            auto task = global_queue_->try_dequeue();
            if (!task.has_value()) break;

            task->func(task->arg);
            tasks_processed++;
        }

        // --- Phase 4: Work-stealing ---
        if (tasks_processed == 0) {
            // This worker is idle — try to steal from neighbors.
            for (auto& other : workers_) {
                if (other.id == w.id) continue;
                if (!other.running) continue;

                auto task = other.local_queue->steal();
                if (task.has_value()) {
                    task->func(task->arg);
                    tasks_processed++;
                    break;  // Got one — process it and loop again
                }
            }
        }

        // --- Phase 5: Yield if completely idle ---
        if (tasks_processed == 0 && w.running) {
            // No work anywhere — yield to save CPU.
            // In production with io_uring SQPOLL, the SQPOLL kernel thread
            // keeps polling for new I/O; we'd sleep on a futex here.
            std::this_thread::yield();
        }
    }
}

// ==========================================================================
// CPU affinity
// ==========================================================================

void Scheduler::set_cpu_affinity(int worker_id) {
#ifdef __linux__
    auto cpu = config_.cpu_affinity_base + worker_id;
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);

    auto tid = pthread_self();
    auto ret = pthread_setaffinity_np(tid, sizeof(cpu_set_t), &cpuset);
    if (ret != 0) {
        // Non-fatal — the scheduler still works without pinning.
        std::cerr << "vortex: warning: pthread_setaffinity_np failed for worker "
                  << worker_id << " cpu " << cpu << "\n";
    }
#else
    (void)worker_id;
#endif
}

// ==========================================================================
// Engine access
// ==========================================================================

auto Scheduler::engine() -> IEngine& {
    // Must be called from a worker thread.
    // In a real implementation, we'd use thread_local storage to find the
    // current worker. For now, return the first worker's engine.
    return *workers_[0].engine;
}

}  // namespace vortex::io
