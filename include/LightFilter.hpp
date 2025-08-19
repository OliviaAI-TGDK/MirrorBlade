#pragma once
#include <string>
#include <unordered_set>
#include <atomic>
#include <functional>   // <-- needed for std::function

namespace MB {

    struct LightFilterConfig {
        std::atomic<bool> adverts{ true };      // billboard helpers
        std::atomic<bool> portals{ false };     // door/window fills (PT recommended)
        std::atomic<bool> forcePortals{ false };// override PT check

        // Simple name heuristics (can be hot-reloaded)
        std::unordered_set<std::string> advertTokens = {
            "billboard","ad_","holo_","adscreen","lcd_","screen_","promo_","neon_sign"
        };
        std::unordered_set<std::string> portalTokens = {
            "window_fill","door_fill","window_fake","portal_fill","wnd_fill","door_fake"
        };
    };

    class LightFilter {
    public:
        static LightFilter& Get();

        void SetAdverts(bool on);
        void SetPortals(bool on);
        void SetForcePortals(bool on);

        // Hooks
        void OnEntitySpawn(void* world, void* entity);
        void SweepWorld(void* world);

        // Utilities
        bool IsPathTracingActive() const;

    private:
        LightFilterConfig cfg_;

        bool IsAdvertHelperName(const std::string& name) const;
        bool IsPortalHelperName(const std::string& name) const;

        void DisableLightComponent(void* lightComp) const;
        void ForEachLightComponent(void* entity, const std::function<void(void*)>& fn) const;

        static std::string ToLower(std::string s);
    };

} // namespace MB
