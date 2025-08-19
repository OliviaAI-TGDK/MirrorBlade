#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <mutex>

namespace MB {

    // LoomisUnderfold
    // ---------------
    // A lightweight, deterministic 1D "underfold" field.
    // You define named creases along the X axis. Each crease pulls (or pushes)
    // positions toward its center with a finite radius and a chosen falloff curve.
    //
    // For an input x:
    //   for each enabled crease in ascending priority:
    //     x = x + gain * K(|x - pos| / radius) * (pos - x)
    // where K(t) ∈ [0,1], with K(0)=1 and K(t>=1)=0 (curve selectable).
    class LoomisUnderfold {
    public:
        struct Crease {
            std::string name;   // unique key
            double      pos = 0.0; // center position
            double      radius = 1.0; // > 0
            double      gain = 0.5; // typical [0..1], can be negative
            int         priority = 0;   // lower -> earlier application
            bool        enabled = true;
        };

        enum class Curve {
            Linear,   // K(t) = max(0, 1 - t)
            Smooth,   // K(t) = 1 - (3t^2 - 2t^3) for t∈[0,1]
            Cosine,   // K(t) = 0.5*(1 + cos(pi*t)) for t∈[0,1]
            Hermite   // K(t) = 1 - (6t^5 - 15t^4 + 10t^3) for t∈[0,1]
        };

        LoomisUnderfold();

        // Global curve control
        void  SetCurve(Curve c);
        Curve GetCurve() const;

        // CRUD on creases (keyed by name)
        void Clear();
        bool Upsert(const Crease& c);                // add or replace by name
        bool Remove(std::string_view name);
        bool Enable(std::string_view name, bool on);
        bool SetPriority(std::string_view name, int p);
        bool SetGain(std::string_view name, double g);
        bool SetRadius(std::string_view name, double r);
        bool SetPosition(std::string_view name, double x);

        // Queries
        bool Exists(std::string_view name) const;
        bool IsEnabled(std::string_view name) const;
        std::vector<Crease> List() const;            // snapshot of current creases

        // Evaluation
        double Evaluate(double x) const;             // folded position
        double EvaluateDelta(double x) const;        // Evaluate(x) - x
        double EvaluateDerivative(double x) const;   // d(Evaluate)/dx
        void   EvaluateMany(const double* xs, double* out, size_t n) const;

        // JSON IO (implemented in .cpp)
        // {
        //   "replace": false,
        //   "curve": "linear|smooth|cosine|hermite",
        //   "creases":[
        //     {"name":"neck","pos":0.0,"radius":0.25,"gain":0.7,"priority":5,"enabled":true}
        //   ]
        // }
        bool        ConfigureFromJSON(const std::string& jsonText, std::string* errorOut = nullptr);
        std::string SnapshotJSON() const;

    private:
        // Helpers (no locking)
        static double saturate(double v);
        static double smoothstep01(double t);
        static double hermite01(double t);
        static bool   validName(std::string_view n);

        // Kernel and derivative wrt normalized distance t in [0,∞)
        double kernel(double t) const;
        double kernel_deriv_dt(double t) const; // dK/dt

        // Unlocked snapshots for fast read
        void snapshot(std::vector<Crease>& out, Curve& curve) const;

    private:
        mutable std::mutex _mx;
        Curve              _curve;
        std::vector<Crease> _creases; // keyed by name (uniqueness enforced in Upsert)
    };

} // namespace MB
