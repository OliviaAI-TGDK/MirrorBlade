#include "5Col6Dex.hpp"

#include <sstream>
#include <iomanip>
#include <limits>
#include <cmath>
#include <algorithm>

namespace MB {

    static int safe_columns(int columns) {
        if (columns <= 0) return 5;
        if (columns > 64) return 64; // arbitrary ceiling to keep lines reasonable
        return columns;
    }

    static int default_width(int precision) {
        if (precision < 0) precision = 6;
        // sign + integer digit + dot + precision + padding
        int w = 1 + 1 + 1 + precision + 3;
        return (w < 10) ? 10 : w;
    }

    std::string FiveColSixDex::Format(const std::vector<double>& values,
        int columns,
        int precision) {
        columns = safe_columns(columns);
        if (precision < 0) precision = 6;

        const int width = default_width(precision);

        std::ostringstream out;
        out.setf(std::ios::fixed);
        out << std::setprecision(precision);

        int col = 0;
        for (std::size_t i = 0; i < values.size(); ++i) {
            out << std::setw(width) << values[i];
            ++col;
            if (col == columns) {
                out << '\n';
                col = 0;
            }
        }
        if (col != 0) out << '\n';
        return out.str();
    }

    std::vector<std::string> FiveColSixDex::FormatLines(const std::vector<double>& values,
        int columns,
        int precision) {
        columns = safe_columns(columns);
        if (precision < 0) precision = 6;

        const int width = default_width(precision);

        std::vector<std::string> lines;
        std::ostringstream line;
        line.setf(std::ios::fixed);
        line << std::setprecision(precision);

        int col = 0;
        for (std::size_t i = 0; i < values.size(); ++i) {
            line << std::setw(width) << values[i];
            ++col;
            if (col == columns) {
                lines.emplace_back(line.str());
                line.str(std::string());
                line.clear();
                line.setf(std::ios::fixed);
                line << std::setprecision(precision);
                col = 0;
            }
        }
        if (col != 0) {
            lines.emplace_back(line.str());
        }
        return lines;
    }

    FiveColSixDex::Stats FiveColSixDex::ComputeStats(const std::vector<double>& values) {
        Stats s{};
        if (values.empty()) return s;

        double mn = std::numeric_limits<double>::infinity();
        double mx = -std::numeric_limits<double>::infinity();
        long double sum = 0.0L;

        for (double v : values) {
            if (v < mn) mn = v;
            if (v > mx) mx = v;
            sum += v;
        }

        const double mean = static_cast<double>(sum / static_cast<long double>(values.size()));

        long double var_acc = 0.0L;
        for (double v : values) {
            const long double d = static_cast<long double>(v) - static_cast<long double>(mean);
            var_acc += d * d;
        }
        const double variance = static_cast<double>(var_acc / static_cast<long double>(values.size()));
        const double stddev = std::sqrt(variance);

        s.min = mn;
        s.max = mx;
        s.mean = mean;
        s.stddev = stddev;
        return s;
    }

    std::string FiveColSixDex::FormatFramed(const std::vector<double>& values,
        int columns,
        int precision,
        const std::string& title,
        const Visceptar::Style& style) {
        auto lines = FormatLines(values, columns, precision);

        // Ensure there's at least one line so framing is not empty
        if (lines.empty()) lines.emplace_back("");

        // inner content width is the max line length
        std::size_t minw = 0;
        for (const auto& s : lines) minw = std::max(minw, s.size());

        return Visceptar::FrameLines(lines, minw, style, title);
    }

} // namespace MB
