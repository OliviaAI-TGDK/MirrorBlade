#include "Ops_LightFilter.hpp"
#include "LightFilter.hpp"

#if __has_include("MBOps.hpp")
#include "MBOps.hpp"
#include <nlohmann/json.hpp>
using json = nlohmann::json;

namespace MB {

    void RegisterLightFilterOps_JSON() {
        // lights.fake.adverts { "enabled": bool }
        Ops::I().Register("lights.fake.adverts", [](const json& a) -> json {
            const bool on = a.value("enabled", true);
            LightFilter::Get().SetAdverts(on);
            return json{ {"ok", true}, {"adverts", on} };
            });

        // lights.fake.portals { "enabled": bool }
        Ops::I().Register("lights.fake.portals", [](const json& a) -> json {
            const bool on = a.value("enabled", false);
            LightFilter::Get().SetPortals(on);
            return json{ {"ok", true}, {"portals", on} };
            });

        // lights.fake.forceportals { "enabled": bool }
        Ops::I().Register("lights.fake.forceportals", [](const json& a) -> json {
            const bool on = a.value("enabled", false);
            LightFilter::Get().SetForcePortals(on);
            return json{ {"ok", true}, {"forcePortals", on} };
            });

        // lights.fake.sweep {}
        Ops::I().Register("lights.fake.sweep", [](const json& a) -> json {
            (void)a;
            LightFilter::Get().SweepWorld(nullptr); // supply a real world ptr if you have one
            return json{ {"ok", true}, {"sweep", "queued"} };
            });
    }

} // namespace MB

#else

namespace MB {
    void RegisterLightFilterOps_JSON() {
        // No-op when MBOps.hpp is not available.
    }
} // namespace MB

#endif
