#include "MBOps.hpp"
#include "MirrorBladeOps.hpp"
#include "MBConfig.hpp"
#include "MBLog.hpp"

#include <nlohmann/json.hpp>
#include <string>

using MB::Ops;
using json = nlohmann::json;

namespace MB {

    Ops& Ops::I() {
        static Ops g;
        return g;
    }

    void Ops::RegisterAll() {
        // Upscaler toggle
        _map["upscaler.enable"] = [](const json& a) -> json {
            bool en = a.value("enabled", false);
            auto* ops = MirrorBladeOps::Instance();
            bool result = ops ? ops->EnableUpscaler(en) : false;
            MB::Log().Log(MB::LogLevel::Info, "Upscaler %s", result ? "enabled" : "disabled");
            return json{ {"ok", true}, {"result", result} };
            };

        // Traffic multiplier
        _map["traffic.mul"] = [](const json& a) -> json {
            double f = a.value("mult", 1.0);
            auto* ops = MirrorBladeOps::Instance();
            float result = ops ? ops->SetTrafficBoost(static_cast<float>(f)) : 1.0f;
            MB::Log().Log(MB::LogLevel::Info, "Traffic multiplier set to %.2f", result);
            return json{ {"ok", true}, {"result", result} };
            };

        // Diagnostics
        _map["diag.dump"] = [](const json&) -> json {
            auto* ops = MirrorBladeOps::Instance();
            std::string diag = ops ? std::string(ops->DumpDiag().c_str()) : "{}";
            return json{ {"ok", true}, {"result", diag} };
            };

        // Config reload/save
        _map["config.reload"] = [](const json&) -> json {
            bool ok = MB::ReloadConfig();
            MB::Log().Log(ok ? MB::LogLevel::Info : MB::LogLevel::Error,
                ok ? "Config reloaded" : "Config reload failed");
            return json{ {"ok", ok} };
            };

        _map["config.save"] = [](const json&) -> json {
            bool ok = MB::SaveConfig();
            MB::Log().Log(ok ? MB::LogLevel::Info : MB::LogLevel::Error,
                ok ? "Config saved" : "Config save failed");
            return json{ {"ok", ok} };
            };

        // Healthcheck
        _map["ping"] = [](const json&) -> json {
            return json{ {"ok", true}, {"result", "pong"} };
            };
    }

    json Ops::Dispatch(const std::string& op, const json& args) {
        auto it = _map.find(op);
        if (it == _map.end()) {
            MB::Log().Log(MB::LogLevel::Warn, "Unknown op: %s", op.c_str());
            return json{ {"ok", false}, {"error", std::string("Unknown op: ") + op} };
        }
        try {
            return it->second(args);
        }
        catch (const std::exception& e) {
            MB::Log().Log(MB::LogLevel::Error, "Op '%s' threw: %s", op.c_str(), e.what());
            return json{ {"ok", false}, {"error", e.what()} };
        }
        catch (...) {
            MB::Log().Log(MB::LogLevel::Error, "Op '%s' threw unknown exception", op.c_str());
            return json{ {"ok", false}, {"error", "unknown error"} };
        }
    }

} // namespace MB
