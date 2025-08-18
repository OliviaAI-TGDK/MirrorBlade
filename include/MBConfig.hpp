#pragma once
#include <atomic>
#include <filesystem>
#include <string>

namespace MB {

    struct Config {
        enum class LogLevel { Trace, Debug, Info, Warn, Error };

        // Live values
        std::atomic<bool>     upscaler{ false };
        std::atomic<float>    traffic{ 1.0f };
        std::atomic<bool>     ipcEnabled{ true };
        std::wstring          ipcPipeName{ L"\\\\.\\pipe\\MirrorBladeBridge" };
        std::atomic<LogLevel> logLevel{ LogLevel::Info };

        // --- explicit copy/move (atomics are non-copyable by default) ---
        Config() = default;

        Config(const Config& o)
            : upscaler(o.upscaler.load())
            , traffic(o.traffic.load())
            , ipcEnabled(o.ipcEnabled.load())
            , ipcPipeName(o.ipcPipeName)
            , logLevel(o.logLevel.load()) {
        }

        Config& operator=(const Config& o) {
            if (this != &o) {
                upscaler.store(o.upscaler.load());
                traffic.store(o.traffic.load());
                ipcEnabled.store(o.ipcEnabled.load());
                ipcPipeName = o.ipcPipeName;
                logLevel.store(o.logLevel.load());
            }
            return *this;
        }

        Config(Config&& o) noexcept
            : upscaler(o.upscaler.load())
            , traffic(o.traffic.load())
            , ipcEnabled(o.ipcEnabled.load())
            , ipcPipeName(std::move(o.ipcPipeName))
            , logLevel(o.logLevel.load()) {
        }

        Config& operator=(Config&& o) noexcept {
            if (this != &o) {
                upscaler.store(o.upscaler.load());
                traffic.store(o.traffic.load());
                ipcEnabled.store(o.ipcEnabled.load());
                ipcPipeName = std::move(o.ipcPipeName);
                logLevel.store(o.logLevel.load());
            }
            return *this;
        }

        // file I/O helpers
        static std::filesystem::path ResolveConfigPath();
        static Config LoadFromFile(const std::filesystem::path& path);
        std::string ToJSON() const;
        void ApplyRuntime(); // push live values into subsystems
    };

    // --- global API used by Plugin.cpp and ops ---
    void InitConfig();
    void ShutdownConfig();
    bool ReloadConfig();
    bool SaveConfig();

    const Config& GetConfig();
    void SetConfig(const Config& c);

} // namespace MB
