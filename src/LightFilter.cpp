#include "LightFilter.hpp"

#if __has_include("MBOps.hpp")
#include "MBOps.hpp"
#include <nlohmann/json.hpp>
using json = nlohmann::json;

namespace {
    struct _MB_LightOpsBoot {
        _MB_LightOpsBoot() {
            // lights.fake.adverts { "enabled": bool }
            MB::Ops::I().Register("lights.fake.adverts", [](const json& a) -> json {
                bool on = a.value("enabled", true);
                MB::LightFilter::Get().SetAdverts(on);
                return json{ {"ok", true}, {"adverts", on} };
                });

            // lights.fake.portals { "enabled": bool }
            MB::Ops::I().Register("lights.fake.portals", [](const json& a) -> json {
                bool on = a.value("enabled", false);
                MB::LightFilter::Get().SetPortals(on);
                return json{ {"ok", true}, {"portals", on} };
                });

            // lights.fake.forceportals { "enabled": bool }
            MB::Ops::I().Register("lights.fake.forceportals", [](const json& a) -> json {
                bool on = a.value("enabled", false);
                MB::LightFilter::Get().SetForcePortals(on);
                return json{ {"ok", true}, {"forcePortals", on} };
                });
        }
    } _mb_lightOpsBoot;
} // namespace
#else
// MBOps not available: compile as no-op TU to avoid link/compile errors.
#endif
