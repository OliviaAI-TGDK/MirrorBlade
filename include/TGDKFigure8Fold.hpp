#pragma once
#include <atomic>
#include <cstdint>
#include <cmath>
#include <mutex>
#include <utility>

namespace MB {

    /**
     * Figure8Fold
     * -----------
     * Deterministic, time-stepped generator of smooth figure-8 trajectories.
     *
     * Use cases:
     *  - Camera debug or test motion
     *  - Temporal jitter paths (feed into your upscaler/jitter)
     *  - UI animation paths
     *
     * Thread safety:
     *  - All public methods are internally synchronized.
     *  - Sampling / stepping does not allocate.
     */
    class Figure8Fold {
    public:
        enum class Type : std::uint8_t {
            Lissajous12,          // x = Ax * sin(ω t + φx), y = Ay * sin(2 ω t + φy)
            LemniscateBernoulli   // classic lemniscate (Bernoulli), area-normalized
        };

        struct Params {
            Type   type = Type::Lissajous12;
            float  amplitudeX = 1.0f;   // logical units
            float  amplitudeY = 1.0f;   // logical units
            float  speedHz = 0.25f;  // base cycles per second (ω = 2π f)
            float  phaseX = 0.0f;   // radians
            float  phaseY = 0.0f;   // radians (for Lissajous)
            float  centerX = 0.0f;   // offset
            float  centerY = 0.0f;   // offset
            float  smoothingAlpha = 1.0f;   // 0..1, 1 = no smoothing; 0.1 = heavy smoothing
        };

        struct State {
            Params params;   // current parameters (snapshot)
            float  timeSec;  // accumulated time
            float  x, y;     // last output (after smoothing)
        };

    public:
        Figure8Fold();
        explicit Figure8Fold(const Params& p);

        // Configure
        void SetParams(const Params& p);
        Params GetParams() const;

        void SetType(Type t);
        void SetAmplitude(float ax, float ay);
        void SetSpeed(float hz);
        void SetPhase(float phx, float phy);
        void SetCenter(float cx, float cy);
        void SetSmoothing(float alpha01);

        // Control
        void Reset(float timeSec = 0.0f); // clears smoothing history too
        // Advance time and return the new (x,y)
        std::pair<float, float> Advance(float dtSec);
        // Sample at absolute time (does not change internal time or smoothing)
        std::pair<float, float> SampleAt(float timeSec) const;
        // Current smoothed output (does not advance time)
        std::pair<float, float> Current() const;

        // Inspect
        State GetState() const;

    private:
        static constexpr float kPi() { return 3.14159265358979323846f; }
        static constexpr float kTwoPi() { return 6.2831853071795864769f; }
        static float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

        // Shape evaluators (unsmoothed, centered at origin, then scaled/offset)
        static std::pair<float, float> EvalLissajous12(float t, float ax, float ay, float w, float phx, float phy);
        static std::pair<float, float> EvalLemniscateBernoulli(float t, float scale);

        // Applies amplitude & center to a unit pair
        static std::pair<float, float> ApplyTransform(std::pair<float, float> p, float ax, float ay, float cx, float cy);

    private:
        mutable std::mutex _mx;
        Params _p;
        float  _timeSec = 0.0f;
        bool   _haveHistory = false;   // for smoothing reset
        float  _lastX = 0.0f;
        float  _lastY = 0.0f;
    };

} // namespace MB
