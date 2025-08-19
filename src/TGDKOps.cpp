// src/TGDKOps.cpp
#include "MBOps.hpp"
#include "MirrorBladeOps.hpp"
#include "MBConfig.hpp"
#include "MBLog.hpp"

#include "TGDKLoader.hpp"          // new: loader + services
#include "TGDKFigure8Fold.hpp"     // new: figure-8 evaluators

#include <nlohmann/json.hpp>
#include <string>

using MB::Ops;
using json = nlohmann::json;

// Single global loader instance for ops
namespace {
    MB::TGDKLoader g_loader;
}

namespace MB {

    Ops& Ops::I() {
        static Ops g;
        return g;
    }

    void Ops::RegisterAll() {
        // -------------------------
        // Existing controls
        // -------------------------
        _map["upscaler.enable"] = [](const json& a) -> json {
            bool en = a.value("enabled", false);
            auto* ops = MirrorBladeOps::Instance();
            bool result = ops ? ops->EnableUpscaler(en) : false;
            MB::Log().Log(MB::LogLevel::Info, "Upscaler %s", result ? "enabled" : "disabled");
            return json{ {"ok", true}, {"result", result} };
            };

        _map["traffic.mul"] = [](const json& a) -> json {
            double f = a.value("mult", 1.0);
            auto* ops = MirrorBladeOps::Instance();
            float result = ops ? ops->SetTrafficBoost(static_cast<float>(f)) : 1.0f;
            MB::Log().Log(MB::LogLevel::Info, "Traffic multiplier set to %.2f", result);
            return json{ {"ok", true}, {"result,": result} };
            };

        _map["diag.dump"] = [](const json&) -> json {
            auto* ops = MirrorBladeOps::Instance();
            std::string diag = ops ? std::string(ops->DumpDiag().c_str()) : "{}";
            return json{ {"ok", true}, {"result", diag} };
            };

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

        _map["ping"] = [](const json&) -> json {
            return json{ {"ok", true}, {"result", "pong"} };
            };

        // -------------------------
        // New: TGDK Loader family
        // -------------------------

        // Load config from JSON file, optional env override object
        // args: { "path": "r6/config/tgdk.json", "env": { ... } }
        _map["loader.loadFile"] = [](const json& a) -> json {
            std::string path = a.value("path", "");
            json env = a.value("env", json::object());
            if (path.empty()) {
                return json{ {"ok", false}, {"error", "path required"} };
            }
            bool ok = g_loader.LoadFromFile(path, env);
            return json{ {"ok", ok}, {"snapshot", g_loader.SnapshotAll()} };
            };

        // Load directly from JSON objects
        // args: { "config": { ... }, "env": { ... } }
        _map["loader.load"] = [](const json& a) -> json {
            if (!a.contains("config") || !a["config"].is_object()) {
                return json{ {"ok", false}, {"error", "config object required"} };
            }
            json env = a.value("env", json::object());
            g_loader.Load(a["config"], env);
            return json{ {"ok", true}, {"snapshot", g_loader.SnapshotAll()} };
            };

        // Snapshot all services as JSON
        _map["loader.snapshot"] = [](const json&) -> json {
            return json{ {"ok", true}, {"result", g_loader.SnapshotAll()} };
            };

        // -------------------------
        // New: Compound service
        // -------------------------
        // args: { "name": "someEntity" }
        _map["compound.get"] = [](const json& a) -> json {
            std::string name = a.value("name", "");
            if (name.empty()) return json{ {"ok", false}, {"error", "name required"} };

            auto* svc = dynamic_cast<MB::CompoundLoader*>(g_loader.Get("compound"));
            if (!svc) return json{ {"ok", false}, {"error", "compound service missing"} };

            auto v = svc->Get(name);
            if (!v) return json{ {"ok", false}, {"error", "not found"} };
            return json{ {"ok", true}, {"result", *v} };
            };

        // -------------------------
        // New: Impound service
        // -------------------------
        // args: { "name": "Some.Asset.Name" }
        _map["impound.check"] = [](const json& a) -> json {
            std::string name = a.value("name", "");
            if (name.empty()) return json{ {"ok", false}, {"error", "name required"} };

            auto* svc = dynamic_cast<MB::ImpoundLoader*>(g_loader.Get("impound"));
            if (!svc) return json{ {"ok", false}, {"error", "impound service missing"} };

            bool imp = svc->IsImpounded(name);
            return json{ {"ok", true}, {"result", imp} };
            };

        // -------------------------
        // New: VolumetricPhi service
        // -------------------------
        _map["volphi.get"] = [](const json&) -> json {
            auto* svc = dynamic_cast<MB::VolumetricPhiLoader*>(g_loader.Get("volumetricPhi"));
            if (!svc) return json{ {"ok", false}, {"error", "volumetricPhi service missing"} };
            auto p = svc->Get();
            return json{
                {"ok", true},
                {"result",
                    {
                        {"enabled",        p.enabled},
                        {"distanceMul",    p.distanceMul},
                        {"densityMul",     p.densityMul},
                        {"horizonFade",    p.horizonFade},
                        {"jitterStrength", p.jitterStrength},
                        {"temporalBlend",  p.temporalBlend}
                    }
                }
            };
            };

        // args: any subset of the fields above
        _map["volphi.set"] = [](const json& a) -> json {
            auto* svc = dynamic_cast<MB::VolumetricPhiLoader*>(g_loader.Get("volumetricPhi"));
            if (!svc) return json{ {"ok", false}, {"error", "volumetricPhi service missing"} };

            // Build a minimal config block as TGDKLoader expects
            json j = {
                {"volumetricPhi", {
                    {"enabled",        a.value("enabled",        true)},
                    {"distanceMul",    a.value("distanceMul",    1.0f)},
                    {"densityMul",     a.value("densityMul",     1.0f)},
                    {"horizonFade",    a.value("horizonFade",    0.25f)},
                    {"jitterStrength", a.value("jitterStrength", 1.0f)},
                    {"temporalBlend",  a.value("temporalBlend",  0.90f)}
                }}
            };

            MB::LoaderContext ctx{ nullptr };
            svc->Configure(j, ctx);
            svc->Apply();
            return json{ {"ok", true}, {"result", svc->Snapshot()} };
            };

        // -------------------------
        // New: Figure-8 evaluators
        // -------------------------

        // args: { "t": <float>, "a": <float default 1.0> }
        _map["figure8.evalBernoulli"] = [](const json& a) -> json {
            double t = a.value("t", 0.0);
            double A = a.value("a", 1.0);
            auto [x, y] = MB::Figure8Fold::EvalLemniscateBernoulli(t, A);
            return json{ {"ok", true}, {"x", x}, {"y", y} };
            };

        // args: { "t": <float>, "ax":1.0, "ay":1.0, "nx":1.0, "ny":2.0, "phase":0.0 }
        _map["figure8.evalLissajous12"] = [](const json& a) -> json {
            double t = a.value("t", 0.0);
            double ax = a.value("ax", 1.0);
            double ay = a.value("ay", 1.0);
            double nx = a.value("nx", 1.0);
            double ny = a.value("ny", 2.0);
            double phase = a.value("phase", 0.0);
            auto [x, y] = MB::Figure8Fold::EvalLissajous12(t, ax, ay, nx, ny, phase);
            return json{ {"ok", true}, {"x", x}, {"y", y} };
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
