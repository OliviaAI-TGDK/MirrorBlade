#include "MBFeatures.hpp"

namespace MB {

    FeatureRegistry& FeatureRegistry::I() {
        static FeatureRegistry gInstance;
        return gInstance;
    }

    FeatureState& FeatureRegistry::_getOrCreate_nolock(const std::string& name) {
        auto it = _map.find(name);
        if (it == _map.end()) {
            auto node = std::make_shared<FeatureState>();
            it = _map.emplace(name, std::move(node)).first;
        }
        return *(it->second);
    }

    FeatureState& FeatureRegistry::Get(const std::string& name) {
        std::lock_guard<std::mutex> _l(_mtx);
        return _getOrCreate_nolock(name);
    }

    void FeatureRegistry::SetEnabled(const std::string& name, bool en) {
        std::lock_guard<std::mutex> _l(_mtx);
        auto& st = _getOrCreate_nolock(name);
        st.enabled.store(en, std::memory_order_relaxed);
        if (en) {
            st.failures.store(0, std::memory_order_relaxed);
        }
    }

    bool FeatureRegistry::IsEnabled(const std::string& name) {
        std::lock_guard<std::mutex> _l(_mtx);
        auto it = _map.find(name);
        if (it == _map.end()) return true; // default: on
        return it->second->enabled.load(std::memory_order_relaxed);
    }

} // namespace MB
