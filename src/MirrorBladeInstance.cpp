#include "MirrorBladeOps.hpp"
#include <sstream>
#include <iomanip>

namespace MB {

    // ----------------- Singleton -----------------
    MirrorBladeOps* MirrorBladeOps::Instance() {
        static MirrorBladeOps g;
        return &g;
    }

    // ----------------- ISerializable -------------
    RED4ext::CClass* MirrorBladeOps::GetNativeType() {
        // Return the RTTI class if registered; safe to return nullptr if not yet available.
        auto* rtti = RED4ext::CRTTISystem::Get();
        if (!rtti) return nullptr;
        return rtti->GetClass(RED4ext::CName("MirrorBlade.MirrorBladeOps"));
    }

    // ----------------- Public API ----------------
    bool MirrorBladeOps::EnableUpscaler(bool enabled) {
        _upscalerEnabled.store(enabled, std::memory_order_relaxed);
        return enabled;
    }

    float MirrorBladeOps::SetTrafficBoost(float multiplier) {
        multiplier = ClampTraffic(multiplier);
        _trafficMultiplier.store(multiplier, std::memory_order_relaxed);
        return multiplier;
    }

    RED4ext::CString MirrorBladeOps::DumpDiag() const {
        std::ostringstream ss;
        ss << "{"
            << "\"upscalerEnabled\":" << (IsUpscalerEnabled() ? "true" : "false") << ","
            << "\"trafficMultiplier\":" << std::fixed << std::setprecision(2) << GetTrafficBoost()
            << "}";
        const std::string s = ss.str();
        return RED4ext::CString(s.c_str());
    }

} // namespace MB
