#pragma once

#include <cstdint>
#include <mutex>
#include <utility>

namespace MB {

    class VolumetricInfinitizer {
    public:
        struct Params {
            bool  enabled = true;
            float distanceMul = 1.0f;
            float densityMul = 1.0f;
            float horizonFade = 0.5f;  // [0,1]
            float jitterStrength = 0.0f;  // screen-space jitter scale
            float temporalBlend = 0.9f;  // [0,1]
        };

        struct State {
            float    timeSec = 0.0f;
            uint32_t frame = 0;
            float    jitterX = 0.0f;
            float    jitterY = 0.0f;
        };

        // Pack exactly what your shader expects. Add padding if your layout needs it.
        struct ShaderConstants {
            float    distanceMul = 1.0f;
            float    densityMul = 1.0f;
            float    horizonFade = 0.5f;
            float    temporalBlend = 0.9f;
            uint32_t enabled = 1u;
            float    jitterX = 0.0f;
            float    jitterY = 0.0f;
            float    _pad = 0.0f; // keeps 16B alignment if needed
        };

    public:
        VolumetricInfinitizer();
        explicit VolumetricInfinitizer(const Params& p);

        // Params
        void   SetParams(const Params& p);
        Params GetParams() const;

        void SetEnabled(bool on);
        void SetDistanceMul(float v);
        void SetDensityMul(float v);
        void SetHorizonFade(float v01);
        void SetJitterStrength(float v);
        void SetTemporalBlend(float v01);

        // Simulation
        void  Reset(float timeSec = 0.0f);
        void  Advance(float dtSec);
        State GetState() const;
        std::pair<float, float> CurrentJitter() const;

        // GPU constants snapshot (thread-safe)
        ShaderConstants GetShaderConstants() const;

        // Low-discrepancy sampling helpers
        static float halton(uint32_t i, uint32_t base);
        static std::pair<float, float> Halton23(uint32_t index);

    private:
        static float clamp01(float v) {
            return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
        }

    private:
        mutable std::mutex _mx;
        Params _p{};
        State  _s{};
    };

} // namespace MB
