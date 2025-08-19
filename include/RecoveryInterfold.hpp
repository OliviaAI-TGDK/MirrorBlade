#pragma once

#include <mutex>
#include <optional>
#include <cstdint>
#include <string>

// Forward declaration to keep headers light.
#include <nlohmann/json_fwd.hpp>

namespace MB {

    // RecoveryInterfold
    // ------------------
    // Smooths a signal with:
    //  - spring-style attraction (stiffness/damping)
    //  - hysteresis band (dead-zone)
    //  - cooldown after large jumps (reduced response)
    //  - output clamping
    //  - optional "abide emptiness" (zero-out)
    // Thread-safe; keep one instance per signal you want to recover.
    class RecoveryInterfold
    {
    public:
        struct Params {
            bool  enabled{ true };   // master toggle
            bool  abideEmptiness{ false };  // if true, output 0 regardless of input

            // Spring smoothing
            float stiffness{ 12.0f };  // attraction to input
            float damping{ 2.5f };   // velocity damping

            // Dead-zone / hysteresis around current output
            float hysteresisBand{ 0.01f };  // small band where we slow movement (units of input)

            // Cooldown handling (when input jumps abruptly)
            float jumpThreshold{ 0.15f };  // if |input - output| > threshold, trigger cooldown
            float cooldownSeconds{ 0.20f };  // time in seconds
            float cooldownGain{ 0.3f };   // multiply stiffness during cooldown

            // Clamping
            bool  clampEnabled{ false };
            float clampMin{ 0.0f };
            float clampMax{ 1.0f };

            // Startup behavior
            bool  snapFirstSample{ true };   // first Step() will snap output to first input

            // Safety
            float maxVelocity{ 1000.0f }; // absolute cap on internal velocity
        };

        struct Snapshot {
            double output{ 0.0 };
            double velocity{ 0.0 };
            double cooldownRemaining{ 0.0 };
            bool   seeded{ false };
            Params params{};
        };

    public:
        RecoveryInterfold() = default;

        // Config & Introspection
        void SetParams(const Params& p);
        Params GetParams() const;

        void ConfigureFromJSON(const nlohmann::json& j);
        nlohmann::json SnapshotJSON() const;
        Snapshot SnapshotState() const;

        // Core
        // dt: seconds since last call (>= 0)
        // x : input sample
        // returns current output
        double Step(float dt, double x);

        // Predict next output WITHOUT mutating state.
        double PeekNext(float dt, double x) const;

        // State control
        void Reset();                 // soft reset (keeps params, zeroes state)
        void HardReset(double value); // full reset and set output to value (seeded)
        void BeginCooldown(float seconds); // start/extend cooldown

    private:
        // Helpers (assume lock held OR param copies used)
        static float clampf(float v, float lo, float hi) {
            return (v < lo) ? lo : (v > hi ? hi : v);
        }
        static double clampd(double v, double lo, double hi) {
            return (v < lo) ? lo : (v > hi ? hi : v);
        }
        static double sgn(double v) { return (v > 0.0) - (v < 0.0); }

        struct State {
            double y{ 0.0 };             // output
            double v{ 0.0 };             // velocity
            double cooldown{ 0.0 };      // seconds remaining
            bool   seeded{ false };      // have we seen first sample?
        };

        // Integrate one step using explicit Euler with spring-damper, return new y.
        static void integrateStep(State& st, const Params& p, float dt, double x);

    private:
        mutable std::mutex _mx;
        Params _p{};
        State  _s{};
    };

} // namespace MB
