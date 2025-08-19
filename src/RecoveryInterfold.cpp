#include "RecoveryInterfold.hpp"
#include <nlohmann/json.hpp>
#include <cmath>
#include <algorithm>

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

    using json = nlohmann::json;

    // ----------------- Param I/O -----------------

    void RecoveryInterfold::SetParams(const Params& p) {
        std::lock_guard<std::mutex> lk(_mx);
        _p = p;
    }

    RecoveryInterfold::Params RecoveryInterfold::GetParams() const {
        std::lock_guard<std::mutex> lk(_mx);
        return _p;
    }

    void RecoveryInterfold::ConfigureFromJSON(const json& j) {
        if (!j.is_object()) return;
        std::lock_guard<std::mutex> lk(_mx);

        auto getf = [&](const char* k, float& dst) { if (j.contains(k)) dst = j.value(k, dst); };
        auto getb = [&](const char* k, bool& dst) { if (j.contains(k)) dst = j.value(k, dst); };

        getb("enabled", _p.enabled);
        getb("abideEmptiness", _p.abideEmptiness);

        getf("stiffness", _p.stiffness);
        getf("damping", _p.damping);

        getf("hysteresisBand", _p.hysteresisBand);

        getf("jumpThreshold", _p.jumpThreshold);
        getf("cooldownSeconds", _p.cooldownSeconds);
        getf("cooldownGain", _p.cooldownGain);

        getb("clampEnabled", _p.clampEnabled);
        getf("clampMin", _p.clampMin);
        getf("clampMax", _p.clampMax);

        getb("snapFirstSample", _p.snapFirstSample);
        getf("maxVelocity", _p.maxVelocity);

        if (_p.clampEnabled && _p.clampMin > _p.clampMax)
            std::swap(_p.clampMin, _p.clampMax);
    }

    json RecoveryInterfold::SnapshotJSON() const {
        std::lock_guard<std::mutex> lk(_mx);
        return json{
            {"enabled",         _p.enabled},
            {"abideEmptiness",  _p.abideEmptiness},
            {"stiffness",       _p.stiffness},
            {"damping",         _p.damping},
            {"hysteresisBand",  _p.hysteresisBand},
            {"jumpThreshold",   _p.jumpThreshold},
            {"cooldownSeconds", _p.cooldownSeconds},
            {"cooldownGain",    _p.cooldownGain},
            {"clampEnabled",    _p.clampEnabled},
            {"clampMin",        _p.clampMin},
            {"clampMax",        _p.clampMax},
            {"snapFirstSample", _p.snapFirstSample},
            {"maxVelocity",     _p.maxVelocity},
            {"state", {
                {"output",            _s.y},
                {"velocity",          _s.v},
                {"cooldownRemaining", _s.cooldown},
                {"seeded",            _s.seeded}
            }}
        };
    }

    RecoveryInterfold::Snapshot RecoveryInterfold::SnapshotState() const {
        std::lock_guard<std::mutex> lk(_mx);
        Snapshot ss;
        ss.output = _s.y;
        ss.velocity = _s.v;
        ss.cooldownRemaining = _s.cooldown;
        ss.seeded = _s.seeded;
        ss.params = _p;
        return ss;
    }

    // ----------------- Core math -----------------

    void RecoveryInterfold::integrateStep(State& st, const Params& p, float dt, double x)
    {
        // dt protection
        if (dt <= 0.f) return;

        // Effective stiffness (reduced during cooldown)
        float k = p.stiffness;
        if (st.cooldown > 0.0) {
            k *= std::max(0.0f, p.cooldownGain);
            st.cooldown = std::max(0.0, st.cooldown - static_cast<double>(dt));
        }

        // Hysteresis: reduce movement if inside a small band
        const double e = x - st.y;
        const double ae = std::fabs(e);
        const double band = static_cast<double>(p.hysteresisBand);
        const double bandScale = (ae < band && band > 1e-12)
            ? (ae / band)         // linearly fade response to 0 at center
            : 1.0;

        // Basic spring-damper: v += (k*e - c*v) * dt ; y += v * dt
        const double accel = static_cast<double>(k) * e * bandScale
            - static_cast<double>(p.damping) * st.v;
        st.v += accel * static_cast<double>(dt);

        // Cap velocity for stability
        const double vmax = static_cast<double>(std::max(1e-6f, p.maxVelocity));
        if (st.v > vmax) st.v = vmax;
        if (st.v < -vmax) st.v = -vmax;

        st.y += st.v * static_cast<double>(dt);

        // Optional clamping
        if (p.clampEnabled) {
            st.y = clampd(st.y, static_cast<double>(p.clampMin), static_cast<double>(p.clampMax));
            // when clamped, also limit velocity to not fly away
            if (st.y <= p.clampMin + 1e-6) st.v = std::min(st.v, 0.0);
            if (st.y >= p.clampMax - 1e-6) st.v = std::max(st.v, 0.0);
        }
    }

    double RecoveryInterfold::Step(float dt, double x)
    {
        std::lock_guard<std::mutex> lk(_mx);

        if (!_p.enabled) {
            // pass-through
            _s.y = x;
            _s.v = 0.0;
            _s.seeded = true;
            _s.cooldown = 0.0;
            return _s.y;
        }

        if (_p.abideEmptiness) {
            _s.y = 0.0;
            _s.v = 0.0;
            _s.seeded = true;
            _s.cooldown = 0.0;
            return _s.y;
        }

        if (!_s.seeded) {
            _s.seeded = true;
            if (_p.snapFirstSample) {
                _s.y = x;
                _s.v = 0.0;
                _s.cooldown = 0.0;
                return _s.y;
            }
            // else: start from zero and let dynamics take over
            _s.y = 0.0;
            _s.v = 0.0;
            _s.cooldown = 0.0;
        }

        // Cooldown trigger on large jumps
        const double jumpMag = std::fabs(x - _s.y);
        if (jumpMag > static_cast<double>(_p.jumpThreshold)) {
            _s.cooldown = std::max(_s.cooldown, static_cast<double>(_p.cooldownSeconds));
        }

        integrateStep(_s, _p, dt, x);
        return _s.y;
    }

    double RecoveryInterfold::PeekNext(float dt, double x) const
    {
        std::lock_guard<std::mutex> lk(_mx);

        if (!_p.enabled) return x;
        if (_p.abideEmptiness) return 0.0;

        State sim = _s;
        if (!sim.seeded) {
            if (_p.snapFirstSample) return x;
            // else simulate from zero
            sim.seeded = true;
            sim.y = 0.0;
            sim.v = 0.0;
            sim.cooldown = 0.0;
        }

        const double jumpMag = std::fabs(x - sim.y);
        if (jumpMag > static_cast<double>(_p.jumpThreshold)) {
            sim.cooldown = std::max(sim.cooldown, static_cast<double>(_p.cooldownSeconds));
        }

        integrateStep(sim, _p, dt, x);
        return sim.y;
    }

    // ----------------- State control -----------------

    void RecoveryInterfold::Reset()
    {
        std::lock_guard<std::mutex> lk(_mx);
        _s = State{};
    }

    void RecoveryInterfold::HardReset(double value)
    {
        std::lock_guard<std::mutex> lk(_mx);
        _s = State{};
        _s.y = value;
        _s.seeded = true;
    }

    void RecoveryInterfold::BeginCooldown(float seconds)
    {
        std::lock_guard<std::mutex> lk(_mx);
        _s.cooldown = std::max(_s.cooldown, static_cast<double>(std::max(0.0f, seconds)));
    }

} // namespace MB
