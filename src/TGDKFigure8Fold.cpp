#include "TGDKFigure8Fold.hpp"
#include <cmath>
#include <algorithm>

namespace MB {

    // ---- Lissajous (generalized) ------------------------------------------------
    std::pair<float, float> Figure8Fold::EvalLissajous12(double t,
        double ax, double ay,
        double nx, double ny,
        double phase) noexcept
    {
        const double a = t * kTwoPi; // treat t as normalized [0,1]
        const float  x = static_cast<float>(ax * std::sin(nx * a + phase));
        const float  y = static_cast<float>(ay * std::sin(ny * a));
        return { x, y };
    }

    std::pair<float, float> Figure8Fold::EvalLissajous12(double t,
        const Params& p) noexcept
    {
        return EvalLissajous12(t, p.ax, p.ay, p.nx, p.ny, p.phase);
    }

    // ---- Lemniscate of Bernoulli (robust parametrization) -----------------------
    std::pair<float, float> Figure8Fold::EvalLemniscateBernoulli(double t,
        double A) noexcept
    {
        // Normalize t to angle u
        const double u = t * kTwoPi;
        const double s = std::sin(u);
        const double c = std::cos(u);
        const double d = 1.0 + s * s; // always >= 1, avoids division spikes
        const float  x = static_cast<float>((A * c) / d);
        const float  y = static_cast<float>((A * s * c) / d);
        return { x, y };
    }

    std::pair<float, float> Figure8Fold::EvalLemniscateBernoulli(double t,
        const Params& p) noexcept
    {
        return EvalLemniscateBernoulli(t, p.A);
    }

    // ---- Selector ----------------------------------------------------------------
    std::pair<float, float> Figure8Fold::Evaluate(double t, const Params& p) noexcept
    {
        if (p.mode == Params::Mode::LemniscateBernoulli)
            return EvalLemniscateBernoulli(t, p);
        return EvalLissajous12(t, p);
    }

    // ---- Stateful helpers --------------------------------------------------------
    void Figure8Fold::SetParams(const Params& p) { _p = p; }
    Figure8Fold::Params Figure8Fold::GetParams() const { return _p; }

    void Figure8Fold::Reset(float t) { _t = t; }

    void Figure8Fold::Advance(float dt)
    {
        // Use 'phase' as angular speed scale; feel free to swap for an explicit omega.
        _t += dt * static_cast<float>(_p.phase);
    }

    float Figure8Fold::WrapAngle(float t) const
    {
        // Wrap a float angle using double precision constant
        float a = static_cast<float>(std::fmod(static_cast<double>(t), kTwoPi));
        if (a < 0.0f) a += static_cast<float>(kTwoPi);
        return a;
    }

    std::pair<float, float> Figure8Fold::Current() const
    {
        return Evaluate(_t, _p);
    }

} // namespace MB
