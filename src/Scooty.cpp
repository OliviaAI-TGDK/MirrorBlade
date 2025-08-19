#include "Scooty.hpp"
#include <algorithm>
#include <limits>
#include <cmath>

namespace MB {

    Scooty& Scooty::Get() {
        static Scooty g;
        return g;
    }

    void Scooty::Bump(double v) {
        std::lock_guard<std::mutex> lk(_mx);
        _ring.push_back(v);
        if (_ring.size() > 512) _ring.erase(_ring.begin(), _ring.begin() + (_ring.size() - 512));
    }

    std::vector<double> Scooty::Samples(std::size_t max) const {
        std::lock_guard<std::mutex> lk(_mx);
        if (_ring.size() <= max) return _ring;
        return std::vector<double>(_ring.end() - max, _ring.end());
    }

    Scooty::Stats Scooty::Compute() const {
        Stats s{};
        auto v = Samples(512);
        if (v.empty()) return s;

        double mn = std::numeric_limits<double>::infinity();
        double mx = -std::numeric_limits<double>::infinity();
        long double sum = 0.0L;
        for (double x : v) { mn = std::min(mn, x); mx = std::max(mx, x); sum += x; }
        const double mean = static_cast<double>(sum / v.size());

        long double acc = 0.0L;
        for (double x : v) { long double d = x - mean; acc += d * d; }
        const double stddev = std::sqrt(static_cast<double>(acc / v.size()));

        s.min = mn; s.max = mx; s.mean = mean; s.stddev = stddev;
        return s;
    }

} // namespace MB
