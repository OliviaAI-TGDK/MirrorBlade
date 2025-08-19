#include "LoomisUnderfold.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <nlohmann/json.hpp>

namespace MB {

    using json = nlohmann::json;

    static inline double pi_const() {
        // Portable pi without relying on M_PI
        return std::acos(-1.0);
    }

    LoomisUnderfold::LoomisUnderfold()
        : _curve(Curve::Smooth) // sensible default
    {
    }

    void LoomisUnderfold::SetCurve(Curve c) {
        std::lock_guard<std::mutex> lk(_mx);
        _curve = c;
    }

    LoomisUnderfold::Curve LoomisUnderfold::GetCurve() const {
        std::lock_guard<std::mutex> lk(_mx);
        return _curve;
    }

    void LoomisUnderfold::Clear() {
        std::lock_guard<std::mutex> lk(_mx);
        _creases.clear();
    }

    bool LoomisUnderfold::Upsert(const Crease& c) {
        if (c.radius <= 0.0 || !validName(c.name)) return false;
        std::lock_guard<std::mutex> lk(_mx);
        auto it = std::find_if(_creases.begin(), _creases.end(),
            [&](const Crease& k) { return k.name == c.name; });
        if (it == _creases.end()) _creases.push_back(c);
        else *it = c;
        return true;
    }

    bool LoomisUnderfold::Remove(std::string_view name) {
        std::lock_guard<std::mutex> lk(_mx);
        auto oldSize = _creases.size();
        _creases.erase(std::remove_if(_creases.begin(), _creases.end(),
            [&](const Crease& k) { return k.name == name; }),
            _creases.end());
        return _creases.size() != oldSize;
    }

    bool LoomisUnderfold::Enable(std::string_view name, bool on) {
        std::lock_guard<std::mutex> lk(_mx);
        for (auto& k : _creases) if (k.name == name) { k.enabled = on; return true; }
        return false;
    }

    bool LoomisUnderfold::SetPriority(std::string_view name, int p) {
        std::lock_guard<std::mutex> lk(_mx);
        for (auto& k : _creases) if (k.name == name) { k.priority = p; return true; }
        return false;
    }

    bool LoomisUnderfold::SetGain(std::string_view name, double g) {
        std::lock_guard<std::mutex> lk(_mx);
        for (auto& k : _creases) if (k.name == name) { k.gain = g; return true; }
        return false;
    }

    bool LoomisUnderfold::SetRadius(std::string_view name, double r) {
        if (r <= 0.0) return false;
        std::lock_guard<std::mutex> lk(_mx);
        for (auto& k : _creases) if (k.name == name) { k.radius = r; return true; }
        return false;
    }

    bool LoomisUnderfold::SetPosition(std::string_view name, double x) {
        std::lock_guard<std::mutex> lk(_mx);
        for (auto& k : _creases) if (k.name == name) { k.pos = x; return true; }
        return false;
    }

    bool LoomisUnderfold::Exists(std::string_view name) const {
        std::lock_guard<std::mutex> lk(_mx);
        for (const auto& k : _creases) if (k.name == name) return true;
        return false;
    }

    bool LoomisUnderfold::IsEnabled(std::string_view name) const {
        std::lock_guard<std::mutex> lk(_mx);
        for (const auto& k : _creases) if (k.name == name) return k.enabled;
        return false;
    }

    std::vector<LoomisUnderfold::Crease> LoomisUnderfold::List() const {
        std::lock_guard<std::mutex> lk(_mx);
        return _creases;
    }

    // ---------- Evaluation ----------

    void LoomisUnderfold::snapshot(std::vector<Crease>& out, Curve& curve) const {
        out.clear();
        {
            std::lock_guard<std::mutex> lk(_mx);
            out = _creases;
            curve = _curve;
        }
        std::sort(out.begin(), out.end(),
            [](const Crease& a, const Crease& b) {
                if (a.priority != b.priority) return a.priority < b.priority;
                return a.name < b.name; // stable tie-breaker
            });
    }

    double LoomisUnderfold::Evaluate(double x) const {
        std::vector<Crease> cs;
        Curve curve;
        snapshot(cs, curve);

        double y = x;
        for (const auto& c : cs) {
            if (!c.enabled || c.radius <= 0.0) continue;
            const double d = std::abs(y - c.pos);
            const double t = d / c.radius;
            if (t >= 1.0) continue;
            const double K = kernel(t);
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
        double dydx = 1.0;

        for (const auto& c : cs) {
            if (!c.enabled || c.radius <= 0.0) continue;

            const double d = std::abs(y - c.pos);
            const double t = d / c.radius;
            if (t >= 1.0) continue;

            const double u = (c.pos - y);   // = -(y - c.pos)
            const double K = kernel(t);
            const double Kd = kernel_deriv_dt(t);

            // dt/dy = sign(y - c.pos)/radius; at center use 0 to avoid NaN
            double sgn = 0.0;
            if (d > 1e-12) sgn = (y > c.pos) ? 1.0 : -1.0;
            const double dtdx = (sgn / c.radius) * dydx; // chain rule through y(x)

            // y_next = y + g*K*u, where u = (c.pos - y)
            // dy_next/dx = dy/dx + g*( K' * dtdx * u + K * du/dx )
            // du/dx = -dy/dx
            const double term = c.gain * (Kd * dtdx * u + K * (-dydx));
            dydx = dydx + term;
            y = y + c.gain * K * u;
        }
        return dydx;
    }

    void LoomisUnderfold::EvaluateMany(const double* xs, double* out, size_t n) const {
        if (!xs || !out || n == 0) return;
        if (out == xs) {
            for (size_t i = 0; i < n; ++i) out[i] = Evaluate(out[i]);
        }
        else {
            for (size_t i = 0; i < n; ++i) out[i] = Evaluate(xs[i]);
        }
    }

    // ---------- Kernels ----------

    double LoomisUnderfold::saturate(double v) {
        return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
    }

    double LoomisUnderfold::smoothstep01(double t) {
        t = saturate(t);
        return t * t * (3.0 - 2.0 * t);
    }

    double LoomisUnderfold::hermite01(double t) {
        t = saturate(t);
        // 6t^5 - 15t^4 + 10t^3
        const double t2 = t * t;
        const double t3 = t2 * t;
        const double t4 = t2 * t2;
        const double t5 = t4 * t;
        return 6.0 * t5 - 15.0 * t4 + 10.0 * t3;
    }

    double LoomisUnderfold::kernel(double t) const {
        if (t <= 0.0) return 1.0;
        if (t >= 1.0) return 0.0;

        switch (_curve) {
        default:
        case Curve::Linear:
            return 1.0 - t;

        case Curve::Smooth: {
            // K = 1 - smoothstep(t)
            const double s = smoothstep01(t);
            return 1.0 - s;
        }

        case Curve::Cosine: {
            return 0.5 * (1.0 + std::cos(pi_const() * t));
        }

        case Curve::Hermite: {
            // K = 1 - (6t^5 - 15t^4 + 10t^3)
            return 1.0 - hermite01(t);
        }
        }
    }

    double LoomisUnderfold::kernel_deriv_dt(double t) const {
        if (t <= 0.0 || t >= 1.0) return 0.0;

        switch (_curve) {
        default:
        case Curve::Linear:
            return -1.0;

        case Curve::Smooth: {
            // d/dt [1 - (3t^2 - 2t^3)] = -(6t - 6t^2)
            return -(6.0 * t - 6.0 * t * t);
        }

        case Curve::Cosine: {
            // d/dt [0.5*(1 + cos(pi t))] = -0.5*pi*sin(pi t)
            return -0.5 * pi_const() * std::sin(pi_const() * t);
        }

        case Curve::Hermite: {
            // d/dt [1 - (6t^5 - 15t^4 + 10t^3)]
            // = -(30 t^4 - 60 t^3 + 30 t^2)
            const double t2 = t * t;
            const double t3 = t2 * t;
            const double t4 = t2 * t2;
            return -(30.0 * t4 - 60.0 * t3 + 30.0 * t2);
        }
        }
    }

    // ---------- JSON I/O ----------

    static bool curve_from_string(std::string_view s, LoomisUnderfold::Curve& out) {
        if (s == "linear") { out = LoomisUnderfold::Curve::Linear;  return true; }
        if (s == "smooth") { out = LoomisUnderfold::Curve::Smooth;  return true; }
        if (s == "cosine") { out = LoomisUnderfold::Curve::Cosine;  return true; }
        if (s == "hermite") { out = LoomisUnderfold::Curve::Hermite; return true; }
        return false;
    }

    static const char* curve_to_string(LoomisUnderfold::Curve c) {
        switch (c) {
        default:
        case LoomisUnderfold::Curve::Linear:  return "linear";
        case LoomisUnderfold::Curve::Smooth:  return "smooth";
        case LoomisUnderfold::Curve::Cosine:  return "cosine";
        case LoomisUnderfold::Curve::Hermite: return "hermite";
        }
    }

    bool LoomisUnderfold::ConfigureFromJSON(const std::string& jsonText, std::string* errorOut) {
        try {
            json j = json::parse(jsonText);
            if (!j.is_object()) {
                if (errorOut) *errorOut = "root is not an object";
                return false;
            }

            bool replace = j.value("replace", false);

            Curve newCurve;
            bool hasCurve = false;
            if (j.contains("curve") && j["curve"].is_string()) {
                std::string cs = j["curve"].get<std::string>();
                std::transform(cs.begin(), cs.end(), cs.begin(),
                    [](unsigned char ch) { return std::tolower(ch); });
                if (curve_from_string(cs, newCurve)) hasCurve = true;
            }

            std::vector<Crease> add;
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
                    if (validName(c.name) && c.radius > 0.0) add.push_back(std::move(c));
                }
            }

            {
                std::lock_guard<std::mutex> lk(_mx);
                if (replace) _creases.clear();
                for (const auto& c : add) {
                    auto it = std::find_if(_creases.begin(), _creases.end(),
                        [&](const Crease& k) { return k.name == c.name; });
                    if (it == _creases.end()) _creases.push_back(c);
                    else *it = c;
                }
                if (hasCurve) _curve = newCurve;
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
            j["curve"] = curve_to_string(_curve);
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

    // ---------- misc helpers ----------

    bool LoomisUnderfold::validName(std::string_view n) {
        if (n.empty()) return false;
        for (char ch : n) {
            if (std::isalnum(static_cast<unsigned char>(ch))) continue;
            if (ch == '_' || ch == '-' || ch == '.') continue;
            return false;
        }
        return true;
    }

} // namespace MB
