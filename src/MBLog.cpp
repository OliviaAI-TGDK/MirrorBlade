#include "MBLog.hpp"
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>

namespace {
    std::filesystem::path GetPluginFolder() {
        wchar_t path[MAX_PATH]{};
        HMODULE hm = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCWSTR)&GetPluginFolder, &hm);
        GetModuleFileNameW(hm, path, MAX_PATH);
        return std::filesystem::path(path).parent_path(); // .../plugins/MirrorBladeBridge
    }
}

namespace MB {

    static Logger g_logger;

    Logger& Log() { return g_logger; }

    void Logger::Init(const std::filesystem::path& logDir,
        const std::wstring& base, size_t maxBytes, int keep) {
        std::scoped_lock lk{ _mtx };
        _dir = logDir;
        _base = base;
        _maxBytes = maxBytes;
        _keep = keep;

        std::filesystem::create_directories(_dir);
        _cur = _dir / (std::wstring(_base) + L".log");
        std::ofstream(_cur, std::ios::app | std::ios::binary).close(); // touch
    }

    void Logger::SetLevel(LogLevel lvl) { _lvl.store(lvl); }

    std::string Logger::timestamp() {
        using namespace std::chrono;
        auto now = system_clock::now();
        auto t = system_clock::to_time_t(now);
        auto us = duration_cast<microseconds>(now.time_since_epoch()) % 1'000'000;

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
        auto sz = std::filesystem::file_size(_cur, ec);
        if (ec || sz < _maxBytes) return;

        for (int i = _keep - 1; i >= 1; --i) {
            auto from = _dir / (std::wstring(_base) + L"." + std::to_wstring(i) + L".log");
            auto to = _dir / (std::wstring(_base) + L"." + std::to_wstring(i + 1) + L".log");
            if (std::filesystem::exists(from, ec)) {
                std::filesystem::remove(to, ec);
                std::filesystem::rename(from, to, ec);
            }
        }
        auto to1 = _dir / (std::wstring(_base) + L".1.log");
        std::filesystem::remove(to1, ec);
        std::filesystem::rename(_cur, to1, ec);
        std::ofstream(_cur, std::ios::trunc | std::ios::binary).close();
    }

    void Logger::writeUnlocked(const std::string& line) {
        rotateIfNeededUnlocked();
        std::ofstream f(_cur, std::ios::app | std::ios::binary);
        if (!f) return;
        f.write(line.data(), (std::streamsize)line.size());
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
        if ((int)lvl < (int)_lvl.load()) return;

        char msg[2048];
        va_list ap; va_start(ap, fmt);
        _vsnprintf_s(msg, sizeof(msg), _TRUNCATE, fmt, ap);
        va_end(ap);

        std::ostringstream line;
        line << timestamp() << " [" << lvlName(lvl) << "] " << msg;

        std::scoped_lock lk{ _mtx };
        writeUnlocked(line.str());
    }

    void Logger::LogErr(const char* fmt, ...) {
        char msg[2048];
        va_list ap; va_start(ap, fmt);
        _vsnprintf_s(msg, sizeof(msg), _TRUNCATE, fmt, ap);
        va_end(ap);

        std::ostringstream line;
        line << timestamp() << " [ERROR] " << msg;

        std::scoped_lock lk{ _mtx };
        writeUnlocked(line.str());
    }

    void InitLogs() {
        auto dir = GetPluginFolder() / "logs"; // .../MirrorBladeBridge/logs
        g_logger.Init(dir); // uses defaults from header
    }

    void ShutdownLogs() { /* no-op */ }

} // namespace MB
