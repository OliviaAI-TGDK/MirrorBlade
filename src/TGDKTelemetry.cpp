#include "TGDKTelemetry.hpp"

#include <algorithm>
#include <sstream>
#include <iomanip>

namespace MB {

    static inline std::uint64_t toUsec(std::chrono::steady_clock::duration d) {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(d).count());
    }

    TGDKTelemetry& TGDKTelemetry::Get() noexcept {
        static TGDKTelemetry g;
        return g;
    }

    void TGDKTelemetry::OptIn(bool enabled) noexcept {
        std::scoped_lock lk(_mx);
        _optIn = enabled;
    }

    bool TGDKTelemetry::IsOptedIn() const noexcept {
        std::scoped_lock lk(_mx);
        return _optIn;
    }

    void TGDKTelemetry::TrackCount(const std::string& key, std::int64_t delta) noexcept {
        std::scoped_lock lk(_mx);
        if (!_optIn) return;
        _counters[key] += delta;
    }

    void TGDKTelemetry::TrackTimingStart(const std::string& name) noexcept {
        std::scoped_lock lk(_mx);
        if (!_optIn) return;
        _inflight[name] = clock::now();
    }

    void TGDKTelemetry::TrackTimingEnd(const std::string& name) noexcept {
        std::scoped_lock lk(_mx);
        if (!_optIn) return;

        auto it = _inflight.find(name);
        if (it == _inflight.end()) return;

        const auto dt = toUsec(clock::now() - it->second);
        _inflight.erase(it);

        auto& acc = _timings[name];
        ++acc.count;
        acc.total_us += dt;
        acc.last_us = dt;
        acc.min_us = std::min(acc.min_us, dt);
        acc.max_us = std::max(acc.max_us, dt);
    }

    void TGDKTelemetry::TrackKV(const std::string& bucket,
        const std::unordered_map<std::string, std::string>& kv) noexcept {
        std::scoped_lock lk(_mx);
        if (!_optIn) return;
        auto& dict = _kv[bucket];
        for (const auto& [k, v] : kv) dict[k] = v;
    }

    std::string TGDKTelemetry::DumpJSON() const {
        using json = nlohmann::json;
        json root;
        {
            std::scoped_lock lk(_mx);
            root["ok"] = true;
            root["optIn"] = _optIn;

            // counters
            {
                json jcnt = json::object();
                for (const auto& [k, v] : _counters) jcnt[k] = v;
                root["counters"] = std::move(jcnt);
            }

            // timings
            {
                json jtim = json::object();
                for (const auto& [name, acc] : _timings) {
                    json jt;
                    jt["count"] = acc.count;
                    jt["total_us"] = acc.total_us;
                    jt["min_us"] = acc.min_us == (std::numeric_limits<std::uint64_t>::max)() ? 0 : acc.min_us;
                    jt["max_us"] = acc.max_us;
                    jt["last_us"] = acc.last_us;
                    jt["avg_us"] = acc.count ? static_cast<double>(acc.total_us) / static_cast<double>(acc.count) : 0.0;
                    jtim[name] = std::move(jt);
                }
                root["timings"] = std::move(jtim);
            }

            // kv
            {
                json jkv = json::object();
                for (const auto& [bucket, dict] : _kv) {
                    json jd = json::object();
                    for (const auto& [k, v] : dict) jd[k] = v;
                    jkv[bucket] = std::move(jd);
                }
                root["kv"] = std::move(jkv);
            }

            // events (just expose sizes here; use SnapshotJSON for details)
            root["events_size"] = _events.size();
            root["events_limit"] = _limit;
        }
        return root.dump();
    }

    // ------- Event stream -------

    void TGDKTelemetry::SetLimit(std::size_t limit) {
        std::scoped_lock lk(_mx);
        _limit = limit ? limit : 1;
        while (_events.size() > _limit) _events.pop_front();
    }

    void TGDKTelemetry::Push(std::string_view name,
        double a, double b, double c,
        std::string_view tag) {
        std::scoped_lock lk(_mx);
        if (!_optIn) return;

        Event e;
        e.tp = clock::now();
        e.t = std::chrono::duration_cast<std::chrono::milliseconds>(e.tp.time_since_epoch()).count();
        e.name = std::string{ name };
        e.a = a;
        e.b = b;
        e.c = c;
        e.tag = std::string{ tag };

        _events.emplace_back(std::move(e));
        if (_events.size() > _limit) _events.pop_front();
    }

    void TGDKTelemetry::Push(const Event& eIn) {
        std::scoped_lock lk(_mx);
        if (!_optIn) return;

        Event e = eIn;
        if (e.tp == clock::time_point{}) {
            e.tp = clock::now();
        }
        if (e.t == 0) {
            e.t = std::chrono::duration_cast<std::chrono::milliseconds>(e.tp.time_since_epoch()).count();
        }

        _events.emplace_back(std::move(e));
        if (_events.size() > _limit) _events.pop_front();
    }

    void TGDKTelemetry::Push(Event&& eIn) {
        std::scoped_lock lk(_mx);
        if (!_optIn) return;

        Event e = std::move(eIn);
        if (e.tp == clock::time_point{}) {
            e.tp = clock::now();
        }
        if (e.t == 0) {
            e.t = std::chrono::duration_cast<std::chrono::milliseconds>(e.tp.time_since_epoch()).count();
        }

        _events.emplace_back(std::move(e));
        if (_events.size() > _limit) _events.pop_front();
    }

    std::vector<TGDKTelemetry::Event> TGDKTelemetry::Snapshot(std::size_t max) const {
        std::scoped_lock lk(_mx);
        const std::size_t n = std::min<std::size_t>(max ? max : 1, _events.size());
        std::vector<Event> out;
        out.reserve(n);
        auto it = _events.end();
        std::advance(it, -static_cast<long>(n));
        std::copy(it, _events.end(), std::back_inserter(out));
        return out;
    }

    nlohmann::json TGDKTelemetry::SnapshotJSON(std::size_t max) const {
        using json = nlohmann::json;
        json arr = json::array();
        for (const auto& e : Snapshot(max)) {
            arr.push_back({
                {"t",    e.t},
                {"name", e.name},
                {"a",    e.a},
                {"b",    e.b},
                {"c",    e.c},
                {"tag",  e.tag}
                });
        }
        return json{ {"ok", true}, {"events", std::move(arr)} };
    }

    static std::string pad(int width, char ch = '-') {
        return std::string(static_cast<std::size_t>(std::max(width, 0)), ch);
    }

    std::string TGDKTelemetry::FormatTable(const std::vector<Event>& evts,
        std::string_view title) {
        int wT = 10, wNm = 16, wNum = 10, wTag = 16;
        for (const auto& e : evts) {
            wNm = std::max(wNm, static_cast<int>(e.name.size()));
            wTag = std::max(wTag, static_cast<int>(e.tag.size()));
        }

        std::ostringstream ss;
        ss << std::left;
        ss << "  " << title << "\n";
        ss << ' ' << std::setw(wT) << "t(ms)"
            << ' ' << std::setw(wNm) << "name"
            << ' ' << std::setw(wNum) << "a"
            << ' ' << std::setw(wNum) << "b"
            << ' ' << std::setw(wNum) << "c"
            << ' ' << std::setw(wTag) << "tag" << "\n";
        ss << ' ' << pad(wT)
            << ' ' << pad(wNm)
            << ' ' << pad(wNum)
            << ' ' << pad(wNum)
            << ' ' << pad(wNum)
            << ' ' << pad(wTag) << "\n";

        ss << std::fixed << std::setprecision(3);
        for (const auto& e : evts) {
            ss << ' ' << std::setw(wT) << e.t
                << ' ' << std::setw(wNm) << e.name
                << ' ' << std::setw(wNum) << e.a
                << ' ' << std::setw(wNum) << e.b
                << ' ' << std::setw(wNum) << e.c
                << ' ' << std::setw(wTag) << e.tag
                << "\n";
        }
        return ss.str();
    }

    std::string TGDKTelemetry::FormatTable(std::size_t lastN,
        const std::string& title,
        Visceptar::Style /*style*/) {
        // Pull from the singleton instance, then render.
        const auto evts = TGDKTelemetry::Get().Snapshot(lastN);
        return TGDKTelemetry::FormatTable(evts, title);
    }


} // namespace MB
