#pragma once
#include <string>
#include <functional>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace MB {

    class Ops {
    public:
        using json = nlohmann::json;
        using Handler = std::function<json(const json&)>;

        static Ops& I();              // singleton
        void RegisterAll();           // register supported ops
        json Dispatch(const std::string& op, const json& args);

    private:
        std::unordered_map<std::string, Handler> _map;
    };

} // namespace MB
