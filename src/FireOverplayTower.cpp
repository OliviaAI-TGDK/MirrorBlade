// src/FireOverplayTower.cpp
#include "FireOverplayTower.hpp"

#include <algorithm>
#include <sstream>

#include <nlohmann/json.hpp>

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

    // -----------------
    // Internal helpers
    // -----------------
    static bool validName(std::string_view s) {
        if (s.empty()) return false;
        for (char c : s) {
            if (!((c >= 'a' && c <= 'z')
                || (c >= 'A' && c <= 'Z')
                || (c >= '0' && c <= '9')
                || c == '_' || c == '-' || c == '.')) return false;
        }
        return true;
    }

    // --------------
    // Basic CRUD
    // --------------
    void FireOverplayTower::Clear() {
        std::lock_guard<std::mutex> lk(_mx);
        _layers.clear();
    }

    bool FireOverplayTower::Upsert(const LayerDesc& d) {
        if (!validName(d.name)) return false;
        std::lock_guard<std::mutex> lk(_mx);
        _layers[d.name] = d;
        return true;
    }

    bool FireOverplayTower::Remove(std::string_view name) {
        std::lock_guard<std::mutex> lk(_mx);
        return _layers.erase(std::string(name)) > 0;
    }

    bool FireOverplayTower::Enable(std::string_view name, bool on) {
        std::lock_guard<std::mutex> lk(_mx);
        auto it = _layers.find(std::string(name));
        if (it == _layers.end()) return false;
        it->second.enabled = on;
        return true;
    }

    bool FireOverplayTower::SetPriority(std::string_view name, int p) {
        std::lock_guard<std::mutex> lk(_mx);
        auto it = _layers.find(std::string(name));
        if (it == _layers.end()) return false;
        it->second.priority = p;
        return true;
    }

    bool FireOverplayTower::SetWeight(std::string_view name, double w) {
        std::lock_guard<std::mutex> lk(_mx);
        auto it = _layers.find(std::string(name));
        if (it == _layers.end()) return false;
        it->second.weight = w;
        return true;
    }

    std::vector<FireOverplayTower::LayerDesc> FireOverplayTower::List() const {
        std::lock_guard<std::mutex> lk(_mx);
        std::vector<LayerDesc> v;
        v.reserve(_layers.size());
        for (const auto& kv : _layers) v.push_back(kv.second);
        return v;
    }

    bool FireOverplayTower::Exists(std::string_view name) const {
        std::lock_guard<std::mutex> lk(_mx);
        return _layers.find(std::string(name)) != _layers.end();
    }

    bool FireOverplayTower::IsEnabled(std::string_view name) const {
        std::lock_guard<std::mutex> lk(_mx);
        auto it = _layers.find(std::string(name));
        return it != _layers.end() && it->second.enabled;
    }

    // -----------------------------
    // Evaluate(base) -> combined
    // -----------------------------
    double FireOverplayTower::Evaluate(double base) const {
        std::vector<LayerDesc> sorted;
        {
            std::lock_guard<std::mutex> lk(_mx);
            sorted.reserve(_layers.size());
            for (const auto& kv : _layers) {
                if (kv.second.enabled) sorted.push_back(kv.second);
            }
        }
        std::sort(sorted.begin(), sorted.end(),
            [](const LayerDesc& a, const LayerDesc& b) {
                if (a.priority != b.priority) return a.priority < b.priority;
                return a.name < b.name;
            });

        double v = base;
        for (const auto& l : sorted) {
            v *= l.weight;
        }
        return v;
    }

    // -----------------------------
    // JSON configure / snapshot
    // -----------------------------
    bool FireOverplayTower::ConfigureFromJSON(const std::string& jsonText, std::string* errorOut) {
        try {
            json j = json::parse(jsonText);
            if (!j.is_object()) {
                if (errorOut) *errorOut = "root is not an object";
                return false;
            }

            const bool replace = j.value("replace", false);
            if (replace) Clear();

            if (j.contains("layers")) {
                const json& arr = j["layers"];
                if (!arr.is_array()) {
                    if (errorOut) *errorOut = "'layers' is not an array";
                    return false;
                }
                for (const auto& el : arr) {
                    if (!el.is_object()) continue;
                    LayerDesc d;
                    d.name = el.value("name", std::string());
                    d.priority = el.value("priority", 0);
                    d.enabled = el.value("enabled", true);
                    d.weight = el.value("weight", 1.0);
                    if (!validName(d.name)) {
                        MBLOGW("FireOverplayTower: invalid layer name skipped");
                        continue;
                    }
                    Upsert(d);
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

    std::string FireOverplayTower::SnapshotJSON() const {
        json j;
        j["layers"] = json::array();

        {
            std::lock_guard<std::mutex> lk(_mx);
            for (const auto& kv : _layers) {
                const auto& d = kv.second;
                j["layers"].push_back(json{
                    {"name", d.name},
                    {"priority", d.priority},
                    {"enabled", d.enabled},
                    {"weight", d.weight}
                    });
            }
        }
        return j.dump(2);
    }

} // namespace MB
