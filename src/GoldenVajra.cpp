#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <utility>

namespace MB {

    // GoldenVajra: low-discrepancy jitter/sequencer using a golden-ratio Kronecker sequence.
    // - Thread-safe
    // - Deterministic (seeded)
    // - Configurable frequency (steps per second), amplitude, and temporal smoothing.
    //
    // Typical use:
    //   MB::GoldenVajra gv;
    //   gv.Configure({true, 0.75f, 60.0f, 0.9f, 0xDEADBEEF});
    //   each frame: gv.Tick(dt);
    //   float jx, jy; gv.GetJitter(jx, jy);  // use as subpixel jitter, etc.
    class GoldenVajra {
    public:
        struct Params {
            bool      enabled = true;   // master enable
            float     amplitude = 1.0f;   // 0..inf (scales jitter range [-0.5,0.5])
            float     frequency = 60.0f;  // steps per second (>= 0)
            float     temporalBlend = 0.90f;  // 0..1 (higher = smoother)
            std::uint64_t seed = 0;      // sequence seed
        };

        GoldenVajra();

        // Configure current parameters (thread-safe). Values are clamped to sane ranges.
        void   Configure(const Params& p);

        // Reset to initial state (phase/index/jitter = 0)
        void   Reset();

        // Advance internal time and update jitter (thread-safe)
        void   Tick(double dtSeconds);

        // Current params (by value, thread-safe)
        Params Get() const;

        // Current jitter (by value, thread-safe)
        void   GetJitter(float& outX, float& outY) const;

        // Current sample of the underlying 2D low-discrepancy sequence (unscaled, in [0,1) range)
        std::pair<float, float> Sample2D() const;

        // Lightweight JSON snapshot as a string (no external JSON dependency)
        std::string SnapshotJSON() const;

    private:
        // Internal: compute the nth 2D Kronecker sample for this seed (u,v in [0,1))
        static std::pair<float, float> KRSequence2D(std::uint64_t index, std::uint64_t seed);

        static float Clamp01(float v);
        static float Fract(float v);

        mutable std::mutex _mx;

        Params       _params{};
        double       _timeAcc{ 0.0 };
        std::uint64_t _index{ 0 };

        float        _jx{ 0.0f };
        float        _jy{ 0.0f };
    };

} // namespace MB
