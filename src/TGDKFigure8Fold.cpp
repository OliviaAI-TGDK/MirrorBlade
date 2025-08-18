#include "TGDKFigure8Fold.hpp"
#include <cmath>
#include <algorithm>

namespace MB {

    void Figure8Fold::SetParams(const Params& p) {
        _p = p;
    }

    Figure8Fold::Params Figure8Fold::GetParams() const {
        return _p;
    }

    void Figure8Fold::Reset(float t) {
        _t = t;
    }

    void Figure8Fold::Advance(float dt) {
        _t += dt * _p.speed;
    }

    float Figure8Fold::WrapAngle(float t) const {
        float a = std::fmod(t, kTwoPi);
        if (a < 0.0f) a += kTwoPi;
        return a;
    }

    std::pair<float, float> Figure8Fold::Current() const {
        return Evaluate(_t);
    }

    std::pair<float, float> Figure8Fold::Evaluate(float t) const {
        // Default = Lissajous (1,2): x = sin(a), y = sin(2a)
        return EvalLissajous12(t);
    }

    std::pair<float, float> Figure8Fold::EvalLissajous12(float t) const {
        float a = WrapAngle(t) + _p.phase;

        // Safe aspect (avoid divide-by-zero)
        const float aspect = (_p.aspect == 0.0f) ? 1.0f : _p.aspect;

        const float x = _p.radius * std::sinf(a);
        const float y = (_p.radius / aspect) * std::sinf(2.0f * a);
        return { x, y };
    }

    std::pair<float, float> Figure8Fold::EvalLemniscateBernoulli(float t) const {
        // Lemniscate of Bernoulli using a stable rational parametrization:
        //   x = a * cos u / (1 + sin^2 u)
        //   y = a * cos u * sin u / (1 + sin^2 u)
        // where 'a' is scale (here: radius), and we apply vertical aspect scaling.
        float u = WrapAngle(t) + _p.phase;

        const float s = std::sinf(u);
        const float c = std::cosf(u);
        const float den = 1.0f + s * s; // always >= 1

        const float a = _p.radius;
        const float asp = (_p.aspect == 0.0f) ? 1.0f : _p.aspect;

        const float x = (a * c) / den;
        const float y = (a / asp) * (c * s) / den;

        return { x, y };
    }

} // namespace MB
