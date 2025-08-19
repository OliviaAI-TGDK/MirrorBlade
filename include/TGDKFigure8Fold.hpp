#pragma once
#include <utility>
#include <cstdint>

namespace MB {

    class Figure8Fold {
    public:
        // Public constants (no private-access error)
        inline static constexpr double kTwoPi = 6.28318530717958647692;

        struct Params {
            enum class Mode { Lissajous12, LemniscateBernoulli };

            // Which curve to evaluate
            Mode mode = Mode::Lissajous12;

            // Lissajous params
            double ax = 1.0;
            double ay = 1.0;
            double nx = 1.0;
            double ny = 2.0;
            double phase = 0.0;

            // Bernoulli param
            double A = 1.0;
        };

        Figure8Fold() = default;

        // Static evaluators used by ops (match call sites in TGDKOps)
        static std::pair<float, float> EvalLissajous12(double t,
            double ax, double ay,
            double nx, double ny,
            double phase) noexcept;

        static std::pair<float, float> EvalLissajous12(double t,
            const Params& p) noexcept;

        static std::pair<float, float> EvalLemniscateBernoulli(double t,
            double A) noexcept;

        static std::pair<float, float> EvalLemniscateBernoulli(double t,
            const Params& p) noexcept;

        // Utility: choose curve based on Params::mode
        static std::pair<float, float> Evaluate(double t, const Params& p) noexcept;

        // Stateful helper API (optional)
        void SetParams(const Params& p);
        Params GetParams() const;

        void Reset(float t = 0.0f);
        void Advance(float dt);                 // advances internal phase/time
        float WrapAngle(float t) const;         // wrap to [0, 2π)
        std::pair<float, float> Current() const; // Evaluate(_t, _p)
        std::pair<float, float> Evaluate(float t) const { return Evaluate(static_cast<double>(t), _p); }

    private:
        Params _p{};
        float  _t = 0.0f;
    };

} // namespace MB
