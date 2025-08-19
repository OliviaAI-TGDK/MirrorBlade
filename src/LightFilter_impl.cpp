#include <atomic>
#include "LightFilter.hpp" // adjust if your header is named differently

namespace {
    struct LFState {
        std::atomic<bool> adverts{ false };
        std::atomic<bool> portals{ false };
        std::atomic<bool> forcePortals{ false };
    };
    LFState& state() { static LFState s; return s; }
}

namespace MB {
    LightFilter& LightFilter::Get() { static LightFilter g; return g; }
    void LightFilter::SetAdverts(bool v) { state().adverts.store(v, std::memory_order_relaxed); }
    void LightFilter::SetPortals(bool v) { state().portals.store(v, std::memory_order_relaxed); }
    void LightFilter::SetForcePortals(bool v) { state().forcePortals.store(v, std::memory_order_relaxed); }
    void LightFilter::SweepWorld(void* /*world*/) { /* stub; wire up later */ }
} // namespace MB
