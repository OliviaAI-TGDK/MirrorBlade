#include "TGDKFigure8Fold.hpp"
#include <algorithm>
#include <limits>

namespace MB {

    // ----------------- Utility -----------------

    static inline float wrap_phase(float a) {
        // Wrap to [-pi, pi] to keep numbers tame (not strictly required)
        constexpr float TWO_PI = Figure8Fold::kTwoPi();
        a = std::fmod(a, TWO_PI);
        if (a > 3.14159265358979323846f) a -= TWO_PI;
        if (a < -3.14159265358979323846f) a += TWO_PI;
        return a;
    }

    static MB::Figure8Fold fig({ .type = MB::Figure8Fold::Type::Lissajous12,
                             .amplitudeX = 0.5f, .amplitudeY = 0.5f,
                             .speedHz = 0.33f, .smoothingAlpha = 1.0f });
    auto [jx, jy] = fig.Advance(deltaTime);
    g_params.jitterX = jx;
    g_params.jitterY = jy;


    // ----------------- Public API -----------------

    Figure8Fold::Figure8Fold() = default;

    Figure8Fold::Figure8Fold(const Params& p) : _p(p) {}

    void Figure8Fold::SetParams(const Params& p) {
        std::lock_guard<std::mutex> lk(_mx);
        _p = p;
        _haveHistory = false; // reset smoothing on parameter jump
    }

    Figure8Fold::Params Figure8Fold::GetParams() const {
        std::lock_guard<std::mutex> lk(_mx);
        return _p;
    }

    void Figure8Fold::SetType(Type t) {
        std::lock_guard<std::mutex> lk(_mx);
        _p.type = t;
        _haveHistory = false;
    }

    void Figure8Fold::SetAmplitude(float ax, float ay) {
        std::lock_guard<std::mutex> lk(_mx);
        _p.amplitudeX = ax;
        _p.amplitudeY = ay;
    }

    void Figure8Fold::SetSpeed(float hz) {
        std::lock_guard<std::mutex> lk(_mx);
        _p.speedHz = std::max(hz, 0.0f);
    }

    void Figure8Fold::SetPhase(float phx, float phy) {
        std::lock_guard<std::mutex> lk(_mx);
        _p.phaseX = wrap_phase(phx);
        _p.phaseY = wrap_phase(phy);
    }

    void Figure8Fold::SetCenter(float cx, float cy) {
        std::lock_guard<std::mutex> lk(_mx);
        _p.centerX = cx;
        _p.centerY = cy;
    }

    void Figure8Fold::SetSmoothing(float alpha01) {
        std::lock_guard<std::mutex> lk(_mx);
        _p.smoothingAlpha = clamp01(alpha01);
        // leave history; caller may want continuous smoothing behavior
    }

    void Figure8Fold::Reset(float timeSec) {
        std::lock_guard<std::mutex> lk(_mx);
        _timeSec = timeSec;
        _haveHistory = false;
        _lastX = _lastY = 0.0f;
    }

    std::pair<float, float> Figure8Fold::Advance(float dtSec) {
        std::lock_guard<std::mutex> lk(_mx);
        // time integration
        _timeSec += std::max(0.0f, dtSec);

        const float w = kTwoPi() * _p.speedHz;

        // raw shape in local space
        std::pair<float, float> p;
        switch (_p.type) {
        case Type::Lissajous12:
            p = EvalLissajous12(_timeSec, _p.amplitudeX, _p.amplitudeY, w, _p.phaseX, _p.phaseY);
            break;
        case Type::LemniscateBernoulli: {
            // We use amplitudeX as overall scale; amplitudeY is ignored for this shape by design.
            const float scale = (_p.amplitudeX + _p.amplitudeY) * 0.5f;
            p = EvalLemniscateBernoulli(_timeSec * w, scale);
            break;
        }
        default:
            p = { 0.0f, 0.0f };
            break;
        }

        // add center offset
        p.first += _p.centerX;
        p.second += _p.centerY;

        // apply smoothing (exponential)
        const float a = clamp01(_p.smoothingAlpha);
        if (!_haveHistory) {
            _lastX = p.first;
            _lastY = p.second;
            _haveHistory = true;
        }
        else {
            _lastX = a * p.first + (1.0f - a) * _lastX;
            _lastY = a * p.second + (1.0f - a) * _lastY;
        }
        return { _lastX, _lastY };
    }

    std::pair<float, float> Figure8Fold::SampleAt(float timeSec) const {
        std::lock_guard<std::mutex> lk(_mx);

        const float w = kTwoPi() * _p.speedHz;
        std::pair<float, float> p;

        switch (_p.type) {
        case Type::Lissajous12:
            p = EvalLissajous12(timeSec, _p.amplitudeX, _p.amplitudeY, w, _p.phaseX, _p.phaseY);
            break;
        case Type::LemniscateBernoulli: {
            const float scale = (_p.amplitudeX + _p.amplitudeY) * 0.5f;
            p = EvalLemniscateBernoulli(timeSec * w, scale);
            break;
        }
        default:
            p = { 0.0f, 0.0f };
            break;
        }

        p.first += _p.centerX;
        p.second += _p.centerY;
        return p; // unsmoothed sample
    }

    std::pair<float, float> Figure8Fold::Current() const {
        std::lock_guard<std::mutex> lk(_mx);
        return { _lastX, _lastY };
    }

    Figure8Fold::State Figure8Fold::GetState() const {
        std::lock_guard<std::mutex> lk(_mx);
        return State{ _p, _timeSec, _lastX, _lastY };
    }

    // ----------------- Math backends -----------------

    std::pair<float, float>
        Figure8Fold::EvalLissajous12(float t, float ax, float ay, float w, float phx, float phy)
    {
        // Lissajous with frequency ratio 1:2 (figure 8)
        const float x = ax * std::sinf(w * t + phx);
        const float y = ay * std::sinf(2.0f * w * t + phy);
        return { x, y };
    }

    std::pair<float, float>
        Figure8Fold::EvalLemniscateBernoulli(float theta, float scale)
    {
        // Parametric Bernoulli lemniscate (well-behaved, no sqrt of negative):
        // x = (a * cos θ) / (1 + sin^2 θ)
        // y = (a * sin θ * cos θ) / (1 + sin^2 θ)
        // where a controls size; here we use 'scale'.
        const float s = std::sinf(theta);
        const float c = std::sinf(theta + kPi() * 0.5f); // alternative cos via phase shift to keep periodicity precise
        // but let's compute real cos for clarity and accuracy:
        const float cc = std::cosf(theta);
        const float denom = 1.0f + s * s;
        const float inv = (denom > std::numeric_limits<float>::epsilon()) ? (1.0f / denom) : 0.0f;

        const float x = scale * (cc)*inv;
        const float y = scale * (s * cc) * inv;
        return { x, y };
    }

    std::pair<float, float>
        Figure8Fold::ApplyTransform(std::pair<float, float> p, float ax, float ay, float cx, float cy)
    {
        return { p.first * ax + cx, p.second * ay + cy };
    }

} // namespace MB
