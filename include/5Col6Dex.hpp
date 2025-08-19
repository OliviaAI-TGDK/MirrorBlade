#pragma once

#include <string>
#include <vector>

#include "Visceptar.hpp"

namespace MB {

    // Mini helper to pretty-print vectors of numbers in columns with fixed precision.
    // Default behavior: 5 columns, 6 decimals.
    // Now optionally frame output using Visceptar.
    class FiveColSixDex {
    public:
        // Returns a single string with newlines separating rows.
        static std::string Format(const std::vector<double>& values,
            int columns = 5,
            int precision = 6);

        // Same as Format but returns one line per element in the returned vector.
        static std::vector<std::string> FormatLines(const std::vector<double>& values,
            int columns = 5,
            int precision = 6);

        struct Stats {
            double min = 0.0;
            double max = 0.0;
            double mean = 0.0;
            double stddev = 0.0;
        };

        static Stats ComputeStats(const std::vector<double>& values);

        // New: format and wrap in a Visceptar frame.
        // If title is non-empty, it is centered at the top of the frame.
        static std::string FormatFramed(const std::vector<double>& values,
            int columns = 5,
            int precision = 6,
            const std::string& title = {},
            const Visceptar::Style& style = {});
    };

} // namespace MB
