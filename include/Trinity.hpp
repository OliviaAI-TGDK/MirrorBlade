// include/Trinity.hpp
#pragma once

#include <cmath>
#include <algorithm>
#include <tuple>
#include <limits>
#include <cstdint>

namespace MB::Trinity {

    // -----------------------------
    // Small math helpers
    // -----------------------------
    constexpr float kEpsilon = 1e-6f;
    constexpr float kSmallNumber = 1e-8f;
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kTwoPi = 6.28318530717958647692f;

    inline bool isFinite(float x) {
        return std::isfinite(x) != 0;
    }
    inline float clampf(float v, float lo, float hi) {
        return std::max(lo, std::min(hi, v));
    }

    // -----------------------------
    // Vec2
    // -----------------------------
    struct Vec2 {
        float x{ 0 }, y{ 0 };

        Vec2() = default;
        constexpr Vec2(float _x, float _y) : x(_x), y(_y) {}

        // basic ops
        Vec2 operator+(const Vec2& r) const { return { x + r.x, y + r.y }; }
        Vec2 operator-(const Vec2& r) const { return { x - r.x, y - r.y }; }
        Vec2 operator*(float s) const { return { x * s, y * s }; }
        Vec2 operator/(float s) const { return s != 0.0f ? Vec2{ x / s, y / s } : Vec2{ 0,0 }; }
        Vec2& operator+=(const Vec2& r) { x += r.x; y += r.y; return *this; }
        Vec2& operator-=(const Vec2& r) { x -= r.x; y -= r.y; return *this; }
        Vec2& operator*=(float s) { x *= s; y *= s; return *this; }
        Vec2& operator/=(float s) { if (s != 0.0f) { x /= s; y /= s; } else { x = 0; y = 0; } return *this; }

        // hadamard
        Vec2 hadamard(const Vec2& r) const { return { x * r.x, y * r.y }; }

        // metrics
        float length2() const { return x * x + y * y; }
        float length() const { return std::sqrt(length2()); }
        bool  isNearlyZero(float eps = kEpsilon) const { return length2() <= eps * eps; }
        bool  isFinite() const { return MB::Trinity::isFinite(x) && MB::Trinity::isFinite(y); }

        // normalization
        Vec2  normalized() const {
            float L = length();
            return (L > kSmallNumber) ? (*this / L) : Vec2{ 0,0 };
        }
        bool  tryNormalize() {
            float L = length();
            if (L > kSmallNumber) { x /= L; y /= L; return true; }
            x = 0; y = 0; return false;
        }

        // dot, "cross" (returns z of 3D cross (x,y,0) x (r.x,r.y,0))
        float dot(const Vec2& r) const { return x * r.x + y * r.y; }
        float crossZ(const Vec2& r) const { return x * r.y - y * r.x; }

        // projection/rejection
        Vec2 projectOn(const Vec2& n) const {
            float d2 = n.length2();
            if (d2 <= kSmallNumber) return Vec2{ 0,0 };
            return n * (dot(n) / d2);
        }
        Vec2 rejectFrom(const Vec2& n) const {
            return (*this) - projectOn(n);
        }

        // reflect across normal (expects n normalized; still handles non-normalized safely)
        Vec2 reflect(const Vec2& n) const {
            Vec2 nn = n.normalized();
            return (*this) - nn * (2.0f * this->dot(nn));
        }

        // rotate by angle (radians)
        Vec2 rotated(float angle) const {
            float c = std::cos(angle), s = std::sin(angle);
            return { x * c - y * s, x * s + y * c };
        }

        // set / clamp length
        Vec2 withLength(float L) const {
            float cur = length();
            if (cur <= kSmallNumber) return Vec2{ 0,0 };
            return (*this) * (L / cur);
        }
        Vec2 clampLength(float maxLen) const {
            float L2 = length2();
            float m2 = maxLen * maxLen;
            if (L2 > m2 && L2 > 0.0f) {
                float f = maxLen / std::sqrt(L2);
                return { x * f, y * f };
            }
            return *this;
        }

        // angles
        float angleTo(const Vec2& r) const {
            float d = this->dot(r);
            float ll = this->length() * r.length();
            if (ll <= kSmallNumber) return 0.0f;
            float c = clampf(d / ll, -1.0f, 1.0f);
            return std::acos(c);
        }
        // signed angle from this to r (positive = CCW)
        float signedAngleTo(const Vec2& r) const {
            float ang = angleTo(r);
            float s = crossZ(r);
            return (s >= 0.0f) ? ang : -ang;
        }

        // lerp
        static Vec2 lerp(const Vec2& a, const Vec2& b, float t) {
            return a * (1.0f - t) + b * t;
        }
    };

    inline Vec2 operator*(float s, const Vec2& v) { return v * s; }

    // -----------------------------
    // Vec3
    // -----------------------------
    struct Vec3 {
        float x{ 0 }, y{ 0 }, z{ 0 };

        Vec3() = default;
        constexpr Vec3(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}

        // basic ops
        Vec3 operator+(const Vec3& r) const { return { x + r.x, y + r.y, z + r.z }; }
        Vec3 operator-(const Vec3& r) const { return { x - r.x, y - r.y, z - r.z }; }
        Vec3 operator*(float s) const { return { x * s, y * s, z * s }; }
        Vec3 operator/(float s) const { return s != 0.0f ? Vec3{ x / s, y / s, z / s } : Vec3{ 0,0,0 }; }
        Vec3& operator+=(const Vec3& r) { x += r.x; y += r.y; z += r.z; return *this; }
        Vec3& operator-=(const Vec3& r) { x -= r.x; y -= r.y; z -= r.z; return *this; }
        Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
        Vec3& operator/=(float s) { if (s != 0.0f) { x /= s; y /= s; z /= s; } else { x = 0; y = 0; z = 0; } return *this; }

        // hadamard
        Vec3 hadamard(const Vec3& r) const { return { x * r.x, y * r.y, z * r.z }; }

        // metrics
        float length2() const { return x * x + y * y + z * z; }
        float length() const { return std::sqrt(length2()); }
        bool  isNearlyZero(float eps = kEpsilon) const { return length2() <= eps * eps; }
        bool  isFinite() const { return MB::Trinity::isFinite(x) && MB::Trinity::isFinite(y) && MB::Trinity::isFinite(z); }

        // normalization
        Vec3  normalized() const {
            float L = length();
            return (L > kSmallNumber) ? (*this / L) : Vec3{ 0,0,0 };
        }
        bool  tryNormalize() {
            float L = length();
            if (L > kSmallNumber) { x /= L; y /= L; z /= L; return true; }
            x = 0; y = 0; z = 0; return false;
        }

        // dot / cross
        float dot(const Vec3& r) const { return x * r.x + y * r.y + z * r.z; }
        Vec3  cross(const Vec3& r) const { return { y * r.z - z * r.y, z * r.x - x * r.z, x * r.y - y * r.x }; }

        // projection / rejection
        Vec3 projectOn(const Vec3& n) const {
            float d2 = n.length2();
            if (d2 <= kSmallNumber) return Vec3{ 0,0,0 };
            return n * (dot(n) / d2);
        }
        Vec3 rejectFrom(const Vec3& n) const {
            return (*this) - projectOn(n);
        }

        // reflection / refraction (expects n normalized; safe-guards included)
        Vec3 reflect(const Vec3& n) const {
            Vec3 nn = n.normalized();
            return (*this) - nn * (2.0f * this->dot(nn));
        }
        // eta = n1/n2
        Vec3 refract(const Vec3& n, float eta) const {
            Vec3 nn = n.normalized();
            float cosi = clampf(this->dot(nn), -1.0f, 1.0f);
            float etai = 1.0f, etat = eta;
            Vec3  nuse = nn;
            if (cosi < 0.0f) {
                cosi = -cosi;
            }
            else {
                std::swap(etai, etat);
                nuse = nn * -1.0f;
            }
            float etaRatio = etai / etat;
            float k = 1.0f - etaRatio * etaRatio * (1.0f - cosi * cosi);
            if (k < 0.0f) {
                // total internal reflection
                return this->reflect(nn);
            }
            return (*this) * etaRatio + nuse * (etaRatio * cosi - std::sqrt(k));
        }

        // rotate around axis by angle (radians) - Rodrigues
        Vec3 rotatedAround(const Vec3& axis, float angle) const {
            Vec3 k = axis.normalized();
            float c = std::cos(angle), s = std::sin(angle);
            return (*this) * c + k.cross(*this) * s + k * (k.dot(*this) * (1.0f - c));
        }

        // orthonormal basis from this (treated as normal)
        void orthonormalBasis(Vec3& tangent, Vec3& bitangent) const {
            Vec3 n = this->normalized();
            Vec3 a = (std::fabs(n.x) > 0.5f) ? Vec3{ 0,1,0 } : Vec3{ 1,0,0 };
            tangent = (a - n * n.dot(a)).normalized();
            bitangent = n.cross(tangent);
        }

        // with length / clamp length / limit length range
        Vec3 withLength(float L) const {
            float cur = length();
            if (cur <= kSmallNumber) return Vec3{ 0,0,0 };
            return (*this) * (L / cur);
        }
        Vec3 clampLength(float maxLen) const {
            float L2 = length2();
            float m2 = maxLen * maxLen;
            if (L2 > m2 && L2 > 0.0f) {
                float f = maxLen / std::sqrt(L2);
                return { x * f, y * f, z * f };
            }
            return *this;
        }
        Vec3 limitLength(float minLen, float maxLen) const {
            float L = length();
            if (L <= kSmallNumber) return Vec3{ 0,0,0 };
            float cl = clampf(L, minLen, maxLen);
            return (*this) * (cl / L);
        }

        // angles / lerp / slerp (unit-safe)
        float angleTo(const Vec3& r) const {
            float d = this->dot(r);
            float ll = this->length() * r.length();
            if (ll <= kSmallNumber) return 0.0f;
            float c = clampf(d / ll, -1.0f, 1.0f);
            return std::acos(c);
        }

        static Vec3 lerp(const Vec3& a, const Vec3& b, float t) {
            return a * (1.0f - t) + b * t;
        }
        static Vec3 slerp(Vec3 a, Vec3 b, float t) {
            a = a.normalized();
            b = b.normalized();
            float cosom = clampf(a.dot(b), -1.0f, 1.0f);
            if (cosom > 0.9995f) {
                return lerp(a, b, t).normalized();
            }
            float omega = std::acos(cosom);
            float sinom = std::sin(omega);
            float s0 = std::sin((1.0f - t) * omega) / sinom;
            float s1 = std::sin(t * omega) / sinom;
            return a * s0 + b * s1;
        }
    };

    inline Vec3 operator*(float s, const Vec3& v) { return v * s; }

    // -----------------------------
    // Vec4 (minimal, for completeness)
    // -----------------------------
    struct Vec4 {
        float x{ 0 }, y{ 0 }, z{ 0 }, w{ 0 };
        Vec4() = default;
        constexpr Vec4(float _x, float _y, float _z, float _w) :x(_x), y(_y), z(_z), w(_w) {}
        Vec4(const Vec3& v, float _w) : x(v.x), y(v.y), z(v.z), w(_w) {}
    };

    // -----------------------------
    // Trideotaxis
    // A tri-attractor potential-guided acceleration field with
    // damping, swirl, and optional planar constraint.
    // -----------------------------
    struct TrideotaxisParams {
        Vec3 A{ 0,0,0 };
        Vec3 B{ 0,0,0 };
        Vec3 C{ 0,0,0 };

        float wA{ 1.0f };
        float wB{ 1.0f };
        float wC{ 1.0f };

        // 1/r^p influence; p in [0..4] typical
        float falloffPow{ 1.0f };
        float minDist{ 0.1f };     // avoid singularity

        // dynamics
        float maxAccel{ 50.0f };
        float maxSpeed{ 20.0f };
        float damping{ 0.05f };    // per-second velocity damping fraction [0..1]

        // swirl around axis
        Vec3  swirlAxis{ 0,1,0 };
        float swirlStrength{ 0.0f };   // 0..1

        // jitter
        float jitterAmp{ 0.0f };       // magnitude in accel units
        float jitterFreq{ 1.0f };      // Hz

        // planar lock (optional)
        bool  planar{ false };
        float planeY{ 0.0f };
    };

    // Deterministic tiny hash noise based on position and time.
    float NoiseHash(const Vec3& p, float t);

    // Compute acceleration from trideotaxis field
    Vec3  ComputeTrideotaxisAccel(const Vec3& pos, const TrideotaxisParams& P, float timeSec = 0.0f);

    // Integrate position/velocity one step
    void  IntegrateTrideotaxis(Vec3& pos, Vec3& vel, const TrideotaxisParams& P, float dt, float timeSec = 0.0f);

} // namespace MB::Trinity
