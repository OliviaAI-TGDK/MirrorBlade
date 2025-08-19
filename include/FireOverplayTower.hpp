// include/FireOverplayTower.hpp
#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <mutex>

namespace MB {

    // FireOverplayTower
    // ------------------
    // Manages a set of named "overplay" layers that modulate a base scalar.
    // Each layer has: name, priority, enabled flag, and a weight.
    // Evaluate() applies enabled layers in ascending priority order:
    //     out = base * (prod of weights for enabled layers in order)
    //
    // JSON helpers (implemented in .cpp with nlohmann::json):
    //   ConfigureFromJSON(text[, err])
    //     {
    //       "replace": true|false,      // optional; default=false (merge/update)
    //       "layers": [
    //         {"name":"fog","priority":10,"enabled":true,"weight":0.9},
    //         {"name":"heat","priority":5,"enabled":true,"weight":1.1}
    //       ]
    //     }
    //
    //   SnapshotJSON() -> canonical JSON describing all layers.
    class FireOverplayTower {
    public:
        struct LayerDesc {
            std::string name;
            int         priority = 0;
            bool        enabled = true;
            double      weight = 1.0;
        };

        FireOverplayTower() = default;

        // Basic CRUD
        void Clear();
        bool Upsert(const LayerDesc& d);                 // add or replace by name
        bool Remove(std::string_view name);
        bool Enable(std::string_view name, bool on);
        bool SetPriority(std::string_view name, int p);
        bool SetWeight(std::string_view name, double w);

        // Query
        std::vector<LayerDesc> List() const;             // all layers (unsorted)
        bool Exists(std::string_view name) const;
        bool IsEnabled(std::string_view name) const;

        // Evaluate composite effect on a base value.
        double Evaluate(double base) const;

        // JSON helpers (implemented in .cpp; no json headers here)
        bool ConfigureFromJSON(const std::string& jsonText, std::string* errorOut = nullptr);
        std::string SnapshotJSON() const;

    private:
        mutable std::mutex _mx;
        std::unordered_map<std::string, LayerDesc> _layers; // keyed by name
    };

} // namespace MB
