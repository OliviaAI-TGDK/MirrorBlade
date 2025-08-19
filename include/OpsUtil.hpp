// include/OpUtils.hpp
#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <charconv>
#include <cctype>
#include <algorithm>
#include <sstream>

#if __has_include(<format>)
#include <format>
#define MB_HAVE_STD_FORMAT 1
#else
#define MB_HAVE_STD_FORMAT 0
#endif

namespace MB {

    // ---- string utils ----------------------------------------------------------

    inline std::string Trim(std::string s) {
        auto notspace = [](unsigned char c) { return !std::isspace(c); };
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), notspace));
        s.erase(std::find_if(s.rbegin(), s.rend(), notspace).base(), s.end());
        return s;
    }

    inline std::string ToLower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
            [](unsigned char c) { return std::tolower(c); });
        return s;
    }

    // Split by spaces, honoring simple quotes "like this" or 'like this'
    inline std::vector<std::string> SplitArgs(std::string_view sv) {
        std::vector<std::string> out;
        std::string cur;
        bool inSingle = false, inDouble = false;
        for (size_t i = 0; i < sv.size(); ++i) {
            char c = sv[i];
            if (c == '\'' && !inDouble) { inSingle = !inSingle; continue; }
            if (c == '\"' && !inSingle) { inDouble = !inDouble; continue; }
            if (!inSingle && !inDouble && std::isspace((unsigned char)c)) {
                if (!cur.empty()) { out.emplace_back(cur); cur.clear(); }
            }
            else {
                cur.push_back(c);
            }
        }
        if (!cur.empty()) out.emplace_back(cur);
        return out;
    }

    // Parse key=value tokens (value may be quoted). Returns map with lowercase keys.
    inline std::unordered_map<std::string, std::string>
        ParseKV(std::string_view args) {
        std::unordered_map<std::string, std::string> kv;
        for (auto& tok : SplitArgs(args)) {
            auto eq = tok.find('=');
            if (eq == std::string::npos) continue;
            std::string k = ToLower(Trim(tok.substr(0, eq)));
            std::string v = Trim(tok.substr(eq + 1));
            // strip surrounding quotes if present
            if (v.size() >= 2 && ((v.front() == '"' && v.back() == '"') ||
                (v.front() == '\'' && v.back() == '\''))) {
                v = v.substr(1, v.size() - 2);
            }
            kv.emplace(std::move(k), std::move(v));
        }
        return kv;
    }

    // ---- ok/err helpers --------------------------------------------------------

    inline std::string Ok(std::string_view msg = "ok") {
        return std::string(msg);
    }

#if MB_HAVE_STD_FORMAT
    template <class... Ts>
    inline std::string OkFmt(std::string_view fmt, Ts&&... ts) {
        return std::vformat(fmt, std::make_format_args(std::forward<Ts>(ts)...));
    }
#else
    // lightweight fallback if <format> isn't available
    template <class... Ts>
    inline std::string OkFmt(std::string_view fmt, Ts&&... ts) {
        // Very simple: just return fmt as-is to avoid a hard dependency on fmtlib.
        // Replace with your fmt::format if you link fmt.
        (void)std::initializer_list<int>{((void)sizeof...(ts), 0)};
        return std::string(fmt);
    }
#endif

    inline std::string Err(std::string_view msg) {
        return std::string("error: ") + std::string(msg);
    }

    // ---- numeric parsing -------------------------------------------------------

    inline bool ParseBoolToken(std::string s, bool* out) {
        s = ToLower(Trim(std::move(s)));
        if (s == "1" || s == "true" || s == "on" || s == "yes" || s == "y") { *out = true;  return true; }
        if (s == "0" || s == "false" || s == "off" || s == "no" || s == "n") { *out = false; return true; }
        return false;
    }

    // args can be: "", "on", "off", "true", "false", "1", "0", or "flag=true"
    inline bool ParseBool(std::string_view args, bool defaultVal) {
        auto s = Trim(std::string(args));
        if (s.empty()) return defaultVal;

        // Try direct token
        bool v = false;
        if (ParseBoolToken(s, &v)) return v;

        // Try key=value form
        auto kv = ParseKV(s);
        if (!kv.empty()) {
            // take first entry
            auto it = kv.begin();
            if (ParseBoolToken(it->second, &v)) return v;
        }
        return defaultVal;
    }

    template <class T>
    inline bool FromChars(std::string_view s, T& out) {
        s = Trim(std::string(s));
        auto* begin = s.data();
        auto* end = s.data() + s.size();
        auto res = std::from_chars(begin, end, out);
        return res.ec == std::errc() && res.ptr == end;
    }

    inline int   ParseInt(std::string_view args, int def) { int v;   return FromChars(args, v) ? v : def; }
    inline float ParseFloat(std::string_view args, float def) { float v; return FromChars(args, v) ? v : def; }

    // Parse "key=value" and read numeric
    inline int   ParseKVInt(const std::unordered_map<std::string, std::string>& kv, std::string key, int def) {
        auto it = kv.find(ToLower(std::move(key))); if (it == kv.end()) return def;
        int v;  return FromChars(it->second, v) ? v : def;
    }
    inline float ParseKVFloat(const std::unordered_map<std::string, std::string>& kv, std::string key, float def) {
        auto it = kv.find(ToLower(std::move(key))); if (it == kv.end()) return def;
        float v; return FromChars(it->second, v) ? v : def;
    }
    inline bool  ParseKVBool(const std::unordered_map<std::string, std::string>& kv, std::string key, bool def) {
        auto it = kv.find(ToLower(std::move(key))); if (it == kv.end()) return def;
        bool v = false; return ParseBoolToken(it->second, &v) ? v : def;
    }

    // ---- misc guards -----------------------------------------------------------

    inline bool ExpectNoArgs(std::string_view args, std::string* errOut = nullptr) {
        if (Trim(std::string(args)).empty()) return true;
        if (errOut) *errOut = Err("unexpected arguments");
        return false;
    }

} // namespace MB
