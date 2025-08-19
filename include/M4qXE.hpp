#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace MB {

    class M4qXE {
    public:
        enum class Lane { High, Normal, Low, IO };
        using Task = std::function<void()>;

        struct Config {
            unsigned workers{ 0 };        // 0 => decide at runtime
            unsigned weightHigh{ 3 };
            unsigned weightNormal{ 2 };
            unsigned weightLow{ 1 };
            unsigned weightIO{ 1 };
            bool     drainOnStop{ true };
        };

        struct Stats {
            std::uint64_t executedHigh{ 0 };
            std::uint64_t executedNormal{ 0 };
            std::uint64_t executedLow{ 0 };
            std::uint64_t executedIO{ 0 };

            std::uint64_t enqHigh{ 0 };
            std::uint64_t enqNormal{ 0 };
            std::uint64_t enqLow{ 0 };
            std::uint64_t enqIO{ 0 };

            std::size_t pendingHigh{ 0 };
            std::size_t pendingNormal{ 0 };
            std::size_t pendingLow{ 0 };
            std::size_t pendingIO{ 0 };

            double ewmaUsec{ 0.0 };
        };

        static const char* LaneName(Lane l) noexcept;

        M4qXE();
        explicit M4qXE(const Config& cfg);
        ~M4qXE();

        void Start();
        void Stop();
        void Flush();

        bool Enqueue(Lane lane, Task task);
        bool Enqueue(Lane lane, Task&& task);

        bool   IsRunning() const noexcept;
        std::size_t WorkerCount() const;
        Stats  GetStats() const;
        std::string StatsJSON() const;

    private:
        bool hasAnyPendingUnlocked() const;
        void clearAllUnlocked();
        bool tryPop(Task& out, Lane& outLane);
        void workerLoop(unsigned workerIndex);

        struct Q {
            std::deque<Task> dq;
            std::uint64_t enqCount{ 0 };
            std::uint64_t execCount{ 0 };
        };

        Config _cfg{};
        mutable std::mutex _mx;
        std::condition_variable _cv;

        std::atomic<bool> _running{ false };
        bool _stopping{ false };

        std::vector<Lane> _schedule;
        std::size_t _schedCursor{ 0 };

        std::vector<std::thread> _threads;

        Q _qHigh, _qNormal, _qLow, _qIO;
        double _ewmaUsec{ 0.0 };
    };

} // namespace MB
