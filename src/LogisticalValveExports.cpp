// src/LogisticalValveExports.cpp
#include "LogisticalValveExports.hpp"

#include <windows.h>      // LocalAlloc / LocalFree
#include <cstring>
#include <string>

#include <nlohmann/json.hpp>
#include "MBOps.hpp"

#if __has_include("MBLog.hpp")
#include "MBLog.hpp"
#define MBLOGI(fmt, ...) MB::Log().Log(MB::LogLevel::Info,  fmt, __VA_ARGS__)
#define MBLOGW(fmt, ...) MB::Log().Log(MB::LogLevel::Warn,  fmt, __VA_ARGS__)
#define MBLOGE(fmt, ...) MB::Log().Log(MB::LogLevel::Error, fmt, __VA_ARGS__)
#else
#define MBLOGI(...) (void)0
#define MBLOGW(...) (void)0
#define MBLOGE(...) (void)0
#endif

using json = nlohmann::json;

// Allocate a NUL-terminated copy of 's' using LocalAlloc (LMEM_FIXED).
static char* DupToLocalAlloc(const std::string& s) {
    const size_t n = s.size();
    HLOCAL h = ::LocalAlloc(LMEM_FIXED, n + 1);
    if (!h) return nullptr;
    char* p = static_cast<char*>(h);
    if (n) std::memcpy(p, s.data(), n);
    p[n] = '\0';
    return p;
}

LV_API const char* LV_Version() {
    // Keep this string short and tool-friendly (no spaces required).
    // Return via LocalAlloc so callers can always use LV_FreeString.
    static const char kVersion[] = "MirrorBladeBridge-LV-1";
    return DupToLocalAlloc(std::string(kVersion));
}

LV_API int LV_Ping() {
    return 1;
}

LV_API const char* LV_DispatchJSON(const char* opC, const char* argsC) {
    std::string op = opC ? opC : "";
    std::string args = argsC ? argsC : "";

    json in = json::object();
    if (!args.empty()) {
        try {
            in = json::parse(args);
            if (!in.is_object()) {
                in = json::object();
            }
        }
        catch (const std::exception& e) {
            json err = {
                {"ok", false},
                {"error", std::string("args parse: ") + e.what()}
            };
            return DupToLocalAlloc(err.dump());
        }
        catch (...) {
            json err = { {"ok", false}, {"error", "args parse: unknown"} };
            return DupToLocalAlloc(err.dump());
        }
    }

    json out;
    try {
        out = MB::Ops::I().Dispatch(op, in);
    }
    catch (const std::exception& e) {
        MBLOGE("LV_DispatchJSON('%s') threw: %s", op.c_str(), e.what());
        out = json{ {"ok", false}, {"error", e.what()} };
    }
    catch (...) {
        MBLOGE("LV_DispatchJSON('%s') threw unknown exception", op.c_str());
        out = json{ {"ok", false}, {"error", "unknown exception"} };
    }

    return DupToLocalAlloc(out.dump());
}

LV_API void LV_FreeString(const char* s) {
    if (s) ::LocalFree((HLOCAL)s);
}

