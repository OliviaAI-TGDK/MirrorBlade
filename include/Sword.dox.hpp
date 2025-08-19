// include/Sword.dox.hpp
#pragma once
/**
 * @file Sword.dox.hpp
 * @brief Doxygen index for MirrorBlade "Sword" developer documentation.
 *
 * This header exists only to host Doxygen groups and long-form comments.
 * It should be included by Sword.dox.cpp so the docs are picked up during
 * compilation, but it does not declare any exported runtime symbols.
 *
 * Groups:
 *   - @ref sword_overview
 *   - @ref runtime_config
 *   - @ref ops_api
 *   - @ref tgdk_loader
 *   - @ref math_trinity
 *   - @ref features
 *   - @ref examples
 */

 /**
  * @defgroup sword_overview Overview
  * @brief High-level architecture.
  *
  * MirrorBladeBridge is a RED4ext plugin providing:
  * - A small Ops RPC surface (JSON in, JSON out).
  * - A config system with atomic write and live reload.
  * - A set of math and motion utilities (Trinity, Figure8Fold).
  * - Optional loaders (Compound, Impound, VolumetricPhi) via TGDKLoader.
  *
  * Key namespaces and files:
  * - `MB::Ops`             — operation registry and dispatch (see TGDKOps.cpp).
  * - `MB::Config`          — persistent configuration (MBConfig.hpp/.cpp).
  * - `MB::MirrorBladeOps`  — live game-facing toggles and diagnostics.
  * - `MB::Trinity`         — vectors and Trideotaxis field (Trinity.hpp/.cpp).
  * - `MB::Figure8Fold`     — figure-8 jitter/jitterX/Y producers.
  * - `MB::TGDKLoader`      — continuity loader with three services.
  */

  /**
   * @defgroup runtime_config Configuration
   * @brief JSON file layout and live reload behavior.
   *
   * The configuration file lives at: `r6/config/MirrorBlade.json`.
   *
   * Example:
   * @code{.json}
   * {
   *   "version": 1,
   *   "upscaler": true,
   *   "trafficBoost": 1.5,
   *   "ipc": {
   *     "enabled": true,
   *     "pipeName": "\\\\.\\pipe\\MirrorBladeBridge"
   *   },
   *   "logging": {
   *     "level": "info"
   *   }
   * }
   * @endcode
   *
   * Behavior:
   * - File writes use an atomic temp-file + MoveFileExW replacement.
   * - A background watcher polls file timestamp with simple debounce.
   * - On reload, live systems are updated via Config::ApplyRuntime().
   */

   /**
    * @defgroup ops_api Ops RPC
    * @brief Public operations exposed via Ops registry.
    *
    * Operations (subject to change):
    * - `upscaler.enable`  — `{ "enabled": bool } -> { "ok": true, "result": bool }`
    * - `traffic.mul`      — `{ "mult": number } -> { "ok": true, "result": float }`
    * - `diag.dump`        — `{ } -> { "ok": true, "result": "<string-json>" }`
    * - `config.reload`    — `{ } -> { "ok": bool }`
    * - `config.save`      — `{ } -> { "ok": bool }`
    * - `ping`             — `{ } -> { "ok": true, "result": "pong" }`
    *
    * Sample:
    * @code{.json}
    * // request
    * { "op":"traffic.mul", "args": { "mult": 2.0 } }
    *
    * // response
    * { "ok": true, "result": 2.0 }
    * @endcode
    */

    /**
     * @defgroup tgdk_loader TGDK Loader
     * @brief Continuity and extension loader with three services.
     *
     * Services:
     * - Compound Loader        — resolves named entities via equations, supports chaining.
     * - Impound Loader         — maintains a blocklist and simple glob rules.
     * - VolumetricPhi Loader   — adjusts volumetric parameters (distance, density, jitter).
     *
     * Config shape:
     * @code{.json}
     * {
     *   "compound": {
     *     "entities": [
     *       { "name":"baseSpeed", "equation":"clamp(60, 0, 120)" },
     *       { "name":"chaseSpeed", "equation":"baseSpeed * 1.25" }
     *     ]
     *   },
     *   "impound": {
     *     "items": ["bad_car_01"],
     *     "rules": [{ "tag":"legacy", "match":"bike_*" }]
     *   },
     *   "volumetricPhi": {
     *     "enabled": true,
     *     "distanceMul": 1.0,
     *     "densityMul": 0.85,
     *     "horizonFade": 0.3,
     *     "jitterStrength": 0.5,
     *     "temporalBlend": 0.9
     *   }
     * }
     * @endcode
     *
     * Equation language (subset):
     * - Literals and identifiers.
     * - Operators: +, -, *, /, ^, unary -.
     * - Functions: abs(x), min(a,b), max(a,b), clamp(x,lo,hi).
     */

     /**
      * @defgroup math_trinity Trinity Math
      * @brief Vector utilities and Trideotaxis field.
      *
      * Vector methods:
      * - Vec2/Vec3: normalization, dot, cross, projection, reflection, rotation,
      *   clamp and set length, angle, lerp, slerp (Vec3).
      *
      * Trideotaxis:
      * - Three attractors A, B, C with weights and 1/r^p falloff.
      * - Swirl around an axis, optional planar constraint, damping.
      * - Small hash noise jitter for natural motion.
      *
      * API:
      * @code{.cpp}
      * using namespace MB::Trinity;
      * TrideotaxisParams P;
      * P.A = { 0, 0, 0 }; P.B = { 5, 0, 0 }; P.C = { -5, 0, 0 };
      * P.wA = 1.0f; P.wB = 0.8f; P.wC = 0.6f;
      * P.falloffPow = 1.0f;
      * P.maxAccel = 20.0f; P.maxSpeed = 15.0f; P.damping = 0.05f;
      * P.swirlAxis = { 0, 1, 0 }; P.swirlStrength = 0.2f;
      *
      * Vec3 pos{0,0,10}, vel{0,0,0};
      * for (int i=0;i<600;++i) {
      *   IntegrateTrideotaxis(pos, vel, P, 1.0f/60.0f, i/60.0f);
      * }
      * @endcode
      */

      /**
       * @defgroup features Feature Surfaces
       * @brief Higher-level features built on math primitives.
       *
       * Figure8Fold:
       * - Produces jitterX, jitterY along a figure-8 (lemniscate) pattern
       *   with frame-independent evolution.
       *
       * Volumetric Infinitizer:
       * - Utility for combining fog/volumetric params to avoid banding,
       *   maintain apparent depth at extreme view distances, and stabilize
       *   temporal accumulation.
       */

       /**
        * @defgroup examples Examples
        * @brief Useful snippets.
        *
        * Save config:
        * @code{.cpp}
        * MB::Config cfg = MB::GetConfig();
        * cfg.upscaler.store(true);
        * MB::SetConfig(cfg);
        * MB::SaveConfig();
        * @endcode
        *
        * Dispatch op:
        * @code{.cpp}
        * nlohmann::json args = { {"enabled", true} };
        * nlohmann::json out  = MB::Ops::I().Dispatch("upscaler.enable", args);
        * @endcode
        */

namespace MB {
    namespace Docs {
        // Empty namespace solely to provide a compile anchor in Sword.dox.cpp
    }
} // namespace MB::Docs
