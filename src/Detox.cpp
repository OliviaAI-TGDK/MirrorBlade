#include "Detox.hpp"

#include <algorithm>
#include <cmath>

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

    // ------------------- Param setters/getters -------------------

    void Detox::SetParams(const Params& p) {
        std::lock_guard<std::mutex> lk(_mx);
        _p = p;
    }

    Detox::Params Detox::GetParams() const {
        std::lock_guard<std::mutex> lk(_mx);
        return _p;
    }

    // ------------------- JSON I/O -------------------

    void Detox::ConfigureFromJSON(const json& j) {
        if (!j.is_object()) return;
        std::lock_guard<std::mutex> lk(_mx);

        if (j.contains("enabled"))         _p.enabled = j.value("enabled", _p.enabled);
        if (j.contains("abideEmptiness"))  _p.abideEmptiness = j.value("abideEmptiness", _p.abideEmptiness);
        if (j.contains("deflectGain"))     _p.deflectGain = std::max(0.0f, j.value("deflectGain", _p.deflectGain));
        if (j.contains("intersectThresh")) _p.intersectThresh = j.value("intersectThresh", _p.intersectThresh);
        if (j.contains("postOpsWeight"))   _p.postOpsWeight = clamp01(j.value("postOpsWeight", _p.postOpsWeight));
        if (j.contains("detailEmphasis"))  _p.detailEmphasis = std::max(0.0f, j.value("detailEmphasis", _p.detailEmphasis));
        if (j.contains("specimenTension")) _p.specimenTension = clamp01(j.value("specimenTension", _p.specimenTension));
    }

    Detox::json Detox::SnapshotJSON() const {
        std::lock_guard<std::mutex> lk(_mx);
        return json{
            {"contractor",      ContractorId()},
            {"enabled",         _p.enabled},
            {"abideEmptiness",  _p.abideEmptiness},
            {"deflectGain",     _p.deflectGain},
            {"intersectThresh", _p.intersectThresh},
            {"postOpsWeight",   _p.postOpsWeight},
            {"detailEmphasis",  _p.detailEmphasis},
            {"specimenTension", _p.specimenTension}
        };
    }

    // ------------------- Core evaluations -------------------

    // Map traffic inputs to a chart (x=density, y=normalized speed), compute a signed deflection.
    // Deflection grows when density is high and speed is low (or vice-versa), scaled by deflectGain.
    Detox::ChartPoint Detox::EvaluateDeflection(const DeflectInput& in) const {
        std::lock_guard<std::mutex> lk(_mx);
        ChartPoint cp{};
        if (!_p.enabled || _p.abideEmptiness || in.refSpeed <= 0.0f) {
            return cp; // zeros
        }

        const float x = clamp01(in.density01);
        const float y = clamp01(in.avgSpeed / std::max(1e-3f, in.refSpeed)); // normalize speed

        // Centered deltas around 0.5
        const float dx = x - 0.5f;
        const float dy = y - 0.5f;

        // Signed deflection: oriented cross-like measure plus mild radial lift
        const float radial = std::sqrt(dx * dx + dy * dy);
        float defl = (dx * (0.5f - y) - dy * (0.5f - x));
        defl *= (0.75f + 0.25f * radial);
        defl *= _p.deflectGain;

        cp.x = x;
        cp.y = y;
        cp.deflection = defl;
        return cp;
    }

    // Blend a post-ops value into a base value, proportioned by detail via a sigmoid gate.
    // Gate = sigmoid(detailEmphasis*(detail - intersectThresh))
    // Result = lerp(base, post, postOpsWeight * Gate)
    Detox::IntercedeResult Detox::Intercede(double base, double post, double detail) const {
        std::lock_guard<std::mutex> lk(_mx);
        IntercedeResult r{};
        if (!_p.enabled || _p.abideEmptiness) {
            r.value = base; r.proportion = 0.0; r.gated = false;
            return r;
        }

        const double gate = sigmoid(static_cast<double>(_p.detailEmphasis) *
            (detail - static_cast<double>(_p.intersectThresh)));
        const double w = std::clamp(static_cast<double>(_p.postOpsWeight) * gate, 0.0, 1.0);

        r.value = base + (post - base) * w;
        r.proportion = w;
        r.gated = (gate < 0.999); // true if gate meaningfully affected the blend
        return r;
    }

    // Parametric folding specimen: smooth S-shaped fold modulated by tension.
    // When abideEmptiness is true, returns zeros.
    Detox::FoldResult Detox::FoldSpecimen(float t) const {
        std::lock_guard<std::mutex> lk(_mx);
        FoldResult fr{};
        if (!_p.enabled || _p.abideEmptiness) {
            return fr; // zeros
        }

        const float tt = clamp01(t);

        // Smoothstep base curve
        const float s = tt * tt * (3.0f - 2.0f * tt);

        // Tension bends the S toward steeper mid curvature
        const float k = clamp01(_p.specimenTension);
        const float bend = s + k * (s - s * s); // emphasize mid region

        fr.specimen = bend;

        // A crude curvature proxy (non-analytic, but monotone enough for tuning)
        fr.curvature = std::fabs(
            6.0f * tt - 6.0f * tt * tt +
            k * (1.0f - 2.0f * s) * (2.0f * tt * (3.0f - 2.0f * tt) + s * (-4.0f * tt + 3.0f))
        );

        return fr;
    }

} // namespace MB
