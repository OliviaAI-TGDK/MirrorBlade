#pragma once
#include <string>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <functional>
#include <memory>   // <-- needed for std::shared_ptr
#include <utility>

namespace MB {

    struct FeatureState {
        std::atomic<bool> enabled{ true };
        std::atomic<int>  failures{ 0 };
        int failThreshold = 3;      // after N failures, auto-disable
    };

    class FeatureRegistry {
    public:
        // Singleton access
        static FeatureRegistry& I();

        // Returns a reference to the FeatureState for `name`, creating it if missing.
        FeatureState& Get(const std::string& name);

        void SetEnabled(const std::string& name, bool en);
        bool IsEnabled(const std::string& name);

        // Guard: runs fn if enabled; catches exceptions; auto-disables on threshold
        template <typename Fn>
        void GuardedRun(const std::string& name, Fn&& fn, const char* context = nullptr) {
            // First: quick enabled check under lock
            {
                std::lock_guard<std::mutex> _l(_mtx);
                auto& st = _getOrCreate_nolock(name);
                if (!st.enabled.load(std::memory_order_relaxed)) {
                    return; // disabled → no-op
                }
            }

            // Execute outside the lock
            bool ok = true;
            try {
                std::invoke(std::forward<Fn>(fn));
            }
            catch (...) {
                ok = false;
            }

            // Update failure counters / auto-disable under lock
            if (!ok) {
                std::lock_guard<std::mutex> _l(_mtx);
                auto& st = _getOrCreate_nolock(name);
                int f = st.failures.fetch_add(1, std::memory_order_relaxed) + 1;
                if (f >= st.failThreshold) {
                    st.enabled.store(false, std::memory_order_relaxed);
                }
                (void)context; // (hook for logging if you add it later)
            }
        }

    private:
        // Private ctor for singleton
        FeatureRegistry() = default;

        // Internal: returns reference to FeatureState, creating entry if missing.
        FeatureState& _getOrCreate_nolock(const std::string& name);

        std::mutex _mtx;
        std::unordered_map<std::string, std::shared_ptr<FeatureState>> _map;
    };

    // Convenience macro
#define MB_GUARDED(featureName, lambdaOrStmt) \
    ::MB::FeatureRegistry::I().GuardedRun((featureName), [&](){ lambdaOrStmt; }, __FUNCTION__)

} // namespace MB
