// src/AILLTUO.cpp
#include "AILLTUO.hpp"
#include "LoomisUnderfold.hpp"

#include <algorithm>
#include <cmath>
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

    // ====================== GentuoLM ======================
    GentuoLM::GentuoLM()
        : _state(0x9e3779b97f4a7c15ULL) // golden ratio seed
    {
        _affirm = { "okay", "right", "sure", "fine", "got it" };
        _skeptic = { "nah", "hm", "nope", "maybe not", "not sure" };
        _connective = { "and", "but", "though", "still", "meanwhile" };
        _traffic = { "flow", "merge", "grid", "signal", "detour" };
    }

    void GentuoLM::SetSeed(uint64_t seed) { _state = seed ? seed : 0x9e3779b97f4a7c15ULL; }
    void GentuoLM::SetAffirmations(std::vector<std::string> words) { _affirm = std::move(words); }
    void GentuoLM::SetSkeptics(std::vector<std::string> words) { _skeptic = std::move(words); }
    void GentuoLM::SetConnectives(std::vector<std::string> words) { _connective = std::move(words); }
    void GentuoLM::SetTrafficTerms(std::vector<std::string> words) { _traffic = std::move(words); }

    uint64_t GentuoLM::nextRand() const {
        // xorshift64*
        uint64_t x = _state;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        _state = x;
        return x * 2685821657736338717ULL;
    }
    size_t GentuoLM::pickIndex(size_t n) const {
        if (n == 0) return 0;
        return static_cast<size_t>(nextRand() % n);
    }

    std::string GentuoLM::GenerateUtterance(std::string_view npcName,
        double mood,
        const std::unordered_map<std::string, double>& env) const
    {
        // Derive a deterministic seed from npcName + a couple env scalars.
        uint64_t seed = 1469598103934665603ULL; // FNV offset
        for (char c : npcName) { seed ^= static_cast<unsigned char>(c); seed *= 1099511628211ULL; }
        if (auto it = env.find("traffic_density"); it != env.end()) {
            uint64_t bits = static_cast<uint64_t>((it->second + 1.0) * 100000.0);
            seed ^= bits + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
        }
        if (auto it = env.find("avg_speed"); it != env.end()) {
            uint64_t bits = static_cast<uint64_t>((it->second + 1.0) * 100000.0);
            seed ^= bits + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
        }
        SetSeed(seed);

        const bool positive = (mood >= 0.0);
        const std::string& tone = positive ? _affirm[pickIndex(_affirm.size())]
            : _skeptic[pickIndex(_skeptic.size())];

        const std::string& conn = _connective[pickIndex(_connective.size())];
        const std::string& flow = _traffic[pickIndex(_traffic.size())];

        // Pull in a couple of coarse env descriptors
        const double dens = (env.count("traffic_density") ? env.at("traffic_density") : 0.4);
        const double spd = (env.count("avg_speed") ? env.at("avg_speed") : 8.0);

        std::ostringstream oss;
        oss << tone << ", " << npcName << " says " << conn << " the " << flow
            << " feels ";
        if (dens > 0.75)      oss << "tight";
        else if (dens > 0.5)  oss << "busy";
        else if (dens > 0.25) oss << "loose";
        else                  oss << "clear";

        oss << " at " << (int)std::round(spd) << " speed.";
        return oss.str();
    }

    // ====================== AILLTUO ======================
    AILLTUO::AILLTUO()
        : _params(), _underfold(nullptr), _gentuo()
    {
    }

    void AILLTUO::SetUnderfold(const LoomisUnderfold* uf) {
        std::lock_guard<std::mutex> lk(_mx);
        _underfold = uf;
    }

    void AILLTUO::SetParams(const Params& p) {
        std::lock_guard<std::mutex> lk(_mx);
        _params = p;
    }

    AILLTUO::Params AILLTUO::GetParams() const {
        std::lock_guard<std::mutex> lk(_mx);
        return _params;
    }

    double AILLTUO::clamp(double v, double lo, double hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }
    double AILLTUO::sign(double v) {
        return (v > 0.0) - (v < 0.0);
    }
    double AILLTUO::crooked(double d, double k) {
        // A gentle odd nonlinearity that biases away from the origin as |d| grows.
        // y = d + k * d * |d|
        return d + k * d * std::abs(d);
    }

    AILLTUO::NPCOffset AILLTUO::EvaluateNPCOffset(double x) const {
        const LoomisUnderfold* uf;
        Params p;
        {
            std::lock_guard<std::mutex> lk(_mx);
            uf = _underfold;
            p = _params;
        }
        if (!p.enabled || !uf) return {};

        double y = uf->Evaluate(x);
        double d = y - x;
        double td = clamp(d, -p.truncation, p.truncation);
        double co = crooked(td, p.crookedness);
        return { td, co };
    }

    void AILLTUO::EvaluateNPCOffsetsMany(const double* xs, double* out, size_t n) const {
        if (!xs || !out || n == 0) return;
        const LoomisUnderfold* uf;
        Params p;
        {
            std::lock_guard<std::mutex> lk(_mx);
            uf = _underfold;
            p = _params;
        }
        if (!p.enabled || !uf) {
            if (out != xs) std::fill(out, out + n, 0.0);
            return;
        }

        for (size_t i = 0; i < n; ++i) {
            double x = xs[i];
            double y = uf->Evaluate(x);
            double d = y - x;
            double td = clamp(d, -p.truncation, p.truncation);
            out[i] = crooked(td, p.crookedness);
        }
    }

    AILLTUO::TrafficDecision AILLTUO::EvaluateTraffic(double density01, double avgSpeed) const {
        Params p;
        {
            std::lock_guard<std::mutex> lk(_mx);
            p = _params;
        }
        if (!p.enabled) return {};

        double d = clamp(density01, 0.0, 1.0);

        // Thoughtfulness: when dense, slow slightly and increase headway;
        // when sparse, allow mild speed-up and reduce spacing a bit.
        double slow = 0.10 * p.trafficThoughtFactor;    // up to 10% slow-down
        double fast = 0.05 * p.trafficThoughtFactor;    // up to 5% speed-up
        double spaceGrow = 0.20 * p.trafficThoughtFactor;
        double spaceShrink = 0.10 * p.trafficThoughtFactor;

        double speedMul = (d * (1.0 - slow) + (1.0 - d) * (1.0 + fast));
        double spacingMul = (d * (1.0 + spaceGrow) + (1.0 - d) * (1.0 - spaceShrink));

        // Light smoothing so it doesn't overshoot wildly for extreme speeds
        if (avgSpeed > 40.0) speedMul = std::min(speedMul, 1.0); // don't accelerate at high speed
        return { speedMul, spacingMul };
    }

    std::string AILLTUO::GenerateNPCUtterance(std::string_view npcName,
        double mood,
        const std::unordered_map<std::string, double>& env) const
    {
        Params p;
        {
            std::lock_guard<std::mutex> lk(_mx);
            p = _params;
        }
        if (!p.enabled) return std::string(npcName) + " is quiet.";

        // Chance to emit an utterance (deterministic via name/env in Gentuo)
        const double w = clamp(p.dialecticWeight, 0.0, 1.0);
        // Always generate here for simplicity; higher-level systems can throttle if needed.
        return _gentuo.GenerateUtterance(npcName, clamp(mood, -1.0, 1.0), env);
    }

    bool AILLTUO::ConfigureFromJSON(const std::string& jsonText, std::string* errorOut) {
        try {
            nlohmann::json j = nlohmann::json::parse(jsonText);
            if (!j.is_object()) {
                if (errorOut) *errorOut = "root is not an object";
                return false;
            }

            Params p;
            {
                std::lock_guard<std::mutex> lk(_mx);
                p = _params;
            }

            if (j.contains("enabled"))              p.enabled = j.value("enabled", p.enabled);
            if (j.contains("truncation"))           p.truncation = std::max(0.0, j.value("truncation", p.truncation));
            if (j.contains("crookedness"))          p.crookedness = std::max(0.0, j.value("crookedness", p.crookedness));
            if (j.contains("dialecticWeight"))      p.dialecticWeight = clamp(j.value("dialecticWeight", p.dialecticWeight), 0.0, 1.0);
            if (j.contains("trafficThoughtFactor")) p.trafficThoughtFactor = clamp(j.value("trafficThoughtFactor", p.trafficThoughtFactor), 0.0, 1.0);

            if (j.contains("gentuo") && j["gentuo"].is_object()) {
                const auto& g = j["gentuo"];
                if (g.contains("affirm") && g["affirm"].is_array()) {
                    std::vector<std::string> v;
                    for (const auto& s : g["affirm"]) if (s.is_string()) v.push_back(s.get<std::string>());
                    if (!v.empty()) _gentuo.SetAffirmations(std::move(v));
                }
                if (g.contains("skeptic") && g["skeptic"].is_array()) {
                    std::vector<std::string> v;
                    for (const auto& s : g["skeptic"]) if (s.is_string()) v.push_back(s.get<std::string>());
                    if (!v.empty()) _gentuo.SetSkeptics(std::move(v));
                }
                if (g.contains("connect") && g["connect"].is_array()) {
                    std::vector<std::string> v;
                    for (const auto& s : g["connect"]) if (s.is_string()) v.push_back(s.get<std::string>());
                    if (!v.empty()) _gentuo.SetConnectives(std::move(v));
                }
                if (g.contains("traffic") && g["traffic"].is_array()) {
                    std::vector<std::string> v;
                    for (const auto& s : g["traffic"]) if (s.is_string()) v.push_back(s.get<std::string>());
                    if (!v.empty()) _gentuo.SetTrafficTerms(std::move(v));
                }
            }

            SetParams(p);
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

    std::string AILLTUO::SnapshotJSON() const {
        nlohmann::json j;
        Params p;
        {
            std::lock_guard<std::mutex> lk(_mx);
            p = _params;
        }
        j["enabled"] = p.enabled;
        j["truncation"] = p.truncation;
        j["crookedness"] = p.crookedness;
        j["dialecticWeight"] = p.dialecticWeight;
        j["trafficThoughtFactor"] = p.trafficThoughtFactor;
        // Wordbanks not serialized here to keep snapshot compact.
        return j.dump(2);
    }

} // namespace MB
