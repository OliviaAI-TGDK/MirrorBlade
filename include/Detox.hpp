#pragma once

#include <mutex>
#include <string>
#include <optional>
#include <utility>
#include <vector>
#include <cstdint>
#include <algorithm>

// Forward-declare nlohmann::json to keep the header light.
#include <nlohmann/json_fwd.hpp>

namespace MB {

    // WolfRecluse.xlm contractor – Detox
    // A small “inspiration mod” that:
    // 1) Deflects traffic metrics onto a 2D chart and computes a deflection scalar.
    // 2) Intersects post-op values to intercede via proportioning (detail emphasis).
    // 3) Provides a folding specimen path (can abide in emptiness).
    class Detox {
    public:
        struct Params {
            bool   enabled{ true };   // master toggle
            bool   abideEmptiness{ false };  // when true, fold to neutral/zeroed responses
            float  deflectGain{ 1.0f };   // scale of traffic deflection
            float  intersectThresh{ 0.0f };   // threshold for intercession gate
            float  postOpsWeight{ 0.5f };   // blend weight for post-ops proportioning [0..1]
            float  detailEmphasis{ 1.0f };   // amplifies detail magnitude in gating
            float  specimenTension{ 0.5f };   // folding tension [0..1]
        };

        struct ChartPoint {
            float x{ 0.f };   // chart abscissa (e.g., density01)
            float y{ 0.f };   // chart ordinate (e.g., normalized speed)
            float deflection{ 0.f }; // signed deflection on the chart
        };

        struct IntercedeResult {
            double value{ 0.0 };      // blended result
            double proportion{ 0.0 };  // proportion applied from post value
            bool   gated{ false };     // true if gate/threshold affected blend
        };

        struct FoldResult {
            float specimen{ 0.f };     // folded output
            float curvature{ 0.f };    // auxiliary curvature measure
        };

        struct DeflectInput {
            float density01{ 0.f };   // [0..1]
            float avgSpeed{ 0.f };    // world units/s
            float refSpeed{ 20.f };   // reference speed for normalization (>0)
        };

    public:
        Detox() = default;

        // Identity for diagnostics/ops
        static constexpr const char* ContractorId() { return "WolfRecluse.xlm"; }

        // Thread-safe setters/getters
        void  SetParams(const Params& p);
        Params GetParams() const;

        // JSON I/O
        void ConfigureFromJSON(const nlohmann::json& j);
        nlohmann::json SnapshotJSON() const;

        // 1) Deflect traffic onto the chart; produce point + deflection.
        ChartPoint EvaluateDeflection(const DeflectInput& in) const;

        // 2) Intercede through post-ops proportioning with detail emphasis.
        // base: upstream value, post: downstream/post-ops candidate, detail: a magnitude to emphasize
        IntercedeResult Intercede(double base, double post, double detail) const;

        // 3) Folding specimen: parametric fold on t in [0..1]
        // If abideEmptiness==true -> returns zeros (abide in emptiness).
        FoldResult FoldSpecimen(float t) const;

    private:
        // helpers (all assume lock is held or params captured)
        static float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }
        static double sigmoid(double x) { return 1.0 / (1.0 + std::exp(-x)); }

    private:
        mutable std::mutex _mx;
        Params _p{};
    };

    // Optional: ops registration helper (safe to call even if MBOps isn’t present).
    void RegisterDetoxOpsIfAvailable();

} // namespace MB
