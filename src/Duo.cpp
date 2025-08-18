#include "Duo.hpp"
#include <limits>

namespace MB {

    // -------- Duo --------

    float Duo::length() const noexcept {
        return std::sqrt(x * x + y * y);
    }

    float Duo::length2() const noexcept {
        return x * x + y * y;
    }

    bool Duo::isFinite() const noexcept {
        return std::isfinite(x) && std::isfinite(y);
    }

    Duo Duo::normalized(float eps) const noexcept {
        const float m2 = length2();
        if (m2 <= eps * eps) return Duo(0.0f, 0.0f);
        const float inv = 1.0f / std::sqrt(m2);
        return Duo(x * inv, y * inv);
    }

    Duo Duo::rotated(float r) const noexcept {
        const float c = std::cos(r);
        const float s = std::sin(r);
        return Duo(x * c - y * s, x * s + y * c);
    }

    Duo& Duo::clamp(const Duo& minv, const Duo& maxv) noexcept {
        x = std::max(minv.x, std::min(maxv.x, x));
        y = std::max(minv.y, std::min(maxv.y, y));
        return *this;
    }

    Duo Duo::Lerp(const Duo& a, const Duo& b, float t) noexcept {
        return Duo(a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t);
    }

    float Duo::Dot(const Duo& a, const Duo& b) noexcept {
        return a.x * b.x + a.y * b.y;
    }

    bool Duo::approxEqual(const Duo& o, float eps) const noexcept {
        return std::fabs(x - o.x) <= eps && std::fabs(y - o.y) <= eps;
    }

    // -------- DuoFilterEMA --------

    void DuoFilterEMA::Reset(const Duo& start) {
        std::lock_guard<std::mutex> lk(_mx);
        _value = start;
        _have = true;
    }

    void DuoFilterEMA::SetAlpha(float a) {
        std::lock_guard<std::mutex> lk(_mx);
        _alpha = clamp01(a);
    }

    float DuoFilterEMA::GetAlpha() const {
        std::lock_guard<std::mutex> lk(_mx);
        return _alpha;
    }

    Duo DuoFilterEMA::Push(const Duo& v) {
        std::lock_guard<std::mutex> lk(_mx);
        if (!_have) {
            _value = v;
            _have = true;
        }
        else {
            const float a = _alpha;
            _value.x = a * v.x + (1.0f - a) * _value.x;
            _value.y = a * v.y + (1.0f - a) * _value.y;
        }
        return _value;
    }

    Duo DuoFilterEMA::Value() const {
        std::lock_guard<std::mutex> lk(_mx);
        return _value;
    }

    bool DuoFilterEMA::HasHistory() const {
        std::lock_guard<std::mutex> lk(_mx);
        return _have;
    }

    // -------- DuoJitter (Halton 2,3) --------

    float DuoJitter::halton(uint32_t i, uint32_t base) {
        float f = 1.0f;
        float r = 0.0f;
        while (i) {
            f /= static_cast<float>(base);
            r += f * static_cast<float>(i % base);
            i /= base;
        }
        return r;
    }

    Duo DuoJitter::Halton23(uint32_t index) {
        // Skip 0 to avoid (0,0)
        const uint32_t k = index + 1u;
        return Duo(halton(k, 2u), halton(k, 3u));
    }

    void DuoJitter::Reset(uint32_t index) {
        std::lock_guard<std::mutex> lk(_mx);
        _index = index;
        _current = Duo(0.0f, 0.0f);
    }

    void DuoJitter::SetStrength(float s) {
        std::lock_guard<std::mutex> lk(_mx);
        _strength = std::max(0.0f, s);
    }

    float DuoJitter::GetStrength() const {
        std::lock_guard<std::mutex> lk(_mx);
        return _strength;
    }

    Duo DuoJitter::Advance() {
        std::lock_guard<std::mutex> lk(_mx);
        _index++;
        Duo h = Halton23(_index);
        // center to [-0.5, 0.5] and scale
        _current.x = (h.x - 0.5f) * _strength;
        _current.y = (h.y - 0.5f) * _strength;
        return _current;
    }

    uint32_t DuoJitter::Index() const {
        std::lock_guard<std::mutex> lk(_mx);
        return _index;
    }

} // namespace MB
