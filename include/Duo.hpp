#pragma once
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <mutex>
#include <utility>

namespace MB {

    // Lightweight 2D float pair with math helpers.
    struct Duo {
        float x{ 0.0f };
        float y{ 0.0f };

        constexpr Duo() = default;
        constexpr Duo(float _x, float _y) : x(_x), y(_y) {}

        static constexpr Duo zero() { return Duo(0.0f, 0.0f); }
        static constexpr Duo unitX() { return Duo(1.0f, 0.0f); }
        static constexpr Duo unitY() { return Duo(0.0f, 1.0f); }

        float length()  const noexcept;
        float length2() const noexcept;
        bool  isFinite() const noexcept;

        Duo   normalized(float eps = 1e-8f) const noexcept;
        Duo   rotated(float radians) const noexcept;
        Duo& clamp(const Duo& minv, const Duo& maxv) noexcept;

        static Duo  Lerp(const Duo& a, const Duo& b, float t) noexcept;
        static float Dot(const Duo& a, const Duo& b) noexcept;

        bool approxEqual(const Duo& o, float eps = 1e-6f) const noexcept;

        // Operators
        Duo& operator+=(const Duo& o) noexcept { x += o.x; y += o.y; return *this; }
        Duo& operator-=(const Duo& o) noexcept { x -= o.x; y -= o.y; return *this; }
        Duo& operator*=(float s) noexcept { x *= s; y *= s; return *this; }
        Duo& operator/=(float s) noexcept { x /= s; y /= s; return *this; }

        friend Duo operator+(Duo a, const Duo& b) noexcept { a += b; return a; }
        friend Duo operator-(Duo a, const Duo& b) noexcept { a -= b; return a; }
        friend Duo operator*(Duo a, float s) noexcept { a *= s; return a; }
        friend Duo operator*(float s, Duo a) noexcept { a *= s; return a; }
        friend Duo operator/(Duo a, float s) noexcept { a /= s; return a; }
        friend Duo operator-(Duo v) noexcept { return Duo(-v.x, -v.y); }
    };

    // Exponential moving-average filter for Duo with thread safety.
    class DuoFilterEMA {
    public:
        DuoFilterEMA() = default;
        explicit DuoFilterEMA(float alpha) : _alpha(clamp01(alpha)) {}

        void  Reset(const Duo& start = Duo::zero());
        void  SetAlpha(float a);
        float GetAlpha() const;

        // Push a new sample; returns filtered value.
        Duo   Push(const Duo& v);
        Duo   Value() const;
        bool  HasHistory() const;

    private:
        static float clamp01(float v) { return std::max(0.0f, std::min(1.0f, v)); }

        mutable std::mutex _mx;
        float _alpha{ 1.0f }; // 1 = passthrough, 0 = freeze
        bool  _have{ false };
        Duo   _value{};
    };

    // Deterministic subpixel jitter generator using Halton(2,3).
    class DuoJitter {
    public:
        DuoJitter() = default;
        explicit DuoJitter(float strength) : _strength(strength) {}

        void   Reset(uint32_t index = 0);
        void   SetStrength(float s);
        float  GetStrength() const;

        // Advance index and return centered jitter in [-0.5, 0.5] * strength.
        Duo    Advance();
        uint32_t Index() const;

        static Duo Halton23(uint32_t index);

    private:
        static float halton(uint32_t i, uint32_t base);

        mutable std::mutex _mx;
        uint32_t _index{ 0 };
        float    _strength{ 1.0f };
        Duo      _current{};
    };

} // namespace MB
