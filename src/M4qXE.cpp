// M4qXE.cpp — hardened against MIDL/Windows macro pollution
// Detox FIRST, before any includes (works even if a PCH or /FI pulled in rpcndr.h earlier)
#ifdef string
#undef string
#endif
#ifdef small
#undef small
#endif
#ifdef uuid
#undef uuid
#endif
#ifdef hyper
#undef hyper
#endif
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#ifdef interface
#undef interface
#endif
#ifdef near
#undef near
#endif
#ifdef far
#undef far
#endif
#ifdef function
#undef function
#endif
#ifdef Handler
#undef Handler
#endif

// --- include STL FIRST ---
#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>
#include <array>
#include <queue>
#include <deque>
#include <mutex>
#include <atomic>
#include <thread>
#include <future>
#include <optional>
#include <unordered_map>
#include <functional>
#include <utility>
#include <algorithm>
#include <memory>
#include <cassert>
#include <chrono>
#include <sstream>

// --- safe logging (scrubs cstdarg early via MBLog.hpp) ---
#include "MBLog.hpp"

// --- then your own headers that might pull Windows stuff ---
#include "M4qXE.hpp"

#if __has_include("MBLog.hpp")

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

    const char* M4qXE::LaneName(Lane l) noexcept {
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

        // Optional per-run reset of stats/ewma
        _qHigh.execCount = _qNormal.execCount = _qLow.execCount = _qIO.execCount = 0;
        _qHigh.enqCount = _qNormal.enqCount = _qLow.enqCount = _qIO.enqCount = 0;
        _ewmaUsec = 0.0;
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
            // Exit if not running OR no pending work (covers drain and non-drain)
            return !_running.load(std::memory_order_acquire) || !hasAnyPendingUnlocked();
            });
    }

    bool M4qXE::Enqueue(Lane lane, Task task) {
        if (!task) return false;
        if (!_running.load(std::memory_order_acquire)) return false;

        {
            std::lock_guard<std::mutex> lk(_mx);
            if (_stopping) return false;

            auto& q = (lane == Lane::High) ? _qHigh
                : (lane == Lane::Normal) ? _qNormal
                : (lane == Lane::Low) ? _qLow
                : _qIO; // default to IO only if explicitly asked

            q.dq.emplace_back(std::move(task));
            ++q.enqCount;
        }
        _cv.notify_one();
        return true;
    }

    bool M4qXE::IsRunning() const noexcept {
        return _running.load(std::memory_order_acquire);
    }

    size_t M4qXE::WorkerCount() const {
        std::lock_guard<std::mutex> lk(_mx);
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
                if (!_qHigh.dq.empty()) {
                    out = std::move(_qHigh.dq.front()); _qHigh.dq.pop_front(); outLane = Lane::High; return true;
                }
                break;
            case Lane::Normal:
                if (!_qNormal.dq.empty()) {
                    out = std::move(_qNormal.dq.front()); _qNormal.dq.pop_front(); outLane = Lane::Normal; return true;
                }
                break;
            case Lane::Low:
                if (!_qLow.dq.empty()) {
                    out = std::move(_qLow.dq.front()); _qLow.dq.pop_front(); outLane = Lane::Low; return true;
                }
                break;
            case Lane::IO:
                if (!_qIO.dq.empty()) {
                    out = std::move(_qIO.dq.front()); _qIO.dq.pop_front(); outLane = Lane::IO; return true;
                }
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
                    // Wake when there is work, or we are stopping
                    return _stopping || hasAnyPendingUnlocked();
                    });

                if (_stopping) {
                    if (!_cfg.drainOnStop) break;
                    if (!hasAnyPendingUnlocked()) break; // drained
                }

                if (!tryPop(t, lane)) {
                    // Spurious wake or no work after drain; continue wait
                    continue;
                }
            }

            uint64_t usec = 0;
            try {
                const auto t0 = clock::now();
                t();
                const auto t1 = clock::now();
                usec = toUsec(t1 - t0);
            }
            catch (...) {
                MBLOGE("M4qXE: task threw an exception");
            }

            {
                std::lock_guard<std::mutex> lk(_mx);
                switch (lane) {
                case Lane::High:   ++_qHigh.execCount;   break;
                case Lane::Normal: ++_qNormal.execCount; break;
                case Lane::Low:    ++_qLow.execCount;    break;
                case Lane::IO:     ++_qIO.execCount;     break;
                default: break;
                }
                const double alpha = 0.1;
                _ewmaUsec = (_ewmaUsec <= 0.0)
                    ? static_cast<double>(usec)
                    : (alpha * usec + (1.0 - alpha) * _ewmaUsec);

                if (!hasAnyPendingUnlocked()) {
                    _cv.notify_all();
                }
            }
        }
        _cv.notify_all();
    }

} // namespace MB
