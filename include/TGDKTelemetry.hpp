#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <limits>
#include <vector>
#include <deque>

#include <nlohmann/json.hpp>
#include "Visceptar.hpp" // for Visceptar::Style used by a FormatTable overload

namespace MB {

    class TGDKTelemetry {
    public:
        using clock = std::chrono::steady_clock;

        // Lightweight event for ad-hoc telemetry streams
        struct Event {
            clock::time_point tp{};   // precise timestamp
            std::int64_t      t{ 0 };   // ms since epoch (cached for convenience)
            std::string       name;
            double            a{ 0.0 };
            double            b{ 0.0 };
            double            c{ 0.0 };
            std::string       tag;

            Event() = default;

            Event(clock::time_point when,
                std::string n, double aa, double bb, double cc, std::string tg = {})
                : tp(when),
                t(std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count()),
                name(std::move(n)), a(aa), b(bb), c(cc), tag(std::move(tg)) {
            }

            Event(std::string n, double aa, double bb, double cc, std::string tg = {})
                : Event(clock::now(), std::move(n), aa, bb, cc, std::move(tg)) {
            }
        };

        // Singleton
        static TGDKTelemetry& Get() noexcept;

        // Init can be a no-op for now, but keeps a place for future setup.
        void Init() noexcept {}

        // Opt-in/out (disabled by default)
        void OptIn(bool enabled) noexcept;
        bool IsOptedIn() const noexcept;

        // Counters
        void TrackCount(const std::string& key, std::int64_t delta) noexcept;

        // Timings (microseconds). Call Start then End with the same name.
        void TrackTimingStart(const std::string& name) noexcept;
        void TrackTimingEnd(const std::string& name) noexcept;

        // Arbitrary key/values under a named bucket (for light snapshots)
        void TrackKV(const std::string& bucket,
            const std::unordered_map<std::string, std::string>& kv) noexcept;

        // Simple JSON dump (for debugging/inspection)
        std::string DumpJSON() const;

        // ------- New ad-hoc event stream API -------

        // Push by fields (a,b,c are generic numeric payload; tag is optional)
        void Push(std::string_view name,
            double a = 0.0, double b = 0.0, double c = 0.0,
            std::string_view tag = {});

        // Push a ready-made event
        void Push(const Event& e);
        void Push(Event&& e);

        // Copy out last 'max' events (newest last)
        std::vector<Event> Snapshot(std::size_t max = 64) const;

        // JSON representation of recent events
        nlohmann::json SnapshotJSON(std::size_t max = 64) const;
        // Compatibility alias (some callers use this spelling)
        nlohmann::json SnapShotJSON(std::size_t max = 64) const { return SnapshotJSON(max); }

        // Pretty ASCII table for logs/UI
        static std::string FormatTable(const std::vector<Event>& evts,
            std::string_view title = "Telemetry");

        // Overload used by some call sites: render last N with a style
        static std::string FormatTable(std::size_t lastN,
            const std::string& title,
            Visceptar::Style style);

        // Set ring buffer cap for events
        void SetLimit(std::size_t limit);

    private:
        TGDKTelemetry() = default;

        struct TimingAccumulator {
            std::uint64_t count = 0;
            std::uint64_t total_us = 0;
            std::uint64_t min_us = (std::numeric_limits<std::uint64_t>::max)();
            std::uint64_t max_us = 0;
            std::uint64_t last_us = 0;
        };

        mutable std::mutex _mx;
        bool _optIn = false;

        std::unordered_map<std::string, std::int64_t>                      _counters;
        std::unordered_map<std::string, TimingAccumulator>                 _timings;
        std::unordered_map<std::string, clock::time_point>                 _inflight;
        std::unordered_map<std::string, std::unordered_map<std::string, std::string>> _kv;

        // Event ring buffer
        std::deque<Event> _events;
        std::size_t       _limit{ 512 };
    };

} // namespace MB
