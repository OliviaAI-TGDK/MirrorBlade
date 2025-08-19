#pragma once

#include <atomic>
#include <cstdint>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace MB {

    // M4qXE: "Multi-4-Queue eXecution Engine"
    // - 4 priority lanes: High, Normal, Low, IO
    // - Fixed-size worker pool
    // - Safe Start/Stop/Flush
    // - Per-lane enqueue and basic stats
    //
    // No external dependencies. Safe on MSVC/Win and Clang/GCC.

    class M4qXE {
    public:
        using Task = std::function<void()>;

        enum class Lane : uint8_t { High = 0, Normal = 1, Low = 2, IO = 3, Count = 4 };

        struct Config {
            unsigned    workers = std::thread::hardware_concurrency() ? std::thread::hardware_concurrency() : 4;
            // Relative selection weights (higher = more often). Must be >=1.
            unsigned    weightHigh = 8;
            unsigned    weightNormal = 4;
            unsigned    weightLow = 1;
            unsigned    weightIO = 2;

            // If true, Stop() waits for all pending tasks to finish. If false, queues are dropped.
            bool        drainOnStop = true;
        };

        struct Stats {
            uint64_t executedHigh = 0;
            uint64_t executedNormal = 0;
            uint64_t executedLow = 0;
            uint64_t executedIO = 0;
            uint64_t enqHigh = 0;
            uint64_t enqNormal = 0;
            uint64_t enqLow = 0;
            uint64_t enqIO = 0;

            // Pending snapshot at the time of query (not monotonically increasing).
            size_t   pendingHigh = 0;
            size_t   pendingNormal = 0;
            size_t   pendingLow = 0;
            size_t   pendingIO = 0;

            // Rough execution time EWMA in microseconds (best-effort).
            double   ewmaUsec = 0.0;
        };

        M4qXE();
        explicit M4qXE(const Config& cfg);
        ~M4qXE();

        // Non-copyable, movable disabled to avoid ownership ambiguity
        M4qXE(const M4qXE&) = delete;
        M4qXE& operator=(const M4qXE&) = delete;
        M4qXE(M4qXE&&) = delete;
        M4qXE& operator=(M4qXE&&) = delete;

        // Start worker pool (idempotent)
        void Start();

        // Stop worker pool (idempotent). If drainOnStop, flushes first.
        void Stop();

        // Blocks until all current tasks are consumed. Safe to call while running.
        void Flush();

        // Enqueue a task in a given lane. Returns false if engine is fully stopped (not running).
        bool Enqueue(Lane lane, Task task);

        // Query
        bool    IsRunning() const;
        size_t  WorkerCount() const;

        // Stats (cheap snapshot)
        Stats   GetStats() const;

        // Minimal JSON stats string (no external JSON dependency)
        std::string StatsJSON() const;

        // Convenience helpers
        static const char* LaneName(Lane l);

    private:
        struct Q {
            std::deque<Task> dq;
            uint64_t enqCount = 0;
            uint64_t execCount = 0;
        };

        void workerLoop(unsigned workerIndex);
