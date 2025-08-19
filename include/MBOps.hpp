#pragma once
#include <functional>
#include <string>
#include <unordered_map>
#include <nlohmann/json_fwd.hpp>

namespace MB {

    class Ops {
    public:
        using Handler = std::function<nlohmann::json(const nlohmann::json&)>;

        static Ops& I();

        // Plan A: register a handler
        void Register(const std::string& name, Handler h);

        // Dispatch already exists in your TGDKOps.cpp; keep it.
        nlohmann::json Dispatch(const std::string& op, const nlohmann::json& args);

    private:
        std::unordered_map<std::string, Handler> _map;
    };

} // namespace MB
