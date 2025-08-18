#pragma once
#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>

namespace MB {

    enum class LogLevel { Trace, Debug, Info, Warn, Error };

    class Logger {
    public:
        // Create/rotate logs under logDir, file name base: <base>.log, with size-based rotation
        void Init(const std::filesystem::path& logDir,
            const std::wstring& base = L"MirrorBladeBridge",
            size_t maxBytes = 2 * 1024 * 1024,  // 2MB
            int keep = 5);

        void SetLevel(LogLevel lvl);

        // printf-style APIs
        void Log(LogLevel lvl, const char* fmt, ...);
        void LogErr(const char* fmt, ...);

    private:
        std::string timestamp();
        void rotateIfNeededUnlocked();
        void writeUnlocked(const std::string& line);

        std::atomic<LogLevel> _lvl{ LogLevel::Info };
        std::mutex _mtx;
        std::filesystem::path _dir;
        std::filesystem::path _cur;
        std::wstring _base{ L"MirrorBladeBridge" };
        size_t _maxBytes{ 2 * 1024 * 1024 };
        int _keep{ 5 };
    };

    // global accessors used throughout the project
    Logger& Log();
    void InitLogs();
    void ShutdownLogs();

} // namespace MB
