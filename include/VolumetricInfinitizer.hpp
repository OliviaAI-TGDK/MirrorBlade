#pragma once
#include <cstdint>
#include <utility>
#include <mutex>
#include <algorithm>

namespace MB {

    /**
     * VolumetricInfinitizer
     * ---------------------
     * Small runtime that tweaks volumetric fog/lighting to feel “infinite”
     * by (a) scaling effective march distance/density, (b) applying horizon
     * fade, and (c) injecting a blue-noise-like subpixel jitter via Halton(2,3).
     *
     * Feed GetShaderConstants() into your root constants/CBV and sample the
     * jitter in your ray-march to break banding and stabilize TAA.
     */
    class VolumetricInfinitizer {
    public:
        struct Params {
            bool  enabled = false; // master enable
            float distanceMul = 1.0f;  // scales ray-march max distance
            float densityMul = 1.0f;  // multiplies media density
            float horizonFade = 0.25f; // 0..1 -> amount of fade near horizon
            float jitterStrength = 1.0f;  // 0..N sub-pixel jitter amplitude
            float temporalBlend = 0.90f; // 0..1 history weight in shader
        };

        // Matches HLSL packing (16-byte friendly)
        struct ShaderConstants {
            float distanceMul;     // x
            float densityMul;      // y
            float horizonFade;     // z
            float temporalBlend;   // w
            float jitter[2];       // xy subpixel offset (in pixels or NDC as you decide)
            float _pad[2];         // keep 16B alignment
        };

        struct State {
            float    timeSec = 0.0f;
            uint32_t frame = 0;
            float    jitterX = 0.0f;
            float    jitterY = 0.0f;
        };

    public:
        VolumetricInfinitizer();
        explicit VolumetricInfinitizer(const Params& p);

        // Configuration (thread-safe)
        void   SetParams(const Params& p);
        Params GetParams() const;

        void SetEnabled(bool on);
        void SetDistanceMul(float v);
        void SetDensityMul(float v);
        void SetHorizonFade(float v01);
        void SetJitterStrength(float v);
        void SetTemporalBlend(float v01);

        // Simulation (thread-safe)
        void   Reset(float timeSec = 0.0f);
        void   Advance(float dtSec);               // increments time/frame + updates jitter
        State  GetState() const;
        std::pair<float, float> CurrentJitter() const;

        // GPU constants (thread-safe snapshot)
        ShaderConstants GetShaderConstants() const;

        // Utility: Halton 2/3 jitter pair in [0,1)
        static std::pair<float, float> Halton23(uint32_t index);

    private:
        static float clamp01(float v) { return std::max(0.0f, std::min(1.0f, v)); }
        static float halton(uint32_t i, uint32_t base);

    private:
        mutable std::mutex _mx;
        Params _p{};
        State  _s{};
    };

} // namespace MB
