#pragma once
#include <vector>
#include <mutex>
#include <cstddef>

namespace MB {

    class Scooty {
    public:
        static Scooty& Get();

        // push a new reading (e.g., speed/whatever)
        void Bump(double v);

        // copy out the recent readings
        std::vector<double> Samples(std::size_t max = 50) const;

        struct Stats { double min = 0, max = 0, mean = 0, stddev = 0; };
        Stats Compute() const;

    private:
        Scooty() = default;
        Scooty(const Scooty&) = delete;
        Scooty& operator=(const Scooty&) = delete;

        mutable std::mutex _mx;
        std::vector<double> _ring; // simple buffer
    };

} // namespace MB
