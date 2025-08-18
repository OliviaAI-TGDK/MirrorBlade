// src/MBConfig.cpp
#include "MBConfig.hpp"
#include "MBLog.hpp"
#include "MirrorBladeOps.hpp"     // for MirrorBladeOps::Instance()
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include <filesystem>
#include <fstream>
#include <string>
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>

static std::string WStringToUtf8(const std::wstring& ws) {
    if (ws.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(),
        nullptr, 0, nullptr, nullptr);
    std::string out(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(),
        out.data(), len, nullptr, nullptr);
    return out;
}

static std::wstring Utf8ToWString(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(),
        nullptr, 0);
    std::wstring out(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(),
        out.data(), len);
    return out;
}


namespace MB {

    namespace {
        // ---------------------------
        // Globals (guarded by g_cfgMtx)
        // ---------------------------
        static Config        g_cfg;           // current in-memory config
        static std::mutex    g_cfgMtx;

        // Live-reload watcher
        static std::thread       g_watchThr;
        static std::atomic<bool> g_watchRun{ false };

        // ---------------------------
        // Helpers
        // ---------------------------

        static std::filesystem::path GameRoot()
        {
            // Resolve from this DLL location: .../bin/x64/plugins/<plugin>.dll → up to game root
            wchar_t modPathW[MAX_PATH]{};
            HMODULE hm = nullptr;
            GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&GameRoot),
                &hm);
            GetModuleFileNameW(hm, modPathW, MAX_PATH);
            std::filesystem::path p = modPathW;
            // .../bin/x64/plugins/<plugin>.dll → plugins → x64 → bin → (game root)
            return p.parent_path().parent_path().parent_path().parent_path();
        }

        static inline float clampf(float v, float lo, float hi)
        {
            return (v < lo) ? lo : (v > hi) ? hi : v;
        }

        // Atomic UTF-8 write with replace + write-through and temp cleanup.
        static bool AtomicWriteUTF8(const std::filesystem::path& dst, const std::string& data)
        {
            std::error_code ec;
            std::filesystem::create_directories(dst.parent_path(), ec);

            auto tmp = dst;
            tmp += L".tmp";

            // Write temp file
            {
                std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
                if (!f) return false;
                f.write(data.data(), static_cast<std::streamsize>(data.size()));
                if (!f.good()) { f.close(); std::filesystem::remove(tmp, ec); return false; }
                f.flush();
            }

            // Atomically replace target with write-through
            if (!MoveFileExW(tmp.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            {
                // Cleanup temp on failure
                std::filesystem::remove(tmp, ec);
                return false;
            }
            return true;
        }

        // FILETIME helpers for cheap polling
        static FILETIME GetFileTimeOrZero(const std::filesystem::path& p)
        {
            WIN32_FILE_ATTRIBUTE_DATA fad{};
            if (GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &fad))
                return fad.ftLastWriteTime;
            FILETIME z{}; z.dwLowDateTime = 0; z.dwHighDateTime = 0;
            return z;
        }
        static bool FileTimeChanged(const FILETIME& a, const FILETIME& b)
        {
            return a.dwLowDateTime != b.dwLowDateTime || a.dwHighDateTime != b.dwHighDateTime;
        }

        // Map Config::LogLevel → MB::LogLevel
        static MB::LogLevel ToLoggerLevel(Config::LogLevel l)
        {
            switch (l) {
            case Config::LogLevel::Trace: return MB::LogLevel::Trace;
            case Config::LogLevel::Debug: return MB::LogLevel::Debug;
            case Config::LogLevel::Info:  return MB::LogLevel::Info;
            case Config::LogLevel::Warn:  return MB::LogLevel::Warn;
            case Config::LogLevel::Error: return MB::LogLevel::Error;
            }
            return MB::LogLevel::Info;
        }

    } // anonymous namespace

    // ---------------------------
    // Config static methods
    // ---------------------------

    std::filesystem::path Config::ResolveConfigPath()
    {
        // r6/config/MirrorBlade.json under game root
        return GameRoot() / "r6" / "config" / "MirrorBlade.json";
    }

    Config::LogLevel ParseLogLevel(std::string_view s)
    {
        if (s == "trace") return Config::LogLevel::Trace;
        if (s == "debug") return Config::LogLevel::Debug;
        if (s == "warn")  return Config::LogLevel::Warn;
        if (s == "error") return Config::LogLevel::Error;
        return Config::LogLevel::Info;
    }

    Config Config::LoadFromFile(const std::filesystem::path& path)
    {
        json j;
        Config c; // defaults from header ctor/initializers
        // --- ipc ---
        auto it_ipc = j.find("ipc");
        if (it_ipc != j.end() && it_ipc->is_object()) {
            const json& ipco = *it_ipc;
            c.ipcEnabled.store(ipco.value("enabled", true));
            std::string pn = ipco.value("pipeName", R"(\\.\pipe\MirrorBladeBridge)");
            c.ipcPipeName = Utf8ToWString(pn);
        }

        // --- logging ---
        auto it_log = j.find("logging");
        if (it_log != j.end() && it_log->is_object()) {
            const json& lo = *it_log;
            c.logLevel.store(ParseLogLevel(lo.value("level", "info")));
        }

        try {
            if (!std::filesystem::exists(path)) {
                MB::Log().Log(MB::LogLevel::Debug, "Config file not found, using defaults: %ls", path.c_str());
                return c;
            }

            std::ifstream f(path, std::ios::binary);
            if (!f) {
                MB::Log().Log(MB::LogLevel::Warn, "Failed to open config: %ls", path.c_str());
                return c;
            }

            json j; f >> j;

            // version (optional)
            const int version = j.value("version", 1);
            (void)version;

            // core
            c.upscaler.store(j.value("upscaler", c.upscaler.load()));
            c.traffic.store(clampf(j.value("trafficBoost", c.traffic.load()), 0.10f, 50.0f));

            // ipc
            if (auto it = j.find("ipc"); it != j.end() && it->is_object()) {
                c.ipcEnabled.store(it->value("enabled", c.ipcEnabled.load()));
                const std::string pn = it->value("pipeName", std::string(c.ipcPipeName.begin(), c.ipcPipeName.end()));
                c.ipcPipeName = std::wstring(pn.begin(), pn.end());
            }

            // logging
            if (auto it = j.find("logging"); it != j.end() && it->is_object()) {
                c.logLevel.store(ParseLogLevel(it->value("level", "info")));
            }

            MB::Log().Log(MB::LogLevel::Info, "Config loaded: upscaler=%d, traffic=%.2f, ipc=%d",
                c.upscaler.load() ? 1 : 0, c.traffic.load(), c.ipcEnabled.load() ? 1 : 0);
        }
        catch (const std::exception& e) {
            MB::Log().Log(MB::LogLevel::Warn, "Config parse error (%ls): %s", path.c_str(), e.what());
            // keep defaults
        }
        catch (...) {
            MB::Log().Log(MB::LogLevel::Warn, "Config parse error (%ls): unknown", path.c_str());
        }

        return c;
    }

    std::string Config::ToJSON() const
    {
        json j;
        j["version"] = 1;
        j["upscaler"] = upscaler.load();
        j["trafficBoost"] = traffic.load();
        j["ipc"] = {
          {"enabled", ipcEnabled.load()},
          {"pipeName", WStringToUtf8(ipcPipeName)}  // was std::string(ipcPipeName.begin(), ipcPipeName.end())
        };

        j["ipc"] = {
            {"enabled",  ipcEnabled.load()},
            {"pipeName", std::string(ipcPipeName.begin(), ipcPipeName.end())}
        };

        const char* lvl = "info";
        switch (logLevel.load()) {
        case LogLevel::Trace: lvl = "trace"; break;
        case LogLevel::Debug: lvl = "debug"; break;
        case LogLevel::Info:  lvl = "info";  break;
        case LogLevel::Warn:  lvl = "warn";  break;
        case LogLevel::Error: lvl = "error"; break;
        }
        j["logging"] = { {"level", lvl} };

        return j.dump(2);
    }

    void Config::ApplyRuntime()
    {
        // Push into ops
        if (auto* ops = MirrorBladeOps::Instance()) {
            ops->EnableUpscaler(upscaler.load());
            ops->SetTrafficBoost(traffic.load());
        }

        // Apply log level immediately
        MB::Log().SetLevel(ToLoggerLevel(logLevel.load()));

        // If you manage IPC lifetime/dynamic rename, do it here based on ipcEnabled/ipcPipeName.
        MB::Log().Log(MB::LogLevel::Debug, "Runtime applied: upscaler=%d, traffic=%.2f, loglevel=%d",
            upscaler.load() ? 1 : 0, traffic.load(), static_cast<int>(logLevel.load()));
    }

    // ---------------------------
    // Global getters/setters
    // ---------------------------

    const Config& GetConfig()
    {
        std::scoped_lock lk{ g_cfgMtx };
        return g_cfg;
    }

    void SetConfig(const Config& c)
    {
        std::scoped_lock lk{ g_cfgMtx };
        g_cfg = c;
    }

    // ---------------------------
    // Public API
    // ---------------------------

    bool ReloadConfig()
    {
        const auto path = Config::ResolveConfigPath();
        Config c = Config::LoadFromFile(path);
        {
            std::scoped_lock lk{ g_cfgMtx };
            g_cfg = c;
        }
        c.ApplyRuntime();
        MB::Log().Log(MB::LogLevel::Info, "Config reloaded");
        return true;
    }

    bool SaveConfig()
    {
        std::string jsonStr;
        {
            std::scoped_lock lk{ g_cfgMtx };
            jsonStr = g_cfg.ToJSON();
        }
        const auto path = Config::ResolveConfigPath();
        const bool ok = AtomicWriteUTF8(path, jsonStr);
        MB::Log().Log(ok ? MB::LogLevel::Info : MB::LogLevel::Error,
            ok ? "Config saved to %ls" : "Config save FAILED to %ls",
            path.c_str());
        return ok;
    }

    void InitConfig()
    {
        const auto path = Config::ResolveConfigPath();

        // Initial load
        {
            Config c = Config::LoadFromFile(path);
            {
                std::scoped_lock lk{ g_cfgMtx };
                g_cfg = c;
            }
            c.ApplyRuntime();
        }

        // Start file watcher (timestamp polling + debounce)
        g_watchRun = true;
        g_watchThr = std::thread([path] {
            FILETIME last = GetFileTimeOrZero(path);
            FILETIME stableProbe = last;
            int stableTicks = 0;

            while (g_watchRun.load(std::memory_order_relaxed)) {
                ::Sleep(250); // poll interval
                const FILETIME now = GetFileTimeOrZero(path);

                if (FileTimeChanged(now, stableProbe)) {
                    // observed change; begin/continue debounce
                    stableProbe = now;
                    stableTicks = 0;
                }
                else if (FileTimeChanged(now, last)) {
                    // no further change since last probe → count toward stability
                    if (++stableTicks >= 4) { // ~1s stable
                        last = now;
                        stableTicks = 0;

                        try {
                            Config c = Config::LoadFromFile(path);
                            {
                                std::scoped_lock lk{ g_cfgMtx };
                                g_cfg = c;
                            }
                            c.ApplyRuntime();
                            MB::Log().Log(MB::LogLevel::Info, "Config auto-reloaded");
                        }
                        catch (...) {
                            MB::Log().Log(MB::LogLevel::Warn, "Config auto-reload failed");
                        }
                    }
                }
            }
            });

        MB::Log().Log(MB::LogLevel::Info, "Config initialized (watching %ls)", path.c_str());
    }

    void ShutdownConfig()
    {
        g_watchRun = false;
        if (g_watchThr.joinable())
            g_watchThr.join();
        MB::Log().Log(MB::LogLevel::Info, "Config shutdown");
    }

} // namespace MB
