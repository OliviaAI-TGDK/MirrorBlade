#include "VolumetricInfinitizer.hpp"
#include <cmath>

namespace MB {

    // ---------- Halton utilities ----------

    float VolumetricInfinitizer::halton(uint32_t i, uint32_t base) {
        // Radical inverse
        float f = 1.0f;
        float r = 0.0f;
        while (i) {
            f /= static_cast<float>(base);
            r += f * static_cast<float>(i % base);
            i /= base;
        }
        return r;
    }

    std::pair<float, float> VolumetricInfinitizer::Halton23(uint32_t index) {
        // Common trick: skip index 0 to avoid (0,0)
        const uint32_t k = index + 1u;
        return { halton(k, 2u), halton(k, 3u) };
    }

    // ---------- Ctors ----------

    VolumetricInfinitizer::VolumetricInfinitizer() = default;

    VolumetricInfinitizer::VolumetricInfinitizer(const Params& p) : _p(p) {}

    // ---------- Param setters/getters ----------

    void VolumetricInfinitizer::SetParams(const Params& p) {
        std::lock_guard<std::mutex> lk(_mx);
        _p = p;
    }

    VolumetricInfinitizer::Params VolumetricInfinitizer::GetParams() const {
        std::lock_guard<std::mutex> lk(_mx);
        return _p;
    }

    void VolumetricInfinitizer::SetEnabled(bool on) {
        std::lock_guard<std::mutex> lk(_mx);
        _p.enabled = on;
    }

    void VolumetricInfinitizer::SetDistanceMul(float v) {
        std::lock_guard<std::mutex> lk(_mx);
        _p.distanceMul = std::max(0.0f, v);
    }

    void VolumetricInfinitizer::SetDensityMul(float v) {
        std::lock_guard<std::mutex> lk(_mx);
        _p.densityMul = std::max(0.0f, v);
    }

    void VolumetricInfinitizer::SetHorizonFade(float v01) {
        std::lock_guard<std::mutex> lk(_mx);
        _p.horizonFade = clamp01(v01);
    }

    void VolumetricInfinitizer::SetJitterStrength(float v) {
        std::lock_guard<std::mutex> lk(_mx);
        _p.jitterStrength = std::max(0.0f, v);
    }

    void VolumetricInfinitizer::SetTemporalBlend(float v01) {
        std::lock_guard<std::mutex> lk(_mx);
        _p.temporalBlend = clamp01(v01);
    }

    // ---------- Simulation ----------

    void VolumetricInfinitizer::Reset(float timeSec) {
        std::lock_guard<std::mutex> lk(_mx);
        _s.timeSec = std::max(0.0f, timeSec);
        _s.frame = 0;
        _s.jitterX = 0.0f;
        _s.jitterY = 0.0f;
    }

    void VolumetricInfinitizer::Advance(float dtSec) {
        std::lock_guard<std::mutex> lk(_mx);

        // Time/frame update
        _s.timeSec += std::max(0.0f, dtSec);
        _s.frame++;

        // New jitter (centered in [-0.5, 0.5]) scaled by strength
        const auto [h2, h3] = Halton23(_s.frame);
        const float jx = (h2 - 0.5f) * _p.jitterStrength;
        const float jy = (h3 - 0.5f) * _p.jitterStrength;
        _s.jitterX = jx;
        _s.jitterY = jy;
    }

    VolumetricInfinitizer::State VolumetricInfinitizer::GetState() const {
        std::lock_guard<std::mutex> lk(_mx);
        return _s;
    }

    std::pair<float, float> VolumetricInfinitizer::CurrentJitter() const {
        std::lock_guard<std::mutex> lk(_mx);
        return { _s.jitterX, _s.jitterY };
    }

    // ---------- GPU constants ----------

    VolumetricInfinitizer::ShaderConstants VolumetricInfinitizer::GetShaderConstants() const {
        std::lock_guard<std::mutex> lk(_mx);
        ShaderConstants c{};
        c.distanceMul = _p.enabled ? _p.distanceMul : 1.0f;
        c.densityMul = _p.enabled ? _p.densityMul : 1.0f;
        c.horizonFade = _p.horizonFade;
        c.temporalBlend = _p.temporalBlend;
        c.jitter[0] = _p.enabled ? _s.jitterX : 0.0f;
        c.jitter[1];
    }
}