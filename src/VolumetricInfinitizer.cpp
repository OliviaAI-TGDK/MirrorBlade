#include "VolumetricInfinitizer.hpp"
#include <algorithm>
#include <cmath>

namespace MB {

    float VolumetricInfinitizer::halton(uint32_t i, uint32_t base) {
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
        const uint32_t k = index + 1u; // avoid (0,0)
        return { halton(k, 2u), halton(k, 3u) };
    }

    VolumetricInfinitizer::VolumetricInfinitizer() = default;

    VolumetricInfinitizer::VolumetricInfinitizer(const Params& p)
        : _p(p) {
    }

    void VolumetricInfinitizer::SetParams(const Params& p) {
        std::lock_guard<std::mutex> lk(_mx);
        _p = p;
        _p.horizonFade = clamp01(_p.horizonFade);
        _p.temporalBlend = clamp01(_p.temporalBlend);
        _p.distanceMul = std::max(0.0f, _p.distanceMul);
        _p.densityMul = std::max(0.0f, _p.densityMul);
        _p.jitterStrength = std::max(0.0f, _p.jitterStrength);
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

    void VolumetricInfinitizer::Reset(float timeSec) {
        std::lock_guard<std::mutex> lk(_mx);
        _s.timeSec = std::max(0.0f, timeSec);
        _s.frame = 0u;
        _s.jitterX = 0.0f;
        _s.jitterY = 0.0f;
    }

    void VolumetricInfinitizer::Advance(float dtSec) {
        std::lock_guard<std::mutex> lk(_mx);
        _s.timeSec += std::max(0.0f, dtSec);
        _s.frame++;

        const auto h = Halton23(_s.frame);
        _s.jitterX = (h.first - 0.5f) * _p.jitterStrength;
        _s.jitterY = (h.second - 0.5f) * _p.jitterStrength;
    }

    VolumetricInfinitizer::State VolumetricInfinitizer::GetState() const {
        std::lock_guard<std::mutex> lk(_mx);
        return _s;
    }

    std::pair<float, float> VolumetricInfinitizer::CurrentJitter() const {
        std::lock_guard<std::mutex> lk(_mx);
        return { _s.jitterX, _s.jitterY };
    }

    VolumetricInfinitizer::ShaderConstants
        VolumetricInfinitizer::GetShaderConstants() const {
        std::lock_guard<std::mutex> lk(_mx);

        ShaderConstants c{};
        c.distanceMul = _p.distanceMul;
        c.densityMul = _p.densityMul;
        c.horizonFade = _p.horizonFade;
        c.temporalBlend = _p.temporalBlend;
        c.enabled = _p.enabled ? 1u : 0u;
        c.jitterX = _s.jitterX;
        c.jitterY = _s.jitterY;
        return c; // always returns a value
    }

} // namespace MB
