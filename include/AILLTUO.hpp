// include/AILLTUO.hpp
#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <optional>
#include <cstdint>

namespace MB {

    // Forward declaration to avoid heavy includes in the header.
    class LoomisUnderfold;

    // GentuoLM
    // --------
    // A tiny, deterministic text synthesizer. It's NOT a real LLM —
    // just enough structure to create believable NPC utterances that
    // feel reactive to environment values.
    class GentuoLM {
    public:
        GentuoLM();

        // Set seed for deterministic output (use npc name hash, etc).
        void SetSeed(uint64_t seed);

        // Generate a short utterance reacting to "mood" [-1..1] and env signals.
        // The env can contain numeric keys like: "traffic_density", "avg_speed", "alertness".
        std::string GenerateUtterance(std::string_view npcName,
            double mood,
            const std::unordered_map<std::string, double>& env) const;

        // Configure wordbanks (thread-safe via outer AILLTUO mutex; no internal locking)
        void SetAffirmations(std::vector<std::string> words);
        void SetSkeptics(std::vector<std::string> words);
        void SetConnectives(std::vector<std::string> words);
        void SetTrafficTerms(std::vector<std::string> words);

    private:
        // xorshift64* PRNG (deterministic)
        uint64_t nextRand() const;
        size_t   pickIndex(size_t n) const;

    private:
        mutable uint64_t _state;
        std::vector<std::string> _affirm;     // positive phrases
        std::vector<std::string> _skeptic;    // negative phrases
        std::vector<std::string> _connective; // "and", "but", "though", ...
        std::vector<std::string> _traffic;    // "grid", "flow", "merge", ...
    };

    // AILLTUO (AI Loomis Link Truncation Underfold Operator)
    // ------------------------------------------------------
    // Couples underfold geometry with a "Gentuo" text generator and traffic heuristics.
    // 1) Truncates the underfold delta (fold - input) to a hard limit.
    // 2) Introduces a "crooked" nonlinearity to bias offsets (crookedness).
    // 3) Produces "dialectic" NPC chatter correlated with traffic "thoughtfulness".
    class AILLTUO {
    public:
        struct Params {
            bool   enabled = true;
            double truncation = 0.25; // clamps |delta| to this max
            double crookedness = 0.75; // >=0; adds nonlinearity on delta
            double dialecticWeight = 0.6;  // [0..1] likelihood of verbose utterance
            double trafficThoughtFactor = 0.5;  // [0..1] how much traffic reacts to env
        };

        struct NPCOffset {
            double truncatedDelta = 0.0; // clamped underfold delta
            double crookedOffset = 0.0; // bias-applied offset
        };

        struct TrafficDecision {
            // speed and spacing recommendations (non-binding hints)
            double speedMul = 1.0; // multiply driver speed by this
            double spacingMul = 1.0; // multiply headway by this
        };

    public:
        AILLTUO();

        // Wiring (not owned)
        void SetUnderfold(const LoomisUnderfold* uf);

        // Config
        void SetParams(const Params& p);
        Params GetParams() const;

        // JSON configuration (safe to call multiple times)
        // {
        //   "enabled": true,
        //   "truncation": 0.2,
        //   "crookedness": 0.4,
        //   "dialecticWeight": 0.5,
        //   "trafficThoughtFactor": 0.6,
        //   "gentuo": {
        //      "affirm": ["right", "sure", "fine"],
        //      "skeptic": ["nah", "hm", "nope"],
        //      "connect": ["and", "but", "though"],
        //      "traffic": ["grid", "flow", "merge"]
        //   }
        // }
        bool ConfigureFromJSON(const std::string& jsonText, std::string* errorOut = nullptr);

        // Snapshot live configuration to JSON string.
        std::string SnapshotJSON() const;

        // Core: compute NPC offset by sampling the underfold at x.
        NPCOffset EvaluateNPCOffset(double x) const;

        // Bulk evaluate: xs/out arrays of size n; out may alias xs (returns crooked offsets).
        void EvaluateNPCOffsetsMany(const double* xs, double* out, size_t n) const;

        // Traffic heuristic based on density [0..1] and average speed (m/s or normalized).
        TrafficDecision EvaluateTraffic(double density01, double avgSpeed) const;

        // Produce a NPC utterance using GentuoLM. Mood [-1..1]. Env numeric keys optional.
        std::string GenerateNPCUtterance(std::string_view npcName,
            double mood,
            const std::unordered_map<std::string, double>& env) const;

    private:
        static double clamp(double v, double lo, double hi);
        static double sign(double v);

        // Crooked bias: y = d + k * d * |d|
        static double crooked(double d, double k);

    private:
        mutable std::mutex _mx;
        Params             _params;
        const LoomisUnderfold* _underfold; // external, not owned
        GentuoLM           _gentuo;
    };

} // namespace MB
