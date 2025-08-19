// MBLog.cpp — macro-safe, STL-first, ASCII-only (save as UTF-8 without BOM)

// --- Emergency scrub in case a PCH or /FI already pulled rpcndr.h ---
#ifdef string
#undef string
#endif
#ifdef small
#undef small
#endif
#ifdef uuid
#undef uuid
#endif
#ifdef hyper
#undef hyper
#endif
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#ifdef interface
#undef interface
#endif
#ifdef near
#undef near
#endif
#ifdef far
#undef far
#endif

// --- C++ headers FIRST (so macros can't break them) ---
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>

// --- Project header ---
#include "MBLog.hpp"

// --- Win32 AFTER STL; be lean; then scrub MIDL keywords again ---
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <rpc.h>
#include <rpcndr.h>

#ifdef string
#undef string
#endif
#ifdef small
#undef small
#endif
#ifdef uuid
#undef uuid
#endif
#ifdef hyper
#undef hyper
#endif

namespace {

    // Resolve the plugin folder from this module's address.
    std::filesystem::path GetPluginFolder() {
        wchar_t path[MAX_PATH]{};
        HMODULE hm = nullptr;

        // Use the address of this function to resolve the containing module.
        GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&GetPluginFolder),
            &hm);

        GetModuleFileNameW(hm, path, static_cast<DWORD>(std::size(path)));
        return std::filesystem::path(path).parent_path(); // .../plugins/MirrorBladeBridge
    }

} // namespace

namespace MB {

    // Global logger instance.
    static Logger g_logger;

    Logger& Log() { return g_logger; }

    void Logger::Init(const std::filesystem::path& logDir,
        const std::wstring& base,
        size_t maxBytes,
        int keep) {
        std::scoped_lock lk{ _mtx };
        _dir = logDir;
        _base = base;
        _maxBytes = maxBytes;
        _keep = keep;

        std::error_code ec;
        std::filesystem::create_directories(_dir, ec);
        _cur = _dir / (std::wstring(_base) + L".log");

        // Touch current log file.
        std::ofstream(_cur, std::ios::app | std::ios::binary).close();
    }

    void Logger::SetLevel(LogLevel lvl) {
        _lvl.store(lvl, std::memory_order_relaxed);
    }

    std::string Logger::timestamp() {
        using namespace std::chrono;
        const auto now = system_clock::now();
        const auto t = system_clock::to_time_t(now);
        const auto us = duration_cast<microseconds>(now.time_since_epoch()) % 1'000'000;

        std::tm tm{};
        localtime_s(&tm, &t);

        std::ostringstream ss;
        ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S")
            << "." << std::setw(6) << std::setfill('0') << us.count();
        return ss.str();
    }

    void Logger::rotateIfNeededUnlocked() {
        if (_maxBytes == 0) return;

        std::error_code ec;
        const auto sz = std::filesystem::file_size(_cur, ec);
        if (ec || sz < _maxBytes) return;

        // Rotate: base.(keep-1).log -> base.keep.log, ..., base.1.log -> base.2.log, cur -> base.1.log
        for (int i = _keep - 1; i >= 1; --i) {
            const auto from = _dir / (std::wstring(_base) + L"." + std::to_wstring(i) + L".log");
            const auto to = _dir / (std::wstring(_base) + L"." + std::to_wstring(i + 1) + L".log");
            if (std::filesystem::exists(from, ec)) {
                std::filesystem::remove(to, ec);
                std::filesystem::rename(from, to, ec);
            }
        }
        const auto to1 = _dir / (std::wstring(_base) + L".1.log");
        std::filesystem::remove(to1, ec);
        std::filesystem::rename(_cur, to1, ec);

        // Start a fresh file.
        std::ofstream(_cur, std::ios::trunc | std::ios::binary).close();
    }

    void Logger::writeUnlocked(const std::string& line) {
        rotateIfNeededUnlocked();
        std::ofstream f(_cur, std::ios::app | std::ios::binary);
        if (!f) return;
        f.write(line.data(), static_cast<std::streamsize>(line.size()));
        f.put('\n');
    }

    static const char* lvlName(LogLevel l) {
        switch (l) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
        }
        return "?";
    }

    void Logger::Log(LogLevel lvl, const char* fmt, ...) {
        if (static_cast<int>(lvl) < static_cast<int>(_lvl.load(std::memory_order_relaxed))) return;

        char msg[2048]{};
        va_list ap; va_start(ap, fmt);
        _vsnprintf_s(msg, sizeof(msg), _TRUNCATE, fmt, ap);
        va_end(ap);

        std::ostringstream line;
        line << timestamp() << " [" << lvlName(lvl) << "] " << msg;

        std::scoped_lock lk{ _mtx };
        writeUnlocked(line.str());
    }

    void Logger::LogErr(const char* fmt, ...) {
        char msg[2048]{};
        va_list ap; va_start(ap, fmt);
        _vsnprintf_s(msg, sizeof(msg), _TRUNCATE, fmt, ap);
        va_end(ap);

        std::ostringstream line;
        line << timestamp() << " [ERROR] " << msg;

        std::scoped_lock lk{ _mtx };
        writeUnlocked(line.str());
    }

    void InitLogs() {
        const auto dir = GetPluginFolder() / "logs"; // .../MirrorBladeBridge/logs
        g_logger.Init(dir); // relies on default args declared in MBLog.hpp
    }

    void ShutdownLogs() {
        // No-op; file handles are short-lived inside writeUnlocked().
    }

} // namespace MB
