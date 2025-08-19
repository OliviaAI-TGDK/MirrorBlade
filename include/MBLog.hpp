#pragma once

// Scrub MIDL/Windows macro landmines that break <cstdarg>/<string>
#ifdef string
#  undef string
#endif
#ifdef small
#  undef small
#endif
#ifdef hyper
#  undef hyper
#endif
#ifdef uuid
#  undef uuid
#endif
#ifdef near
#  undef near
#endif
#ifdef far
#  undef far
#endif
#ifdef min
#  undef min
#endif
#ifdef max
#  undef max
#endif

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <filesystem>
#include <mutex>
#include <string>

namespace MB {

    enum class LogLevel {
        Trace = 0,
        Debug = 1,
        Info = 2,
        Warn = 3,
        Error = 4
    };

    class Logger {
    public:
        void Init(const std::filesystem::path& logDir,
            const std::wstring& base = L"MirrorBladeBridge",
            std::size_t maxBytes = 2 * 1024 * 1024,
            int keep = 5);

        void SetLevel(LogLevel lvl);

        void Log(LogLevel lvl, const char* fmt, ...);
        void LogErr(const char* fmt, ...);

    private:
        std::string timestamp();
        void rotateIfNeededUnlocked();
        void writeUnlocked(const std::string& line);

        std::mutex _mtx{};
        std::filesystem::path _dir{};
        std::filesystem::path _cur{};
        std::wstring _base{ L"MirrorBladeBridge" };
        std::size_t _maxBytes{ 2 * 1024 * 1024 };
        int _keep{ 5 };
        std::atomic<LogLevel> _lvl{ LogLevel::Info };
    };

    // Global accessor
    Logger& Log();

    // Optional lifecycle helpers
    void InitLogs();
    void ShutdownLogs();

} // namespace MB
