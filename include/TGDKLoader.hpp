#pragma once

#include <string>
#include <string_view>
#include <optional>
#include <unordered_map>
#include <vector>
#include <memory>
#include <mutex>

#include <nlohmann/json.hpp>

namespace MB {

    using json = nlohmann::json;

    // ---------- Shared helpers ----------
    struct EquationResult {
        bool        ok{ false };
        double      value{ 0.0 };
        std::string error;
    };

    struct LoaderContext {
        // Optional base environment for variables used in equations
        const json* baseEnv{ nullptr };

        // Returns a numeric variable if present and numeric, else nullopt
        std::optional<double> GetVar(std::string_view name) const;
    };

    // ---------- Service interface ----------
    class ILoaderService {
    public:
        virtual ~ILoaderService() = default;

        virtual std::string Name() const = 0;
        virtual void        Configure(const json& cfg, const LoaderContext& ctx) = 0;
        virtual void        Apply() = 0;
        virtual json        Snapshot() const = 0;
        virtual void        Reset() = 0;
    };

    // ---------- Main loader ----------
    class TGDKLoader {
    public:
        TGDKLoader();

        void Register(std::unique_ptr<ILoaderService> svc);
        void Unregister(std::string_view name);
        ILoaderService* Get(std::string_view name) const;

        void Load(const json& config, const json& env);
        bool LoadFromFile(const std::string& path, const json& env);
        json SnapshotAll() const;

        // Expression evaluator (numbers, identifiers, (), + - * / ^, unary -, abs, min, max, clamp)
        static EquationResult ResolveEquation(std::string_view expr, const json& env);

    private:
        mutable std::mutex _mx;
        std::unordered_map<std::string, std::shared_ptr<ILoaderService>> _services;

        json _lastConfig;
        json _lastEnv;
    };

    // ---------- CompoundLoader ----------
    class CompoundLoader : public ILoaderService {
    public:
        std::string Name() const override { return "compound"; }
        void        Configure(const json& cfg, const LoaderContext& ctx) override;
        void        Apply() override;
        json        Snapshot() const override;
        void        Reset() override;

        std::optional<double> Get(std::string_view entity) const;

    private:
        mutable std::mutex _mx;
        std::unordered_map<std::string, double> _staged;
        std::unordered_map<std::string, double> _values;
    };

    // ---------- ImpoundLoader ----------
    class ImpoundLoader : public ILoaderService {
    public:
        struct Rule {
            std::string tag;
            std::string pattern;
        };

        std::string Name() const override { return "impound"; }
        void        Configure(const json& cfg, const LoaderContext& ctx) override;
        void        Apply() override;
        json        Snapshot() const override;
        void        Reset() override;

        bool IsImpounded(const std::string& name) const;

        // Simple glob match: * and ? supported
        static bool MatchLike(const std::string& text, const std::string& pattern);

    private:
        mutable std::mutex _mx;

        std::vector<std::string> _stagedItems;
        std::vector<Rule>        _stagedRules;

        std::vector<std::string> _items;
        std::vector<Rule>        _rules;
    };

    // ---------- VolumetricPhiLoader ----------
    class VolumetricPhiLoader : public ILoaderService {
    public:
        struct Params {
            bool  enabled = true;
            float distanceMul = 1.0f;
            float densityMul = 1.0f;
            float horizonFade = 0.25f; // 0..1
            float jitterStrength = 1.0f;  // >=0
            float temporalBlend = 0.90f; // 0..1
        };

        std::string Name() const override { return "volumetricPhi"; }
        void        Configure(const json& cfg, const LoaderContext& ctx) override;
        void        Apply() override;
        json        Snapshot() const override;
        void        Reset() override;

        Params Get() const;

    private:
        mutable std::mutex _mx;
        Params _staged;
        Params _live;
    };

} // namespace MB
