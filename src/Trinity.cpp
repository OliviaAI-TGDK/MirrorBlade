// src/Trinity.cpp
#include "Trinity.hpp"

#include <cmath>
#include <algorithm>

namespace MB::Trinity {

    // -----------------------------
    // Simple deterministic noise
    // -----------------------------
    static inline uint32_t mix32(uint32_t x) {
        x ^= x >> 16; x *= 0x7feb352dU;
        x ^= x >> 15; x *= 0x846ca68bU;
        x ^= x >> 16;
        return x;
    }
    static inline float u32_to_unit(uint32_t x) {
        // [0,1)
        const float inv = 1.0f / 4294967296.0f;
        return static_cast<float>(x) * inv;
    }
    float NoiseHash(const Vec3& p, float t) {
        // hash integerized coords + time
        int ix = static_cast<int>(std::floor(p.x * 97.0f));
        int iy = static_cast<int>(std::floor(p.y * 101.0f));
        int iz = static_cast<int>(std::floor(p.z * 89.0f));
        int it = static_cast<int>(std::floor(t * 53.0f));

        uint32_t h = 2166136261u;
        h ^= static_cast<uint32_t>(ix + 0x9e3779b9u); h = mix32(h);
        h ^= static_cast<uint32_t>(iy + 0x85ebca6bu); h = mix32(h);
        h ^= static_cast<uint32_t>(iz + 0xc2b2ae35u); h = mix32(h);
        h ^= static_cast<uint32_t>(it + 0x27d4eb2fu); h = mix32(h);
        float u = u32_to_unit(h);
        // remap to [-1,1]
        return u * 2.0f - 1.0f;
    }

    // -----------------------------
    // Internal helpers
    // -----------------------------
    static inline Vec3 attractorAccel(const Vec3& pos, const Vec3& tgt, float weight, float falloffPow, float minDist) {
        Vec3 d = tgt - pos;
        float r2 = d.length2();
        float r = std::sqrt(std::max(r2, kSmallNumber));
        // clamp near singularity
        r = std::max(r, minDist);
        Vec3 dir = (r > kSmallNumber) ? (d / r) : Vec3{ 0,0,0 };

        // magnitude ~ weight / r^p
        float mag = weight;
        if (falloffPow > kSmallNumber)
            mag *= std::pow(std::max(r, kSmallNumber), -falloffPow);

        return dir * mag;
    }

    // -----------------------------
    // Public API
    // -----------------------------
    Vec3 ComputeTrideotaxisAccel(const Vec3& pos, const TrideotaxisParams& P, float timeSec) {
        // Base pull from three attractors
        Vec3 acc = Vec3{ 0,0,0 }
            + attractorAccel(pos, P.A, P.wA, P.falloffPow, P.minDist)
            + attractorAccel(pos, P.B, P.wB, P.falloffPow, P.minDist)
            + attractorAccel(pos, P.C, P.wC, P.falloffPow, P.minDist);

        // Swirl component around chosen axis, proportional to current pull magnitude
        float accMag = acc.length();
        if (P.swirlStrength > kSmallNumber && accMag > kSmallNumber) {
            Vec3 axis = P.swirlAxis.normalized();
            // perpendicular swirl = axis x acc_dir
            Vec3 accDir = acc / accMag;
            Vec3 swirl = axis.cross(accDir).normalized() * (accMag * P.swirlStrength);
            acc += swirl;
        }

        // Jitter (tiny procedural noise)
        if (P.jitterAmp > kSmallNumber) {
            float n1 = NoiseHash(pos + Vec3{ 13.1f, 0.0f, 0.0f }, timeSec * P.jitterFreq);
            float n2 = NoiseHash(pos + Vec3{ 0.0f,27.7f, 0.0f }, timeSec * P.jitterFreq);
            float n3 = NoiseHash(pos + Vec3{ 0.0f, 0.0f,39.3f }, timeSec * P.jitterFreq);
            acc += Vec3{ n1, n2, n3 } *P.jitterAmp;
        }

        // Planar constraint (zero out Y component)
        if (P.planar) {
            acc.y = 0.0f;
        }

        // Clamp acceleration magnitude
        if (P.maxAccel > kSmallNumber) {
            acc = acc.clampLength(P.maxAccel);
        }
        return acc;
    }

    void IntegrateTrideotaxis(Vec3& pos, Vec3& vel, const TrideotaxisParams& P, float dt, float timeSec) {
        dt = std::max(dt, 0.0f);

        // Compute acceleration from field
        Vec3 acc = ComputeTrideotaxisAccel(pos, P, timeSec);

        // Semi-implicit Euler
        vel += acc * dt;

        // Damping (approx per-second fraction)
        if (P.damping > 0.0f) {
            float k = clampf(P.damping, 0.0f, 1.0f);
            float damp = std::exp(-k * dt * 60.0f / 60.0f); // roughly frame-rate independent
            vel *= damp;
        }

        // Speed clamp
        if (P.maxSpeed > kSmallNumber) {
            vel = vel.clampLength(P.maxSpeed);
        }

        // Planar lock
        if (P.planar) {
            pos.y = P.planeY; // keep height
            vel.y = 0.0f;
        }

        pos += vel * dt;
    }

} // namespace MB::Trinity
