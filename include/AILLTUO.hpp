// include/AILLTUO.hpp
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <mutex>

namespace MB {

    // Forward declaration to avoid a hard include here.
    class LoomisUnderfold;

    // -----------------------------------------------------------------------------
    // GentuoLM: tiny, deterministic word-picker / utterance generator
    // -----------------------------------------------------------------------------
    class GentuoLM {
    public:
        GentuoLM();

        // Optional: seed the internal RNG (xorshift64*). Zero resets to default.
        void SetSeed(uint64_t seed);

        void SetAffirmations(std::vector<std::string> words);
        void SetSkeptics(std::vector<std::string> words);
        void SetConnectives(std::vector<std::string> words);
        void SetTrafficTerms(std::vector<std::string> words);

        // Build a short, stylized utterance using name/mood/env.
        std::string GenerateUtterance(std::string_view npcName,
            double mood,
            const std::unordered_map<std::string, double>& env) const;

    private:
        // RNG state; mutated in const nextRand() for convenience.
        mutable uint64_t _state;

        uint64_t nextRand() const;
        size_t   pickIndex(size_t n) const;

        std::vector<std::string> _affirm;
        std::vector<std::string> _skeptic;
        std::vector<std::string> _connective;
        std::vector<std::string> _traffic;
    };

    // -----------------------------------------------------------------------------
    // AILLTUO: AI Loomis-Like Truncated Underfold Orchestrator
    // -----------------------------------------------------------------------------
    class AILLTUO {
    public:
        struct Params {
            bool   enabled = true;
            double truncation = 1.0;  // clamp |delta| before crookedness
            double crookedness = 0.25; // odd nonlinearity strength
            double dialecticWeight = 0.5;  // 0..1, currently advisory
            double trafficThoughtFactor = 0.5;  // 0..1, scales traffic response
        };

        struct NPCOffset {
            double truncated = 0.0; // clamped delta
            double crooked = 0.0; // post-nonlinearity
        };

        struct TrafficDecision {
            double speedMultiplier = 1.0;
            double spacingMultiplier = 1.0;
        };

        struct FoldResult {
            double specimen = 0.0;
            double curvature = 0.0;
        };

        AILLTUO();

        // Wiring
        void   SetUnderfold(const LoomisUnderfold* uf);
        void   SetParams(const Params& p);
        Params GetParams() const;

        // Evaluation
        NPCOffset       EvaluateNPCOffset(double x) const;
        void            EvaluateNPCOffsetsMany(const double* xs, double* out, size_t n) const;
        TrafficDecision EvaluateTraffic(double density01, double avgSpeed) const;

        // Dialogue
        std::string GenerateNPCUtterance(std::string_view npcName,
            double mood,
            const std::unordered_map<std::string, double>& env) const;

        // Configuration / Introspection
        bool        ConfigureFromJSON(const std::string& jsonText, std::string* errorOut);
        std::string SnapshotJSON() const;

    private:
        // Helpers
        static double clamp(double v, double lo, double hi);
        static double sign(double v);
        static double crooked(double d, double k);

        mutable std::mutex _mx;
        const LoomisUnderfold* _underfold = nullptr;
        Params   _params{};
        GentuoLM _gentuo;
    };

} // namespace MB
