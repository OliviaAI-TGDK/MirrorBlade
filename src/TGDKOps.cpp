#include "TGDKOps.hpp"

#include "MBOps.hpp"
#include "TGDKFigure8Fold.hpp"
#include "Detox.hpp"
#include "TGDKTelemetry.hpp"
#include "5Col6Dex.hpp"
#include "Visceptar.hpp"
#include "Scooty.hpp"

#include <nlohmann/json.hpp>
#include <chrono>
#include <string>

// Extra local macro hygiene (in case /FI got bypassed)
#ifdef Event
#  undef Event
#endif

namespace MB {
    using json = nlohmann::json;

    static inline double NowSecs() {
        using clock = std::chrono::steady_clock;
        return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
    }

    void RegisterTGDKOps() {
        auto& ops = Ops::I();

        // ---------------- figure8.* ----------------
        ops.Register("figure8.evalBernoulli", [](const json& a) -> json {
            const double t = a.value("t", 0.0);
            const double A = a.value("a", 1.0);
            auto xy = Figure8Fold::EvalLemniscateBernoulli(t, A);
            return json{ {"ok", true}, {"x", xy.first}, {"y", xy.second} };
            });

        // args: { t, ax, ay, nx, ny, phase }
        ops.Register("figure8.evalLissajous12", [](const json& a) -> json {
            const double t = a.value("t", 0.0);
            const double ax = a.value("ax", 1.0);
            const double ay = a.value("ay", 1.0);
            const double nx = a.value("nx", 1.0);
            const double ny = a.value("ny", 2.0);
            const double phase = a.value("phase", 0.0);
            auto xy = Figure8Fold::EvalLissajous12(t, ax, ay, nx, ny, phase);
            return json{ {"ok", true}, {"x", xy.first}, {"y", xy.second} };
            });

        // ---------------- detox.* ------------------
        static Detox g_detox;

        ops.Register("detox.set", [](const json& a) -> json {
            g_detox.ConfigureFromJSON(a);
            return json{ {"ok", true} };
            });

        // { density01, avgSpeed, refSpeed, base, post, detail, t }
        ops.Register("detox.eval", [](const json& a) -> json {
            Detox::DeflectInput in{};
            in.density01 = a.value("density01", 0.0);
            in.avgSpeed = a.value("avgSpeed", 0.0);
            in.refSpeed = a.value("refSpeed", 20.0);

            auto cp = g_detox.EvaluateDeflection(in);

            const double base = a.value("base", 0.0);
            const double post = a.value("post", base);
            const double detail = a.value("detail", 0.0);
            auto ir = g_detox.Intercede(base, post, detail);

            const double tparam = a.value("t", 0.5);
            auto fr = g_detox.FoldSpecimen(static_cast<float>(tparam));

            return json{
                {"ok", true},
                {"chart",     {{"x", cp.x}, {"y", cp.y}, {"deflection", cp.deflection}}},
                {"intercede", {{"value", ir.value}, {"proportion", ir.proportion}, {"gated", ir.gated}}},
                {"fold",      {{"specimen", fr.specimen}, {"curvature", fr.curvature}}},
                {"params",    g_detox.SnapshotJSON()}
            };
            });

        ops.Register("detox.snapshot", [](const json&) -> json {
            return json{ {"ok", true}, {"params", g_detox.SnapshotJSON()} };
            });

        // ---------------- scooty.* -----------------
        ops.Register("scooty.bump", [](const json& a) -> json {
            const double v = a.value("v", 0.0);
            Scooty::Get().Bump(v);
            return json{ {"ok", true}, {"added", v} };
            });

        ops.Register("scooty.snapshot", [](const json&) -> json {
            auto st = Scooty::Get().Compute();
            return json{ {"ok", true},
                        {"stats", {{"min", st.min}, {"max", st.max}, {"mean", st.mean}, {"stddev", st.stddev}}} };
            });

        ops.Register("scooty.samples", [](const json& a) -> json {
            int n = a.value("n", 25);
            if (n < 1) n = 25;
            if (n > 512) n = 512;

            auto v = Scooty::Get().Samples(static_cast<std::size_t>(n));

            MB::Visceptar::Style st;
            st.h = '=';
            st.corner = '#';
            st.pad = 1;

            const std::string title = "Scooty Samples";
            const std::string framed = MB::FiveColSixDex::FormatFramed(v, 5, 6, title, st);

            return json{ {"ok", true}, {"count", (int)v.size()}, {"framed", framed} };
            });

        // --------------- telem.* -------------------
        // telem.push: { name, a, b, c, tag }
        ops.Register("telem.push", [](const json& a) -> json {
            TGDKTelemetry::Event ev{};
            ev.t = NowSecs();
            ev.name = a.value("name", std::string("evt"));
            ev.a = a.value("a", 0.0);
            ev.b = a.value("b", 0.0);
            ev.c = a.value("c", 0.0);
            ev.tag = a.value("tag", std::string());
            TGDKTelemetry::Get().Push(ev);
            return json{ {"ok", true} };
            });

        // telem.snapshot: { max }
        ops.Register("telem.snapshot", [](const json& a) -> json {
            const int maxN = std::max(1, a.value("max", 64));
            return json{ {"ok", true}, {"events", TGDKTelemetry::Get().SnapshotJSON((std::size_t)maxN)} };
            });

        // telem.table: { max, title }
        ops.Register("telem.table", [](const json& a) -> json {
            const int maxN = std::max(1, a.value("max", 32));
            MB::Visceptar::Style st;
            st.h = '-';
            st.corner = '+';
            st.pad = 1;
            const std::string title = a.value("title", std::string("Telemetry"));
            const std::string txt = TGDKTelemetry::Get().FormatTable((std::size_t)maxN, title, st);
            return json{ {"ok", true}, {"framed", txt} };
            });
    }

} // namespace MB
