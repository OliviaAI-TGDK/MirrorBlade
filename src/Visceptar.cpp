#include "Visceptar.hpp"

#include <algorithm>
#include <sstream>

namespace MB {

    std::string Visceptar::Ruler(std::size_t width, char ch) {
        return std::string(width, ch);
    }

    std::size_t Visceptar::MaxLineLen(const std::vector<std::string>& lines) {
        std::size_t m = 0;
        for (const auto& s : lines) m = std::max(m, s.size());
        return m;
    }

    std::string Visceptar::PadLine(const std::string& s, std::size_t width) {
        if (s.size() >= width) return s;
        return s + std::string(width - s.size(), ' ');
    }

    std::string Visceptar::Center(const std::string& s, std::size_t width) {
        if (s.size() >= width) return s.substr(0, width);
        const std::size_t total = width - s.size();
        const std::size_t left = total / 2;
        const std::size_t right = total - left;
        return std::string(left, ' ') + s + std::string(right, ' ');
    }

    std::string Visceptar::FrameLines(const std::vector<std::string>& lines,
        std::size_t min_width,
        const Style& st,
        const std::string& title) {
        const std::size_t content_w = std::max({ min_width, MaxLineLen(lines), title.size() });
        const std::size_t inner_pad = static_cast<std::size_t>(st.pad < 0 ? 0 : st.pad);
        const std::size_t total_w = 2 /*borders*/ + inner_pad * 2 + content_w;

        std::ostringstream out;
        // top
        out << st.corner << Ruler(total_w - 2, st.h) << st.corner << '\n';

        // title
        if (!title.empty()) {
            out << st.v
                << std::string(inner_pad, ' ')
                << Center(title, content_w)
                << std::string(inner_pad, ' ')
                << st.v << '\n';
            if (st.title_rule) {
                out << st.v
                    << std::string(inner_pad, ' ')
                    << Ruler(content_w, st.h)
                    << std::string(inner_pad, ' ')
                    << st.v << '\n';
            }
        }

        // content
        for (const auto& s : lines) {
            out << st.v
                << std::string(inner_pad, ' ')
                << PadLine(s, content_w)
                << std::string(inner_pad, ' ')
                << st.v << '\n';
        }

        // bottom
        out << st.corner << Ruler(total_w - 2, st.h) << st.corner << '\n';
        return out.str();
    }

    std::string Visceptar::FrameText(const std::string& text,
        std::size_t min_width,
        const Style& st,
        const std::string& title) {
        std::vector<std::string> lines;
        lines.reserve(32);

        std::size_t start = 0;
        while (start <= text.size()) {
            auto pos = text.find('\n', start);
            if (pos == std::string::npos) {
                lines.emplace_back(text.substr(start));
                break;
            }
            lines.emplace_back(text.substr(start, pos - start));
            start = pos + 1;
            if (start == text.size()) { lines.emplace_back(""); break; }
        }
        return FrameLines(lines, min_width, st, title);
    }

} // namespace MB
