// src/Bridge.cpp — production-grade helper (no duplicate exports)
//
// This TU intentionally does NOT define MB::InitBridge/ShutdownBridge.
// Those are defined in MirrorBladeBridge.cpp to avoid LNK2005 collisions.
//
// Responsibilities here:
//  - Lightweight logging helpers used locally.
//  - Load and push "onLoad" boot ops from red4ext/plugins/<Plugin>/config.json
//    to the already-running pipe server via a client connection.
//  - Optional no-op ApplyPending stub (kept for back-compat if some code calls it).
//
// Notes:
//  - We DO NOT include or compile "RenderHook.cpp" here. Use its header (or forward
//    declarations) from the TU that actually installs hooks.
//  - Boot ops are sent over the same JSON RPC pipe: \\\\.\\pipe\\MirrorBladeBridge-v1
//    so this file doesn’t need access to the internal g_ops map in another TU.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <Shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <functional>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include "json.hpp"
using json = nlohmann::json;

// -------------------- Local logging --------------------
static void MB_Log(const char* s)
{
    OutputDebugStringA("[MirrorBladeBridge/Bridge] ");
    OutputDebugStringA(s);
    OutputDebugStringA("\n");
}
static void MB_Logf(const char* fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    MB_Log(buf);
}

// -------------------- Constants --------------------
static const wchar_t* PIPE_NAME = L"\\\\.\\pipe\\MirrorBladeBridge-v1";

// -------------------- Small utilities --------------------
static std::wstring GetDllDir()
{
    HMODULE hm = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&GetDllDir), &hm))
    {
        wchar_t path[MAX_PATH] = {};
        GetModuleFileNameW(hm, path, MAX_PATH);
        PathRemoveFileSpecW(path);
        return path;
    }
    return L"";
}

static bool ReadLineFromPipe(HANDLE h, std::string& acc, std::string& outLine)
{
    outLine.clear();
    char ch = 0;
    DWORD br = 0;
    while (true)
    {
        if (!ReadFile(h, &ch, 1, &br, nullptr) || br == 0)
            return false; // disconnected / no data
        if (ch == '\n')
        {
            outLine.swap(acc);
            acc.clear();
            return true;
        }
        acc.push_back(ch);
        if (acc.size() > 1'000'000) { acc.clear(); return false; } // guard
    }
}

static void WriteJsonLineToPipe(HANDLE h, const json& j)
{
    std::string s = j.dump();
    s.push_back('\n');
    DWORD bw = 0;
    WriteFile(h, s.data(), static_cast<DWORD>(s.size()), &bw, nullptr);
}

// -------------------- Boot ops (send over pipe) --------------------
static void RunBootOpsOverPipe()
{
    try
    {
        // config at: red4ext\plugins\<PluginDir>\config.json
        std::wstring dir = GetDllDir();
        std::wstring cfgPath = dir + L"\\config.json";

        HANDLE hCfg = CreateFileW(cfgPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hCfg == INVALID_HANDLE_VALUE)
        {
            MB_Logf("No config.json at %ls (boot ops skipped)", cfgPath.c_str());
            return;
        }

        DWORD size = GetFileSize(hCfg, nullptr);
        std::string data;
        data.resize(size ? size : 0);
        DWORD br = 0;
        if (size && !ReadFile(hCfg, data.data(), size, &br, nullptr))
        {
            CloseHandle(hCfg);
            MB_Log("Failed to read config.json");
            return;
        }
        CloseHandle(hCfg);
        if (br != data.size()) data.resize(br);

        json cfg = json::parse(data, /*callback*/nullptr, /*allow_exceptions*/true);
        if (!cfg.contains("onLoad") || !cfg["onLoad"].is_array())
        {
            MB_Log("config.json missing onLoad[]; nothing to do.");
            return;
        }

        // Try to connect to our server with a few retries (server may still be starting).
        HANDLE hPipe = INVALID_HANDLE_VALUE;
        for (int attempt = 0; attempt < 40; ++attempt) // ~4s total
        {
            hPipe = CreateFileW(PIPE_NAME, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
            if (hPipe != INVALID_HANDLE_VALUE) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (hPipe == INVALID_HANDLE_VALUE)
        {
            MB_Log("RunBootOps: could not connect to pipe server (skipping onLoad).");
            return;
        }

        // Send each op and read one response line per op (best-effort).
        std::string rbuf;
        for (auto& op : cfg["onLoad"])
        {
            if (!op.is_object() || !op.contains("op")) continue;
            if (!op.contains("v")) op["v"] = 1;

            WriteJsonLineToPipe(hPipe, op);

            // try to read one reply line (non-blocking-ish with timeout)
            std::string line;
            DWORD avail = 0;
            for (int spin = 0; spin < 50; ++spin) // up to ~500ms
            {
                if (PeekNamedPipe(hPipe, nullptr, 0, nullptr, &avail, nullptr) && avail > 0)
                {
                    if (ReadLineFromPipe(hPipe, rbuf, line))
                    {
                        MB_Logf("[boot-op reply] %s", line.c_str());
                        break;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }

        CloseHandle(hPipe);
        MB_Log("Boot ops processed.");
    }
    catch (const std::exception& e)
    {
        MB_Logf("RunBootOpsOverPipe exception: %s", e.what());
    }
}

// -------------------- Public tiny helpers --------------------
namespace MB
{
    // Kept for back-compat; this file no longer owns any command queue.
    void ApplyPending()
    {
        // No-op in this TU. Real dispatch lives in MirrorBladeBridge.cpp.
    }

    // Expose an explicit call so your main init (in MirrorBladeBridge.cpp) can invoke boot ops
    // AFTER the server thread is listening.
    void RunBootOps()
    {
        RunBootOpsOverPipe();
    }
} // namespace MB
