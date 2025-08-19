#include "M4qXE.hpp"

#include <cassert>
#include <chrono>
#include <sstream>

#if __has_include("MBLog.hpp")
#include "MBLog.hpp"
#define MBLOGI(fmt, ...) MB::Log().Log(MB::LogLevel::Info,  fmt, __VA_ARGS__)
#define MBLOGW(fmt, ...) MB::Log().Log(MB::LogLevel::Warn,  fmt, __VA_ARGS__)
#define MBLOGE(fmt, ...) MB::Log().Log(MB::LogLevel::Error, fmt, __VA_ARGS__)
#else
#define MBLOGI(...) (void)0
#define MBLOGW(...) (void)0
#define MBLOGE(...) (void)0
#endif

namespace MB {

    static inline uint64_t toUsec(std::chrono::steady_clock::duration d) {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(d).count());
    }

    const char* M4qXE::LaneName(Lane l) {
        switch (l) {
        case Lane::High:   return "High";
        case Lane::Normal: return "Normal";
        case Lane::Low:    return "Low";
        case Lane::IO:     return "IO";
        default:           return "?";
        }
    }

    M4qXE::M4qXE() = default;
    M4qXE::M4qXE(const Config& cfg) : _cfg(cfg) {}

    M4qXE::~M4qXE() {
        Stop();
    }

    void M4qXE::Start() {
        bool expected = false;
        if (!_running.compare_exchange_strong(expected, true)) {
            return; // already running
        }

        std::lock_guard<std::mutex> lk(_mx);
        _stopping = false;

        // Build schedule from weights
        _schedule.clear();
        auto pushN = [this](Lane l, unsigned n) {
            if (n == 0) n = 1;
            for (unsigned i = 0; i < n; ++i) _schedule.push_back(l);
            };
        pushN(Lane::High, _cfg.weightHigh);
        pushN(Lane::Normal, _cfg.weightNormal);
        pushN(Lane::Low, _cfg.weightLow);
        pushN(Lane::IO, _cfg.weightIO);
        if (_schedule.empty()) _schedule.push_back(Lane::Normal);
        _schedCursor = 0;

        const unsigned N = _cfg.workers ? _cfg.workers : 1;
        _threads.reserve(N);
        for (unsigned i = 0; i < N; ++i) {
            _threads.emplace_back(&M4qXE::workerLoop, this, i);
        }

        MBLOGI("M4qXE started with %u workers", N);
    }

    void M4qXE::Stop() {
        if (!_running.exchange(false)) {
            return; // was not running
        }
        {
            std::lock_guard<std::mutex> lk(_mx);
            _stopping = true;
            if (!_cfg.drainOnStop) {
                clearAllUnlocked();
            }
        }
        _cv.notify_all();

        for (auto& t : _threads) {
            if (t.joinable()) t.join();
        }
        _threads.clear();

        MBLOGI("M4qXE stopped");
    }

    void M4qXE::Flush() {
        std::unique_lock<std::mutex> lk(_mx);
        _cv.wait(lk, [this] {
            return !_running.load() || !hasAnyPendingUnlocked();
            });
    }

    bool M4qXE::Enqueue(Lane lane, Task task) {
        if (!task) return false;
        if (!_running.load()) return false;

        {
            std::lock_guard<std::mutex> lk(_mx);
            if (_stopping) return false;
            switch (lane) {
            case Lane::High:   _qHigh.dq.emplace_back(std::move(task));   ++_qHigh.enqCount;   break;
            case Lane::Normal: _qNormal.dq.emplace_back(std::move(task)); ++_qNormal.enqCount; break;
            case Lane::Low:    _qLow.dq.emplace_back(std::move(task));    ++_qLow.enqCount;    break;
            case Lane::IO:     _qIO.dq.emplace_back(std::move(task));     ++_qIO.enqCount;     break;
            default:           _qNormal.dq.emplace_back(std::move(task)); ++_qNormal.enqCount; break;
            }
        }
        _cv.notify_one();
        return true;
    }

    bool M4qXE::IsRunning() const {
        return _running.load();
    }

    size_t M4qXE::WorkerCount() const {
        return _threads.size();
    }

    M4qXE::Stats M4qXE::GetStats() const {
        std::lock_guard<std::mutex> lk(_mx);
        Stats s;
        s.executedHigh = _qHigh.execCount;
        s.executedNormal = _qNormal.execCount;
        s.executedLow = _qLow.execCount;
        s.executedIO = _qIO.execCount;
        s.enqHigh = _qHigh.enqCount;
        s.enqNormal = _qNormal.enqCount;
        s.enqLow = _qLow.enqCount;
        s.enqIO = _qIO.enqCount;
        s.pendingHigh = _qHigh.dq.size();
        s.pendingNormal = _qNormal.dq.size();
        s.pendingLow = _qLow.dq.size();
        s.pendingIO = _qIO.dq.size();
        s.ewmaUsec = _ewmaUsec;
        return s;
    }

    std::string M4qXE::StatsJSON() const {
        auto s = GetStats();
        std::ostringstream ss;
        ss.setf(std::ios::fixed);
        ss.precision(3);
        ss << "{"
            << "\"executed\":{"
            << "\"high\":" << s.executedHigh << ","
            << "\"normal\":" << s.executedNormal << ","
            << "\"low\":" << s.executedLow << ","
            << "\"io\":" << s.executedIO
            << "},"
            << "\"enqueued\":{"
            << "\"high\":" << s.enqHigh << ","
            << "\"normal\":" << s.enqNormal << ","
            << "\"low\":" << s.enqLow << ","
            << "\"io\":" << s.enqIO
            << "},"
            << "\"pending\":{"
            << "\"high\":" << s.pendingHigh << ","
            << "\"normal\":" << s.pendingNormal << ","
            << "\"low\":" << s.pendingLow << ","
            << "\"io\":" << s.pendingIO
            << "},"
            << "\"ewmaUsec\":" << s.ewmaUsec
            << "}";
        return ss.str();
    }

    bool M4qXE::hasAnyPendingUnlocked() const {
        return !_qHigh.dq.empty() || !_qNormal.dq.empty() || !_qLow.dq.empty() || !_qIO.dq.empty();
    }

    void M4qXE::clearAllUnlocked() {
        _qHigh.dq.clear();
        _qNormal.dq.clear();
        _qLow.dq.clear();
        _qIO.dq.clear();
    }

    bool M4qXE::tryPop(Task& out, Lane& outLane) {
        // Requires external lock held
        if (_schedule.empty()) {
            // Fallback ordering if someone bypassed Start()
            _schedule = { Lane::High, Lane::Normal, Lane::Low, Lane::IO };
        }

        const size_t N = _schedule.size();
        for (size_t k = 0; k < N; ++k) {
            const Lane l = _schedule[_schedCursor];
            _schedCursor = (_schedCursor + 1) % N;

            switch (l) {
            case Lane::High:
                if (!_qHigh.dq.empty()) { out = std::move(_qHigh.dq.front()); _qHigh.dq.pop_front(); outLane = Lane::High;   return true; }
                break;
            case Lane::Normal:
                if (!_qNormal.dq.empty()) { out = std::move(_qNormal.dq.front()); _qNormal.dq.pop_front(); outLane = Lane::Normal; return true; }
                break;
            case Lane::Low:
                if (!_qLow.dq.empty()) { out = std::move(_qLow.dq.front()); _qLow.dq.pop_front(); outLane = Lane::Low;    return true; }
                break;
            case Lane::IO:
                if (!_qIO.dq.empty()) { out = std::move(_qIO.dq.front()); _qIO.dq.pop_front(); outLane = Lane::IO;     return true; }
                break;
            default:
                break;
            }
        }
        return false;
    }

    void M4qXE::workerLoop(unsigned /*workerIndex*/) {
        using clock = std::chrono::steady_clock;
        for (;;) {
            Task t;
            Lane lane = Lane::Normal;

            {
                std::unique_lock<std::mutex> lk(_mx);
                _cv.wait(lk, [this] {
                    return _stopping || hasAnyPendingUnlocked();
                    });

                if (_stopping && !_cfg.drainOnStop) {
                    // Dropping tasks: exit immediately
                    break;
                }

                if (_stopping && _cfg.drainOnStop && !hasAnyPendingUnlocked()) {
                    // Done draining
                    break;
                }

                if (!tryPop(t, lane)) {
                    // Spuriously woke with no task and not stopping; loop again
                    continue;
                }
            }

            // Execute outside lock
            uint64_t usec = 0;
            try {
                const auto t0 = clock::now();
                t();
                const auto t1 = clock::now();
                usec = toUsec(t1 - t0);
            }
            catch (...) {
                // Avoid throwing across thread boundary
                MBLOGE("M4qXE: task threw an exception");
            }

            // Update stats
            {
                std::lock_guard<std::mutex> lk(_mx);
                switch (lane) {
                case Lane::High:   ++_qHigh.execCount;   break;
                case Lane::Normal: ++_qNormal.execCount; break;
                case Lane::Low:    ++_qLow.execCount;    break;
                case Lane::IO:     ++_qIO.execCount;     break;
                default: break;
                }
                // Simple EWMA with alpha ~ 0.1
                const double alpha = 0.1;
                _ewmaUsec = (_ewmaUsec <= 0.0) ? static_cast<double>(usec) : (alpha * usec + (1.0 - alpha) * _ewmaUsec);

                // If someone is waiting on Flush(), notify when queues become empty
                if (!hasAnyPendingUnlocked()) {
                    _cv.notify_all();
                }
            }
        }

        // Last notify in case a flusher is waiting
        _cv.notify_all();
    }

} // namespace MB
