#pragma once

#include <mutex>
#include <utility>
#include <cmath>
#include <nlohmann/json.hpp>

namespace MB {

    class Detox {
    public:
        using json = nlohmann::json;

        struct Params {
            bool  enabled = true;
            bool  abideEmptiness = false;
            float deflectGain = 1.0f;
            float intersectThresh = 0.5f;  // gate threshold for intercede
            float postOpsWeight = 0.5f;  // blend weight scale
            float detailEmphasis = 1.0f;  // gate steepness
            float specimenTension = 0.5f;  // fold tension
        };

        struct DeflectInput {
            float density01 = 0.0f;  // 0..1
            float avgSpeed = 0.0f;  // arbitrary units
            float refSpeed = 20.0f; // normalization reference (>0)
        };

        struct ChartPoint {
            float x = 0.0f;          // density01 clamped
            float y = 0.0f;          // normalized speed clamped
            float deflection = 0.0f; // signed bias
        };

        struct IntercedeResult {
            double value = 0.0; // blended value
            double proportion = 0.0; // blend factor after gate
            bool   gated = false;
        };

        struct FoldResult {
            float specimen = 0.0f;  // S-shaped fold amount
            float curvature = 0.0f;  // crude curvature proxy
        };

        Detox() = default;

        // Params
        void   SetParams(const Params& p);
        Params GetParams() const;

        // JSON I/O
        void ConfigureFromJSON(const json& j);
        json SnapshotJSON() const;

        // Core evaluations
        ChartPoint       EvaluateDeflection(const DeflectInput& in) const;
        IntercedeResult  Intercede(double base, double post, double detail) const;
        FoldResult       FoldSpecimen(float t) const;

        // Tag used in telemetry/snapshots
        static const char* ContractorId() { return "Detox"; }

    private:
        static inline float  clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
        static inline double sigmoid(double x) { return 1.0 / (1.0 + std::exp(-x)); }

    private:
        mutable std::mutex _mx;
        Params _p{};
    };

} // namespace MB
