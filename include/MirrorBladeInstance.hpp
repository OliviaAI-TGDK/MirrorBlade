#pragma once
#include <atomic>
#include <mutex>
#include <string>

namespace MB {

    // Central bridge for runtime feature toggles that other systems can push into.
    // Thread-safe, process-wide singleton.
    class MirrorBladeOps {
    public:
        // Access the single instance
        static MirrorBladeOps* Instance();

        // Enable/disable the upscaler (e.g., FSR2/FSR3/XeSS/DLSS routing done elsewhere)
        // Returns the resulting on/off state.
        bool  EnableUpscaler(bool enabled);

        // Set traffic multiplier (clamped to [0.1, 50.0]).
        // Returns the resulting multiplier.
        float SetTrafficBoost(float multiplier);

        // Diagnostics snapshot as a compact JSON-like text (human-friendly).
        // (No dependencies on a JSON lib to keep the surface light.)
        std::string DumpDiag() const;

        // Convenience accessors (lock-free atomics)
        bool  IsUpscalerEnabled() const noexcept { return _upscalerEnabled.load(std::memory_order_relaxed); }
        float GetTrafficBoost()  const noexcept { return _trafficMultiplier.load(std::memory_order_relaxed); }

    private:
        MirrorBladeOps() = default;
        MirrorBladeOps(const MirrorBladeOps&) = delete;
        MirrorBladeOps& operator=(const MirrorBladeOps&) = delete;

        // Internals
        static float _ClampTraffic(float v) noexcept;

        std::atomic<bool>  _upscalerEnabled{ false };
        std::atomic<float> _trafficMultiplier{ 1.0f };

        // For future expansion if you need guarded transitions, complex state, etc.
        mutable std::mutex _mtx;
    };

} // namespace MB
