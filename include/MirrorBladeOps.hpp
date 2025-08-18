#pragma once
#include <atomic>
#include <mutex>
#include <string>

#include "RED4ext/ISerializable.hpp"   // base class + GetNativeType()
#include "RED4ext/RTTISystem.hpp"      // CRTTISystem::Get, GetClass
#include "RED4ext/NativeTypes.hpp"     // RED4ext::CString
#include "RED4ext/CName.hpp" // RED4ext::CName

namespace MB {

    // Bridge for runtime feature toggles, exposed to other subsystems and ops.
    // NOTE: We inherit ISerializable because your existing header chain expects it.
    // We provide GetNativeType() to satisfy the abstract base.
    class MirrorBladeOps : public RED4ext::ISerializable {
    public:
        // Access singleton instance
        static MirrorBladeOps* Instance();

        // ISerializable override: return the class RTTI if available
        RED4ext::CClass* GetNativeType() override;

        // Feature controls
        bool  EnableUpscaler(bool enabled);
        float SetTrafficBoost(float multiplier);

        // Human-friendly diagnostic blob (compact JSON-like string)
        RED4ext::CString DumpDiag() const;

        // Lock-free accessors
        bool  IsUpscalerEnabled() const noexcept { return _upscalerEnabled.load(std::memory_order_relaxed); }
        float GetTrafficBoost()  const noexcept { return _trafficMultiplier.load(std::memory_order_relaxed); }

    private:
        MirrorBladeOps() = default;
        MirrorBladeOps(const MirrorBladeOps&) = delete;
        MirrorBladeOps& operator=(const MirrorBladeOps&) = delete;

        static float ClampTraffic(float v) noexcept {
            if (v < 0.10f) return 0.10f;
            if (v > 50.0f) return 50.0f;
            return v;
        }

        std::atomic<bool>  _upscalerEnabled{ false };
        std::atomic<float> _trafficMultiplier{ 1.0f };
        mutable std::mutex _mtx; // reserved for future multi-field guarded updates
    };

} // namespace MB
