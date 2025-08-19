#pragma once

#include <string>
#include <vector>
#include <cstddef>

namespace MB {

    // Visceptar: super small framing/box/ruler text helper.
    // Produces ASCII frames around lines with optional centered title.
    class Visceptar {
    public:
        struct Style {
            char corner = '+';
            char h = '-';
            char v = '|';
            int  pad = 1;      // spaces to pad inside the frame (left+right)
            bool title_rule = true; // put a horizontal rule under the title
        };

        // Make a horizontal ruler of given width (>= 0).
        static std::string Ruler(std::size_t width, char ch = '-');

        // Frame the provided lines inside a box. If 'title' is non-empty,
        // it is centered on its own line near the top.
        // min_width lets you bump the content area width (not counting borders).
        // Returns a single string with trailing '\n'.
        static std::string FrameLines(const std::vector<std::string>& lines,
            std::size_t min_width = 0,
            const Style& style = {},
            const std::string& title = {});

        // Convenience: frame a single multi-line string (split on '\n').
        static std::string FrameText(const std::string& text,
            std::size_t min_width = 0,
            const Style& style = {},
            const std::string& title = {});

    private:
        static std::size_t MaxLineLen(const std::vector<std::string>& lines);
        static std::string PadLine(const std::string& s, std::size_t width);
        static std::string Center(const std::string& s, std::size_t width);
    };

} // namespace MB
