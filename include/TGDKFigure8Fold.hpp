#pragma once
#include <utility>

namespace MB {

    class Figure8Fold {
    public:
        struct Params {
            float speed = 1.0f;  // angular speed multiplier
            float radius = 1.0f;  // overall amplitude / scale
            float aspect = 1.0f;  // y scaling (1 keeps symmetry)
            float phase = 0.0f;  // initial phase (radians)
        };

        Figure8Fold() = default;

        void SetParams(const Params& p);
        Params GetParams() const;

        void Reset(float t = 0.0f);
        void Advance(float dt);

        // Generic figure-8 (our default Lissajous 1:2)
        std::pair<float, float> Current() const;
        std::pair<float, float> Evaluate(float t) const;

        // Explicit named variants:
        //  - Lissajous with frequency ratio 1:2 (same as Evaluate)
        std::pair<float, float> EvalLissajous12(float t) const;

        //  - Lemniscate of Bernoulli (polar r^2 = a^2 cos(2θ))
        //    Using a stable rational parametrization; scaled by Params.radius/aspect.
        std::pair<float, float> EvalLemniscateBernoulli(float t) const;

    private:
        static constexpr float kTwoPi = 6.2831853071795864769f;

        float WrapAngle(float t) const;

        Params _p{};
        float  _t{ 0.0f };
    };

} // namespace MB
