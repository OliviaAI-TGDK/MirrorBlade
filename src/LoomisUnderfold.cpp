// src/LoomisUnderfold.cpp
#include "LoomisUnderfold.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>
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

#include <nlohmann/json.hpp>

namespace MB {

    using json = nlohmann::json;

    LoomisUnderfold::LoomisUnderfold()
        : _curve(Curve::Smooth) {
    }

    // ----------------- Small helpers -----------------
    double LoomisUnderfold::saturate(double v) {
        return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
    }
    double LoomisUnderfold::smoothstep01(double t) {
        t = saturate(t);
        return t * t * (3.0 - 2.0 * t); // 3t^2 - 2t^3
    }
    double LoomisUnderfold::hermite01(double t) {
        t = saturate(t);
        // 6t^5 - 15t^4 + 10t^3 (smoothstep^2 generalization); we use 1 - poly for a peaked kernel
        double t2 = t * t;
        double t3 = t2 * t;
        double t4 = t3 * t;
        double t5 = t4 * t;
        double poly = (6.0 * t5 - 15.0 * t4 + 10.0 * t3);
        return 1.0 - poly;
    }
    bool LoomisUnderfold::validName(std::string_view n) {
        if (n.empty()) return false;
        for (char c : n) {
            if (!((c >= 'a' && c <= 'z') ||
                (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') ||
                c == '_' || c == '-' || c == '.')) return false;
        }
        return true;
    }

    // Kernel selection -------------------------------------------------
    double LoomisUnderfold::kernel(double t) const {
        if (t >= 1.0) return 0.0;
        switch (_curve) {
        case Curve::Linear:  return 1.0 - t;
        case Curve::Smooth:  return 1.0 - smoothstep01(t);
        case Curve::Cosine:  return 0.5 * (1.0 + std::cos(M_PI * saturate(t)));
        case Curve::Hermite: return hermite01(t);
        default:             return 1.0 - t;
        }
    }

    double LoomisUnderfold::kernel_deriv_dt(double t) const {
        if (t < 0.0 || t > 1.0) return 0.0;
        switch (_curve) {
        case Curve::Linear:
            return -1.0;
        case Curve::Smooth: {
            // d/dt [1 - smoothstep(t)] = - d/dt [3t^2 - 2t^3] = - (6t - 6t^2) = 6(t^2 - t)
            return 6.0 * (t * t - t);
        }
        case Curve::Cosine:
            // d/dt [0.5*(1 + cos(pi t))] = -0.5*pi*sin(pi t)
            return -0.5 * M_PI * std::sin(M_PI * t);
        case Curve::Hermite: {
            // d/dt [1 - (6t^5 -15t^4 +10t^3)] = -(30t^4 - 60t^3 + 30t^2)
            double t2 = t * t;
            double t3 = t2 * t;
            double t4 = t3 * t;
            return -(30.0 * t4 - 60.0 * t3 + 30.0 * t2);
        }
        default:
            return -1.0;
        }
    }

    // ----------------- Curve control -----------------
    void LoomisUnderfold::SetCurve(Curve c) {
        std::lock_guard<std::mutex> lk(_mx);
        _curve = c;
    }

    LoomisUnderfold::Curve LoomisUnderfold::GetCurve() const {
        std::lock_guard<std::mutex> lk(_mx);
        return _curve;
    }

    // ----------------- CRUD -----------------
    void LoomisUnderfold::Clear() {
        std::lock_guard<std::mutex> lk(_mx);
        _creases.clear();
    }

    bool LoomisUnderfold::Upsert(const Crease& c) {
        if (!validName(c.name) || !(c.radius > 0.0)) return false;
        std::lock_guard<std::mutex> lk(_mx);
        // overwrite by name if exists
        for (auto& it : _creases) {
            if (it.name == c.name) { it = c; return true; }
        }
        _creases.push_back(c);
        return true;
    }

    bool LoomisUnderfold::Remove(std::string_view name) {
        std::lock_guard<std::mutex> lk(_mx);
        auto it = std::remove_if(_creases.begin(), _creases.end(),
            [&](const Crease& c) { return c.name == name; });
        bool removed = it != _creases.end();
        _creases.erase(it, _creases.end());
        return removed;
    }

    bool LoomisUnderfold::Enable(std::string_view name, bool on) {
        std::lock_guard<std::mutex> lk(_mx);
        for (auto& c : _creases) if (c.name == name) { c.enabled = on; return true; }
        return false;
    }

    bool LoomisUnderfold::SetPriority(std::string_view name, int p) {
        std::lock_guard<std::mutex> lk(_mx);
        for (auto& c : _creases) if (c.name == name) { c.priority = p; return true; }
        return false;
    }

    bool LoomisUnderfold::SetGain(std::string_view name, double g) {
        std::lock_guard<std::mutex> lk(_mx);
        for (auto& c : _creases) if (c.name == name) { c.gain = g; return true; }
        return false;
    }

    bool LoomisUnderfold::SetRadius(std::string_view name, double r) {
        if (!(r > 0.0)) return false;
        std::lock_guard<std::mutex> lk(_mx);
        for (auto& c : _creases) if (c.name == name) { c.radius = r; return true; }
        return false;
    }

    bool LoomisUnderfold::SetPosition(std::string_view name, double x) {
        std::lock_guard<std::mutex> lk(_mx);
        for (auto& c : _creases) if (c.name == name) { c.pos = x; return true; }
        return false;
    }

    bool LoomisUnderfold::Exists(std::string_view name) const {
        std::lock_guard<std::mutex> lk(_mx);
        for (const auto& c : _creases) if (c.name == name) return true;
        return false;
    }

    bool LoomisUnderfold::IsEnabled(std::string_view name) const {
        std::lock_guard<std::mutex> lk(_mx);
        for (const auto& c : _creases) if (c.name == name) return c.enabled;
        return false;
    }

    std::vector<LoomisUnderfold::Crease> LoomisUnderfold::List() const {
        std::lock_guard<std::mutex> lk(_mx);
        return _creases;
    }

    // ----------------- Evaluation -----------------
    void LoomisUnderfold::snapshot(std::vector<Crease>& out, Curve& curve) const {
        std::lock_guard<std::mutex> lk(_mx);
        curve = _curve;
        out.clear();
        out.reserve(_creases.size());
        for (const auto& c : _creases) if (c.enabled) out.push_back(c);
        std::sort(out.begin(), out.end(), [](const Crease& a, const Crease& b) {
            if (a.priority != b.priority) return a.priority < b.priority;
            return a.name < b.name;
            });
    }

    double LoomisUnderfold::Evaluate(double x) const {
        std::vector<Crease> cs;
        Curve curve;
        snapshot(cs, curve);

        double y = x;
        for (const auto& c : cs) {
            double dx = y - c.pos;
            double t = std::fabs(dx) / c.radius;   // normalized distance
            if (t >= 1.0) continue;
            // Temporarily switch to this object curve for kernel()
            // We need a const_cast to reuse kernel; safer: compute inline
            double K;
            switch (curve) {
            case Curve::Linear:  K = (t >= 1.0) ? 0.0 : (1.0 - t); break;
            case Curve::Smooth:  K = (t >= 1.0) ? 0.0 : (1.0 - smoothstep01(t)); break;
            case Curve::Cosine:  K = (t >= 1.0) ? 0.0 : (0.5 * (1.0 + std::cos(M_PI * t))); break;
            case Curve::Hermite: K = (t >= 1.0) ? 0.0 : (hermite01(t)); break;
            default:             K = (t >= 1.0) ? 0.0 : (1.0 - t); break;
            }
            y = y + c.gain * K * (c.pos - y);
        }
        return y;
    }

    double LoomisUnderfold::EvaluateDelta(double x) const {
        return Evaluate(x) - x;
    }

    double LoomisUnderfold::EvaluateDerivative(double x) const {
        std::vector<Crease> cs;
        Curve curve;
        snapshot(cs, curve);

        double y = x;
        double dy = 1.0; // running derivative dy/dx

        for (const auto& c : cs) {
            const double dx = y - c.pos;
            const double absdx = std::fabs(dx);
            const double t = absdx / c.radius;
            if (t >= 1.0) continue;

            // Kernel and dK/dt
            double K, dKdt;
            switch (curve) {
            case Curve::Linear:  K = 1.0 - t; dKdt = -1.0; break;
            case Curve::Smooth:  K = 1.0 - smoothstep01(t); dKdt = 6.0 * (t * t - t); break;
            case Curve::Cosine:  K = 0.5 * (1.0 + std::cos(M_PI * t)); dKdt = -0.5 * M_PI * std::sin(M_PI * t); break;
            case Curve::Hermite: K = hermite01(t); dKdt = -(30.0 * t * t * t * t - 60.0 * t * t * t + 30.0 * t * t); break;
            default:             K = 1.0 - t; dKdt = -1.0; break;
            }

            // sign for dt/dy
            const double sgn = (dx >= 0.0) ? 1.0 : -1.0;
            const double dtdy = (absdx > 0.0) ? (sgn / c.radius) : 0.0;
            const double dKdy = dKdt * dtdy;

            // y_next = y + g * K(y) * (p - y)
            // Local derivative L = d(y_next)/d(y)
            //   = 1 + g * ( dKdy*(p - y) + K * d(p - y)/dy )
            //   = 1 + g * ( dKdy*(p - y) - K )
            const double L = 1.0 + c.gain * (dKdy * (c.pos - y) - K);

            // Chain rule
            dy *= L;

            // Update y (must use current y for next crease)
            y = y + c.gain * K * (c.pos - y);
        }

        return dy;
    }

    void LoomisUnderfold::EvaluateMany(const double* xs, double* out, size_t n) const {
        if (!xs || !out || n == 0) return;

        std::vector<Crease> cs;
        Curve curve;
        snapshot(cs, curve);

        for (size_t i = 0; i < n; ++i) {
            double y = xs[i];
            for (const auto& c : cs) {
                double dx = y - c.pos;
                double t = std::fabs(dx) / c.radius;
                if (t >= 1.0) continue;
                double K;
                switch (curve) {
                case Curve::Linear:  K = 1.0 - t; break;
                case Curve::Smooth:  K = 1.0 - smoothstep01(t); break;
                case Curve::Cosine:  K = 0.5 * (1.0 + std::cos(M_PI * t)); break;
                case Curve::Hermite: K = hermite01(t); break;
                default:             K = 1.0 - t; break;
                }
                y = y + c.gain * K * (c.pos - y);
            }
            out[i] = y;
        }
    }

    // ----------------- JSON IO -----------------
    static LoomisUnderfold::Curve parseCurve(std::string v) {
        // lowercase
        for (auto& c : v) c = char(std::tolower(unsigned char(c)));
        if (v == "linear")  return LoomisUnderfold::Curve::Linear;
        if (v == "smooth")  return LoomisUnderfold::Curve::Smooth;
        if (v == "cosine")  return LoomisUnderfold::Curve::Cosine;
        if (v == "hermite") return LoomisUnderfold::Curve::Hermite;
        return LoomisUnderfold::Curve::Smooth;
    }
    static const char* curveName(LoomisUnderfold::Curve c) {
        switch (c) {
        case LoomisUnderfold::Curve::Linear:  return "linear";
        case LoomisUnderfold::Curve::Smooth:  return "smooth";
        case LoomisUnderfold::Curve::Cosine:  return "cosine";
        case LoomisUnderfold::Curve::Hermite: return "hermite";
        }
        return "smooth";
    }

    bool LoomisUnderfold::ConfigureFromJSON(const std::string& jsonText, std::string* errorOut) {
        try {
            json j = json::parse(jsonText);
            if (!j.is_object()) {
                if (errorOut) *errorOut = "root is not an object";
                return false;
            }

            const bool replace = j.value("replace", false);
            if (replace) Clear();

            if (j.contains("curve")) {
                SetCurve(parseCurve(j.value("curve", std::string("smooth"))));
            }

            if (j.contains("creases") && j["creases"].is_array()) {
                for (const auto& e : j["creases"]) {
                    if (!e.is_object()) continue;
                    Crease c;
                    c.name = e.value("name", std::string());
                    c.pos = e.value("pos", 0.0);
                    c.radius = e.value("radius", 1.0);
                    c.gain = e.value("gain", 0.5);
                    c.priority = e.value("priority", 0);
                    c.enabled = e.value("enabled", true);

                    if (!validName(c.name)) {
                        MBLOGW("LoomisUnderfold: invalid crease name '%s' (skipped)", c.name.c_str());
                        continue;
                    }
                    if (!(c.radius > 0.0)) {
                        MBLOGW("LoomisUnderfold: radius<=0 for '%s' (skipped)", c.name.c_str());
                        continue;
                    }
                    Upsert(c);
                }
            }

            return true;
        }
        catch (const std::exception& e) {
            if (errorOut) *errorOut = e.what();
            return false;
        }
        catch (...) {
            if (errorOut) *errorOut = "unknown error";
            return false;
        }
    }

    std::string LoomisUnderfold::SnapshotJSON() const {
        json j;
        {
            std::lock_guard<std::mutex> lk(_mx);
            j["curve"] = curveName(_curve);
            j["creases"] = json::array();
            for (const auto& c : _creases) {
                j["creases"].push_back(json{
                    {"name", c.name},
                    {"pos", c.pos},
                    {"radius", c.radius},
                    {"gain", c.gain},
                    {"priority", c.priority},
                    {"enabled", c.enabled}
                    });
            }
        }
        return j.dump(2);
    }

} // namespace MB
