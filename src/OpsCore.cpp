// src/OpsCore.cpp
#include <unordered_map>
#include <mutex>
#include <string>
#include <functional>
#include <nlohmann/json.hpp>

// ---- Minimal, compatible declaration of MB::Ops ----
// (Keep in sync with your header; this just ensures this TU knows the type.)
namespace MB {
    class Ops {
    public:
        static Ops& I();

        void Register(const std::string& name,
            std::function<nlohmann::json(const nlohmann::json&)> fn);

        nlohmann::json Dispatch(const std::string& name,
            const nlohmann::json& payload);
    };
} // namespace MB

// ---- Internal registry used by the implementation ----
namespace {
    using json = nlohmann::json;

    std::unordered_map<std::string, std::function<json(const json&)>>& registry() {
        static std::unordered_map<std::string, std::function<json(const json&)>> r;
        return r;
    }
    std::mutex& regMutex() {
        static std::mutex m;
        return m;
    }
}

// ---- Definitions ----
namespace MB {

    Ops& Ops::I() {
        static Ops inst;
        return inst;
    }

    void Ops::Register(const std::string& name,
        std::function<nlohmann::json(const nlohmann::json&)> fn) {
        std::scoped_lock lk(regMutex());
        registry()[name] = std::move(fn);
    }

    nlohmann::json Ops::Dispatch(const std::string& name, const nlohmann::json& payload) {
        std::function<nlohmann::json(const nlohmann::json&)> fn;
        {
            std::scoped_lock lk(regMutex());
            auto it = registry().find(name);
            if (it == registry().end()) {
                return nlohmann::json{ {"error","op_not_found"},{"name",name} };
            }
            fn = it->second;
        }
        return fn ? fn(payload) : nlohmann::json{ {"error","op_null"},{"name",name} };
    }

} // namespace MB
