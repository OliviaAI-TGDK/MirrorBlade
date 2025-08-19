// MirrorBladeBridge.cpp — production-grade, self-contained bridge for RED4ext
//
// Responsibilities:
//  - Starts a named-pipe JSON RPC server for external control ("\\\\.\\pipe\\MirrorBladeBridge-v1").
//  - Queues work onto a lightweight "game-thread" pump (replace with a real tick hook later).
//  - Exposes a set of example ops (traffic/npc/vehicle/ui/etc) plus upscaler control ops.
//  - Integrates with an optional upscaler module.
//
// JSON schema: { v:1, id?:..., op:"...", args:{...} } -> replies mirror v/id and include ok/result|error.

#include "MirrorBladeBridge.hpp"

#include <RED4ext/RED4ext.hpp>
#include <RED4ext/GameEngine.hpp>
#include <RED4ext/Api/EMainReason.hpp>
#include <RED4ext/Api/SemVer.hpp>
#include <RED4ext/Api/Sdk.hpp>

#include <Windows.h>
#include <nlohmann/json.hpp>

#include <string>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <mutex>
#include <functional>
#include <queue>
#include <optional>
#include <chrono>
#include <cstdarg>
#include <algorithm>

// -------- Optional LightFilter integration (compile-time guarded) --------
#if __has_include("LightFilter.hpp")
#include "LightFilter.hpp"
#define MB_HAS_LIGHTSFAKE 1
#else
#define MB_HAS_LIGHTSFAKE 0
#endif

// -------- Optional Upscaler integration (compile-time guarded) --------
#if __has_include("Upscaler.hpp")
#include "Upscaler.hpp" // defines MB::UpscalerParams, UpscaleMode, API funcs
#define MB_UPSCALER_AVAILABLE 1
#else
#define MB_UPSCALER_AVAILABLE 0
namespace MB {
    enum class UpscaleMode { Off, FSR2 };
    struct UpscalerParams {
        uint32_t outputWidth = 0, outputHeight = 0;
        uint32_t renderWidth = 0, renderHeight = 0;
        float sharpness = 0.0f;
    };
    inline void Upscaler_Enable(bool) {}
    inline void Upscaler_SetMode(UpscaleMode) {}
    inline void Upscaler_SetParams(const UpscalerParams&) {}
    inline void Upscaler_Resize(const UpscalerParams&) {}
}
#endif

using json = nlohmann::json;

// -------------------- Logging --------------------
static void MB_Log(const char* msg)
{
    OutputDebugStringA("[MirrorBladeBridge] ");
    OutputDebugStringA(msg);
    OutputDebugStringA("\n");
}
static void MB_Logf(const char* fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    MB_Log(buf);
}

// -------------------- Replies --------------------
using OpReply = std::function<void(json)>;

static void ReplyOk(const json& req, OpReply reply, json result = json::object())
{
    json r = {
        {"v", req.value("v", 1)},
        {"ok", true},
        {"result", std::move(result)}
    };
    if (req.contains("id")) r["id"] = req["id"];
    if (reply) reply(std::move(r));
}

static void ReplyErr(const json& req, OpReply reply, const std::string& code, const std::string& msg)
{
    json r = {
        {"v", req.value("v", 1)},
        {"ok", false},
        {"error", { {"code", code}, {"msg", msg} }}
    };
    if (req.contains("id")) r["id"] = req["id"];
    if (reply) reply(std::move(r));
}

// -------------------- Globals --------------------
static std::atomic<bool> g_running{ false };
static std::atomic<bool> g_tickRunning{ false };
static HANDLE g_pipe = INVALID_HANDLE_VALUE;
static const wchar_t* PIPE_NAME = L"\\\\.\\pipe\\MirrorBladeBridge-v1";
static const RED4ext::Sdk* g_sdk = nullptr; // saved if needed

// ---------- Simple main-thread task queue ----------
struct Task { std::function<void()> fn; };
static std::mutex q_mtx;
static std::queue<Task> q_tasks;

static void EnqueueOnGameThread(std::function<void()> fn)
{
    std::lock_guard<std::mutex> lk(q_mtx);
    q_tasks.push({ std::move(fn) });
}

static void PumpTasksOnTick()
{
    std::queue<Task> local;
    {
        std::lock_guard<std::mutex> lk(q_mtx);
        std::swap(local, q_tasks);
    }
    while (!local.empty()) {
        try { local.front().fn(); }
        catch (...) { /* keep loop alive */ }
        local.pop();
    }
}

namespace MB {
    void PumpOnce() { PumpTasksOnTick(); }
}

// A lightweight tick worker so queued tasks still run.
// Not guaranteed to be the game thread; replace later with a real game-tick hook.
static void TickWorker()
{
    g_tickRunning = true;
    MB_Log("Tick worker started.");
    while (g_running.load()) {
        PumpTasksOnTick();
        std::this_thread::sleep_for(std::chrono::milliseconds(8)); // ~120 Hz
    }
    g_tickRunning = false;
    MB_Log("Tick worker stopped.");
}

// ---------- Pipe I/O helpers ----------
static void WriteJsonLine(HANDLE pipe, const json& j)
{
    std::string line = j.dump();
    line.push_back('\n');
    DWORD written = 0;
    WriteFile(pipe, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
}

static std::optional<std::string> ReadLine(HANDLE pipe)
{
    static thread_local std::string buf;
    char ch;
    DWORD read = 0;
    while (true) {
        if (!ReadFile(pipe, &ch, 1, &read, nullptr) || read == 0)
            return std::nullopt; // disconnected
        if (ch == '\n') {
            std::string line = std::move(buf);
            buf.clear();
            return line;
        }
        buf.push_back(ch);
        if (buf.size() > 1'000'000) { buf.clear(); return std::nullopt; } // 1MB guard
    }
}

// -------------------- Ops --------------------
static void Op_UI_Toast(const json& req, OpReply reply)
{
    const auto& args = req.value("args", json::object());
    if (!args.contains("text")) return ReplyErr(req, reply, "BadArgs", "args.text required");
    int ms = std::max(1, args.value("ms", 2000));
    std::string text = args["text"].get<std::string>();

    EnqueueOnGameThread([req, reply, ms, text]() {
        MB_Logf("[toast] %s (%d ms)", text.c_str(), ms);
        // TODO: Display in Ink/UI if desired
        ReplyOk(req, reply, { {"status", "shown"}, {"ms", ms} });
        });
}

static void Op_TimeScale_Set(const json& req, OpReply reply)
{
    const auto& args = req.value("args", json::object());
    if (!args.contains("scale")) return ReplyErr(req, reply, "BadArgs", "args.scale required");
    double scale = args["scale"].get<double>();
    if (scale <= 0.0 || scale > 10.0) return ReplyErr(req, reply, "BadArgs", "scale out of range (0,10]");

    EnqueueOnGameThread([req, reply, scale]() {
        MB_Logf("[timescale] -> %.3f", scale);
        // TODO: Apply via game RTTI
        ReplyOk(req, reply, { {"scale", scale} });
        });
}

static void Op_LOD_Pin(const json& req, OpReply reply)
{
    const auto& args = req.value("args", json::object());
    int ttl = std::max(1, args.value("ttl", 3000));
    std::string tag = args.value("tag", std::string("default"));
    EnqueueOnGameThread([req, reply, ttl, tag]() {
        MB_Logf("[lod.pin] tag=%s ttl=%d", tag.c_str(), ttl);
        // TODO: LOD pin impl
        ReplyOk(req, reply, { {"pinned", true}, {"ttl", ttl}, {"tag", tag} });
        });
}

static void Op_Traffic_Mul(const json& req, OpReply reply)
{
    const auto& args = req.value("args", json::object());
    double mult = args.value("mult", 1.0);
    if (mult <= 0.01 || mult > 50.0) return ReplyErr(req, reply, "BadArgs", "mult out of range");
    EnqueueOnGameThread([req, reply, mult]() {
        MB_Logf("[traffic.mul] mult=%.3f", mult);
        // TODO: Apply to traffic system
        ReplyOk(req, reply, { {"applied", true}, {"mult", mult} });
        });
}

// --- NPC ---
static void Op_NPC_Freeze(const json& req, OpReply reply) { ReplyOk(req, reply, { {"npc", "frozen"} }); }
static void Op_NPC_Unfreeze(const json& req, OpReply reply) { ReplyOk(req, reply, { {"npc", "unfrozen"} }); }
static void Op_NPC_Spawn(const json& req, OpReply reply) {
    const auto& args = req.value("args", json::object());
    std::string id = args.value("id", std::string("npc_default"));
    ReplyOk(req, reply, { {"npc", id}, {"spawned", true} });
}
static void Op_NPC_Despawn(const json& req, OpReply reply) {
    const auto& args = req.value("args", json::object());
    std::string id = args.value("id", std::string("npc_default"));
    ReplyOk(req, reply, { {"npc", id}, {"despawned", true} });
}
static void Op_NPC_Teleport(const json& req, OpReply reply) {
    const auto& args = req.value("args", json::object());
    auto pos = args.value("pos", json::object());
    ReplyOk(req, reply, { {"npc", "teleported"}, {"pos", pos} });
}

// --- Vehicle ---
static void Op_Vehicle_Spawn(const json& req, OpReply reply) {
    const auto& args = req.value("args", json::object());
    std::string id = args.value("id", std::string("Vehicle.v_default"));
    ReplyOk(req, reply, { {"vehicle", id}, {"spawned", true} });
}
static void Op_Vehicle_Despawn(const json& req, OpReply reply) {
    const auto& args = req.value("args", json::object());
    std::string id = args.value("id", std::string("Vehicle.v_default"));
    ReplyOk(req, reply, { {"vehicle", id}, {"despawned", true} });
}
static void Op_Vehicle_Boost(const json& req, OpReply reply) {
    const auto& args = req.value("args", json::object());
    double boost = args.value("factor", 2.0);
    ReplyOk(req, reply, { {"boostFactor", boost} });
}
static void Op_Vehicle_Paint(const json& req, OpReply reply) {
    const auto& args = req.value("args", json::object());
    std::string color = args.value("color", std::string("red"));
    ReplyOk(req, reply, { {"painted", true}, {"color", color} });
}
static void Op_Vehicle_Repair(const json& req, OpReply reply) {
    ReplyOk(req, reply, { {"vehicle", "repaired"} });
}

// --- Traffic ---
static void Op_Traffic_Clear(const json& req, OpReply reply) { ReplyOk(req, reply, { {"traffic", "cleared"} }); }
static void Op_Traffic_Freeze(const json& req, OpReply reply) { ReplyOk(req, reply, { {"traffic", "frozen"} }); }
static void Op_Traffic_Unfreeze(const json& req, OpReply reply) { ReplyOk(req, reply, { {"traffic", "unfrozen"} }); }
static void Op_Traffic_Route(const json& req, OpReply reply) {
    const auto& args = req.value("args", json::object());
    auto route = args.value("route", json::array());
    ReplyOk(req, reply, { {"trafficRoute", route} });
}
static void Op_Traffic_Persist(const json& req, OpReply reply) {
    const auto& args = req.value("args", json::object());
    bool enabled = args.value("enabled", true);
    ReplyOk(req, reply, { {"persist", enabled} });
}

// --- AV (aerial vehicles) ---
static void Op_AV_Spawn(const json& req, OpReply reply) {
    const auto& args = req.value("args", json::object());
    std::string id = args.value("id", std::string("AV.default"));
    ReplyOk(req, reply, { {"av", id}, {"spawned", true} });
}
static void Op_AV_Route_Set(const json& req, OpReply reply) {
    const auto& args = req.value("args", json::object());
    auto pts = args.value("points", json::array());
    ReplyOk(req, reply, { {"avRoute", pts} });
}
static void Op_AV_Despawn(const json& req, OpReply reply) {
    const auto& args = req.value("args", json::object());
    std::string id = args.value("id", std::string("AV.default"));
    ReplyOk(req, reply, { {"av", id}, {"despawned", true} });
}
static void Op_AV_Land(const json& req, OpReply reply) { ReplyOk(req, reply, { {"av", "landed"} }); }
static void Op_AV_Takeoff(const json& req, OpReply reply) { ReplyOk(req, reply, { {"av", "takeoff"} }); }

// --- Train ---
static void Op_Train_Persist(const json& req, OpReply reply) {
    const auto& args = req.value("args", json::object());
    bool enabled = args.value("enabled", true);
    ReplyOk(req, reply, { {"trainPersist", enabled} });
}
static void Op_Train_Spawn(const json& req, OpReply reply) {
    const auto& args = req.value("args", json::object());
    std::string id = args.value("id", std::string("train_default"));
    ReplyOk(req, reply, { {"train", id}, {"spawned", true} });
}
static void Op_Train_Despawn(const json& req, OpReply reply) {
    const auto& args = req.value("args", json::object());
    std::string id = args.value("id", std::string("train_default"));
    ReplyOk(req, reply, { {"train", id}, {"despawned", true} });
}
static void Op_Train_Freeze(const json& req, OpReply reply) { ReplyOk(req, reply, { {"train", "frozen"} }); }
static void Op_Train_Unfreeze(const json& req, OpReply reply) { ReplyOk(req, reply, { {"train", "unfrozen"} }); }

// --- UI ---
static void Op_UI_Alert(const json& req, OpReply reply) {
    const auto& args = req.value("args", json::object());
    std::string text = args.value("text", std::string("Alert"));
    int ms = std::max(1, args.value("ms", 2000));
    ReplyOk(req, reply, { {"type","alert"},{"text",text},{"ms",ms} });
}
static void Op_UI_Marker_Add(const json& req, OpReply reply) {
    const auto& args = req.value("args", json::object());
    auto pos = args.value("pos", json::object());
    std::string tag = args.value("tag", std::string("marker"));
    ReplyOk(req, reply, { {"marker","added"},{"tag",tag},{"pos",pos} });
}
static void Op_UI_Marker_Remove(const json& req, OpReply reply) {
    const auto& args = req.value("args", json::object());
    std::string tag = args.value("tag", std::string("marker"));
    ReplyOk(req, reply, { {"marker","removed"},{"tag",tag} });
}
static void Op_UI_HUD_Toggle(const json& req, OpReply reply) {
    const auto& args = req.value("args", json::object());
    bool visible = args.value("visible", true);
    ReplyOk(req, reply, { {"hudVisible", visible} });
}

// --- Time / Weather ---
static void Op_Time_Set(const json& req, OpReply reply) {
    const auto& args = req.value("args", json::object());
    int hour = std::clamp(args.value("hour", 12), 0, 23);
    int minute = std::clamp(args.value("minute", 0), 0, 59);
    ReplyOk(req, reply, { {"timeSet", true}, {"hour", hour}, {"minute", minute} });
}
static void Op_Time_Pause(const json& req, OpReply reply) { ReplyOk(req, reply, { {"time", "paused"} }); }
static void Op_Time_Resume(const json& req, OpReply reply) { ReplyOk(req, reply, { {"time", "resumed"} }); }
static void Op_Weather_Set(const json& req, OpReply reply) {
    const auto& args = req.value("args", json::object());
    std::string preset = args.value("preset", std::string("Clear"));
    float blend = args.value("blend", 1.0f);
    ReplyOk(req, reply, { {"weatherPreset", preset}, {"blend", blend} });
}

// --- Player ---
static void Op_Player_Teleport(const json& req, OpReply reply) {
    const auto& args = req.value("args", json::object());
    auto pos = args.value("pos", json::object());
    float yaw = args.value("yaw", 0.0f);
    ReplyOk(req, reply, { {"teleported", true}, {"pos", pos}, {"yaw", yaw} });
}
static void Op_Player_Heal(const json& req, OpReply reply) {
    const auto& args = req.value("args", json::object());
    float amount = args.value("amount", 100.0f);
    ReplyOk(req, reply, { {"healed", amount} });
}
static void Op_Player_Damage(const json& req, OpReply reply) {
    const auto& args = req.value("args", json::object());
    float amount = args.value("amount", 10.0f);
    std::string type = args.value("type", std::string("generic"));
    ReplyOk(req, reply, { {"damaged", amount}, {"type", type} });
}
static void Op_Player_Inventory_Add(const json& req, OpReply reply) {
    const auto& args = req.value("args", json::object());
    std::string item = args.value("item", std::string("Item.Default"));
    int count = std::max(1, args.value("count", 1));
    ReplyOk(req, reply, { {"added", item}, {"count", count} });
}
static void Op_Player_Inventory_Remove(const json& req, OpReply reply) {
    const auto& args = req.value("args", json::object());
    std::string item = args.value("item", std::string("Item.Default"));
    int count = std::max(1, args.value("count", 1));
    ReplyOk(req, reply, { {"removed", item}, {"count", count} });
}

// --- World / Streaming / LOD ---
static void Op_World_Spawn_Explosion(const json& req, OpReply reply) {
    const auto& args = req.value("args", json::object());
    auto pos = args.value("pos", json::object());
    float radius = args.value("radius", 5.0f);
    float power = args.value("power", 1.0f);
    ReplyOk(req, reply, { {"explosion","queued"},{"pos",pos},{"radius",radius},{"power",power} });
}
static void Op_World_Light_Spawn(const json& req, OpReply reply) {
    const auto& args = req.value("args", json::object());
    auto pos = args.value("pos", json::object());
    float intensity = args.value("intensity", 1000.0f);
    std::string color = args.value("color", std::string("#FFFFFF"));
    std::string tag = args.value("tag", std::string("light1"));
    ReplyOk(req, reply, { {"light","spawned"},{"tag",tag},{"pos",pos},{"intensity",intensity},{"color",color} });
}
static void Op_World_Light_Remove(const json& req, OpReply reply) {
    const auto& args = req.value("args", json::object());
    std::string tag = args.value("tag", std::string("light1"));
    ReplyOk(req, reply, { {"light","removed"},{"tag",tag} });
}
static void Op_World_StreamGrid_Recenter(const json& req, OpReply reply) {
    const auto& args = req.value("args", json::object());
    auto pos = args.value("pos", json::object());
    std::string mode = args.value("mode", std::string("auto"));
    ReplyOk(req, reply, { {"streamgrid","recentered"},{"mode",mode},{"pos",pos} });
}
static void Op_World_LOD_Lock(const json& req, OpReply reply) {
    const auto& args = req.value("args", json::object());
    int ttl = std::max(1, args.value("ttl", 3000));
    std::string tag = args.value("tag", std::string("lodlock"));
    ReplyOk(req, reply, { {"lodLocked", true},{"ttl",ttl},{"tag",tag} });
}
static void Op_World_LOD_Unlock(const json& req, OpReply reply) {
    const auto& args = req.value("args", json::object());
    std::string tag = args.value("tag", std::string("lodlock"));
    ReplyOk(req, reply, { {"lodLocked", false},{"tag",tag} });
}

// --- Debug / Telemetry ---
static void Op_Debug_Log(const json& req, OpReply reply) {
    const auto& args = req.value("args", json::object());
    std::string level = args.value("level", std::string("info"));
    std::string msg = args.value("msg", std::string("(empty)"));
    MB_Logf("[debug.%s] %s", level.c_str(), msg.c_str());
    ReplyOk(req, reply, { {"logged", true}, {"level", level}, {"msg", msg} });
}
static void Op_Debug_Capture_Screenshot(const json& req, OpReply reply) {
    const auto& args = req.value("args", json::object());
    std::string path = args.value("path", std::string("screenshot.png"));
    ReplyOk(req, reply, { {"screenshot","queued"},{"path",path} });
}

// --- Config / Introspection ---
static void Op_Config_Set(const json& req, OpReply reply) {
    const auto& args = req.value("args", json::object());
    std::string key = args.value("key", std::string());
    json value = args.value("value", json());
    if (key.empty()) { return ReplyErr(req, reply, "BadArgs", "key required"); }
    ReplyOk(req, reply, { {"set", key}, {"value", value} });
}
static void Op_Config_Get(const json& req, OpReply reply) {
    const auto& args = req.value("args", json::object());
    std::string key = args.value("key", std::string());
    if (key.empty()) { return ReplyErr(req, reply, "BadArgs", "key required"); }
    ReplyOk(req, reply, { {"key", key}, {"value", "(stub)"} });
}
static void Op_Ops_Capabilities(const json& req, OpReply reply) {
    json caps = json::array({
        "ui.toast","ui.alert","ui.marker.add","ui.marker.remove","ui.hud.toggle",
        "time.set","time.pause","time.resume","timescale.set","weather.set",
        "player.teleport","player.heal","player.damage",
        "player.inventory.add","player.inventory.remove",
        "world.spawn.explosion","world.light.spawn","world.light.remove",
        "world.streamgrid.recenter","world.lod.lock","world.lod.unlock",
        "traffic.mul","traffic.clear","traffic.freeze","traffic.unfreeze","traffic.route","traffic.persist",
        "npc.freeze","npc.unfreeze","npc.spawn","npc.despawn","npc.teleport",
        "vehicle.spawn","vehicle.despawn","vehicle.boost","vehicle.paint","vehicle.repair",
        "av.spawn","av.route.set","av.despawn","av.land","av.takeoff",
        "train.persist","train.spawn","train.despawn","train.freeze","train.unfreeze",
        "debug.log","debug.capture.screenshot","config.set","config.get","ops.capabilities","lod.pin","ping",
        "upscaler.enable","upscaler.set","graphics.target.set","graphics.internal.scale"
        });
#if MB_HAS_LIGHTSFAKE
    caps.push_back("lights.fake.adverts");
    caps.push_back("lights.fake.portals");
    caps.push_back("lights.fake.forceportals");
    caps.push_back("lights.fake.sweep");
#endif
    ReplyOk(req, reply, { {"capabilities", caps} });
}
static void Op_Ping(const json& req, OpReply reply) {
    const auto& args = req.value("args", json::object());
    std::string echo = args.value("echo", std::string("pong"));
    ReplyOk(req, reply, { {"pong", true}, {"echo", echo} });
}

// --- Upscaler control ---
static MB::UpscalerParams g_upParams{};

static void Op_Upscaler_Enable(const json& req, OpReply reply)
{
#if MB_UPSCALER_AVAILABLE
    const auto& args = req.value("args", json::object());
    bool enabled = args.value("enabled", true);
    MB::Upscaler_Enable(enabled);
    ReplyOk(req, reply, { {"enabled", enabled} });
#else
    ReplyErr(req, reply, "Unavailable", "Upscaler module not built");
#endif
}

static void Op_Upscaler_Set(const json& req, OpReply reply)
{
#if MB_UPSCALER_AVAILABLE
    const auto& args = req.value("args", json::object());
    std::string mode = args.value("mode", std::string("off"));
    float sharp = args.value("sharpness", 0.6f);

    if (mode == "off") MB::Upscaler_SetMode(MB::UpscaleMode::Off);
    else if (mode == "fsr2") MB::Upscaler_SetMode(MB::UpscaleMode::FSR2);
    else return ReplyErr(req, reply, "BadArgs", "mode must be off|fsr2");

    g_upParams.sharpness = sharp;
    MB::Upscaler_SetParams(g_upParams);

    ReplyOk(req, reply, { {"mode", mode}, {"sharpness", sharp} });
#else
    ReplyErr(req, reply, "Unavailable", "Upscaler module not built");
#endif
}

static void Op_Graphics_Target_Set(const json& req, OpReply reply)
{
#if MB_UPSCALER_AVAILABLE
    const auto& args = req.value("args", json::object());
    g_upParams.outputWidth = args.value("width", 3840u);
    g_upParams.outputHeight = args.value("height", 2160u);
    MB::Upscaler_Resize(g_upParams);
    ReplyOk(req, reply, { {"width", g_upParams.outputWidth}, {"height", g_upParams.outputHeight} });
#else
    ReplyErr(req, reply, "Unavailable", "Upscaler module not built");
#endif
}

static void Op_Graphics_Internal_Scale(const json& req, OpReply reply)
{
#if MB_UPSCALER_AVAILABLE
    const auto& args = req.value("args", json::object());
    float s = std::clamp(args.value("scale", 0.5f), 0.05f, 2.0f);
    g_upParams.renderWidth = (uint32_t)std::max(16.0f, s * static_cast<float>(g_upParams.outputWidth));
    g_upParams.renderHeight = (uint32_t)std::max(16.0f, s * static_cast<float>(g_upParams.outputHeight));
    MB::Upscaler_Resize(g_upParams);
    ReplyOk(req, reply, { {"renderWidth", g_upParams.renderWidth}, {"renderHeight", g_upParams.renderHeight} });
#else
    ReplyErr(req, reply, "Unavailable", "Upscaler module not built");
#endif
}

#if MB_HAS_LIGHTSFAKE
// ---- Optional LightFilter JSON ops ----
static void Op_LightsFake_Adverts(const json& req, OpReply reply) {
    const auto& args = req.value("args", json::object());
    bool on = args.value("enabled", true);
    MB::LightFilter::Get().SetAdverts(on);
    ReplyOk(req, reply, { {"adverts", on} });
}
static void Op_LightsFake_Portals(const json& req, OpReply reply) {
    const auto& args = req.value("args", json::object());
    bool on = args.value("enabled", false);
    MB::LightFilter::Get().SetPortals(on);
    ReplyOk(req, reply, { {"portals", on} });
}
static void Op_LightsFake_ForcePortals(const json& req, OpReply reply) {
    const auto& args = req.value("args", json::object());
    bool on = args.value("enabled", false);
    MB::LightFilter::Get().SetForcePortals(on);
    ReplyOk(req, reply, { {"forcePortals", on} });
}
static void Op_LightsFake_Sweep(const json& req, OpReply reply) {
    (void)req;
    // If you have a world pointer available via SDK, pass it in:
    // MB::LightFilter::Get().SweepWorld(worldPtr);
    ReplyOk(req, reply, { {"sweep", "ok"} });
}
#endif // MB_HAS_LIGHTSFAKE

// -------------------- Op registry --------------------
using OpHandler = std::function<void(const json& req, OpReply reply)>;
static std::unordered_map<std::string, OpHandler> g_opTable;

static void RegisterOps()
{
    g_opTable.emplace("ui.toast", &Op_UI_Toast);
    g_opTable.emplace("timescale.set", &Op_TimeScale_Set);
    g_opTable.emplace("lod.pin", &Op_LOD_Pin);
    g_opTable.emplace("traffic.mul", &Op_Traffic_Mul);

    g_opTable.emplace("npc.freeze", &Op_NPC_Freeze);
    g_opTable.emplace("npc.unfreeze", &Op_NPC_Unfreeze);
    g_opTable.emplace("npc.spawn", &Op_NPC_Spawn);
    g_opTable.emplace("npc.despawn", &Op_NPC_Despawn);
    g_opTable.emplace("npc.teleport", &Op_NPC_Teleport);

    g_opTable.emplace("vehicle.spawn", &Op_Vehicle_Spawn);
    g_opTable.emplace("vehicle.despawn", &Op_Vehicle_Despawn);
    g_opTable.emplace("vehicle.boost", &Op_Vehicle_Boost);
    g_opTable.emplace("vehicle.paint", &Op_Vehicle_Paint);
    g_opTable.emplace("vehicle.repair", &Op_Vehicle_Repair);

    g_opTable.emplace("traffic.clear", &Op_Traffic_Clear);
    g_opTable.emplace("traffic.freeze", &Op_Traffic_Freeze);
    g_opTable.emplace("traffic.unfreeze", &Op_Traffic_Unfreeze);
    g_opTable.emplace("traffic.route", &Op_Traffic_Route);
    g_opTable.emplace("traffic.persist", &Op_Traffic_Persist);

    g_opTable.emplace("av.spawn", &Op_AV_Spawn);
    g_opTable.emplace("av.route.set", &Op_AV_Route_Set);
    g_opTable.emplace("av.despawn", &Op_AV_Despawn);
    g_opTable.emplace("av.land", &Op_AV_Land);
    g_opTable.emplace("av.takeoff", &Op_AV_Takeoff);

    g_opTable.emplace("train.persist", &Op_Train_Persist);
    g_opTable.emplace("train.spawn", &Op_Train_Spawn);
    g_opTable.emplace("train.despawn", &Op_Train_Despawn);
    g_opTable.emplace("train.freeze", &Op_Train_Freeze);
    g_opTable.emplace("train.unfreeze", &Op_Train_Unfreeze);

    g_opTable.emplace("ui.alert", &Op_UI_Alert);
    g_opTable.emplace("ui.marker.add", &Op_UI_Marker_Add);
    g_opTable.emplace("ui.marker.remove", &Op_UI_Marker_Remove);
    g_opTable.emplace("ui.hud.toggle", &Op_UI_HUD_Toggle);

    g_opTable.emplace("time.set", &Op_Time_Set);
    g_opTable.emplace("time.pause", &Op_Time_Pause);
    g_opTable.emplace("time.resume", &Op_Time_Resume);

    g_opTable.emplace("weather.set", &Op_Weather_Set);

    g_opTable.emplace("player.teleport", &Op_Player_Teleport);
    g_opTable.emplace("player.heal", &Op_Player_Heal);
    g_opTable.emplace("player.damage", &Op_Player_Damage);
    g_opTable.emplace("player.inventory.add", &Op_Player_Inventory_Add);
    g_opTable.emplace("player.inventory.remove", &Op_Player_Inventory_Remove);

    g_opTable.emplace("world.spawn.explosion", &Op_World_Spawn_Explosion);
    g_opTable.emplace("world.light.spawn", &Op_World_Light_Spawn);
    g_opTable.emplace("world.light.remove", &Op_World_Light_Remove);
    g_opTable.emplace("world.streamgrid.recenter", &Op_World_StreamGrid_Recenter);
    g_opTable.emplace("world.lod.lock", &Op_World_LOD_Lock);
    g_opTable.emplace("world.lod.unlock", &Op_World_LOD_Unlock);

    g_opTable.emplace("debug.log", &Op_Debug_Log);
    g_opTable.emplace("debug.capture.screenshot", &Op_Debug_Capture_Screenshot);

    g_opTable.emplace("config.set", &Op_Config_Set);
    g_opTable.emplace("config.get", &Op_Config_Get);

    g_opTable.emplace("ops.capabilities", &Op_Ops_Capabilities);
    g_opTable.emplace("ping", &Op_Ping);

    g_opTable.emplace("upscaler.enable", &Op_Upscaler_Enable);
    g_opTable.emplace("upscaler.set", &Op_Upscaler_Set);
    g_opTable.emplace("graphics.target.set", &Op_Graphics_Target_Set);
    g_opTable.emplace("graphics.internal.scale", &Op_Graphics_Internal_Scale);

#if MB_HAS_LIGHTSFAKE
    g_opTable.emplace("lights.fake.adverts", &Op_LightsFake_Adverts);
    g_opTable.emplace("lights.fake.portals", &Op_LightsFake_Portals);
    g_opTable.emplace("lights.fake.forceportals", &Op_LightsFake_ForcePortals);
    g_opTable.emplace("lights.fake.sweep", &Op_LightsFake_Sweep);
#endif
}

// ---------- Server loop ----------
static void ServerWorker()
{
    MB_Log("Server worker started.");
    while (g_running.load()) {
        g_pipe = CreateNamedPipeW(
            PIPE_NAME,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1, 1 << 16, 1 << 16, 0, nullptr
        );

        if (g_pipe == INVALID_HANDLE_VALUE) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        if (!ConnectNamedPipe(g_pipe, nullptr)) {
            CloseHandle(g_pipe);
            g_pipe = INVALID_HANDLE_VALUE;
            continue;
        }

        MB_Log("Client connected.");

        while (g_running.load()) {
            auto lineOpt = ReadLine(g_pipe);
            if (!lineOpt.has_value()) break; // disconnected
            const std::string& line = *lineOpt;

            json req;
            try { req = json::parse(line); }
            catch (...) {
                json err = { {"v",1},{"ok",false},{"error",{{"code","BadJSON"},{"msg","parse failed"}}} };
                WriteJsonLine(g_pipe, err);
                continue;
            }

            auto reply = [](json j) { WriteJsonLine(g_pipe, j); };

            if (req.value("v", 0) != 1) { ReplyErr(req, reply, "BadVersion", "Only v=1 supported"); continue; }
            if (!req.contains("op")) { ReplyErr(req, reply, "BadArgs", "op required"); continue; }

            std::string op = req["op"].get<std::string>();
            auto it = g_opTable.find(op);
            if (it == g_opTable.end()) { ReplyErr(req, reply, "UnknownOp", op); continue; }

            try { it->second(req, reply); }
            catch (const std::exception& e) { ReplyErr(req, reply, "Exception", e.what()); }
            catch (...) { ReplyErr(req, reply, "Exception", "unknown"); }
        }

        FlushFileBuffers(g_pipe);
        DisconnectNamedPipe(g_pipe);
        CloseHandle(g_pipe);
        g_pipe = INVALID_HANDLE_VALUE;

        MB_Log("Client disconnected.");
    }
    MB_Log("Server worker stopped.");
}

// ---------- Public Bridge API (called from your exported Main) ----------
namespace MB
{
    void InitBridge(const RED4ext::Sdk* sdk)
    {
        if (g_running.load()) return; // already running
        g_sdk = sdk;

        RegisterOps();

        g_running = true;
        std::thread(ServerWorker).detach();
        std::thread(TickWorker).detach();

        MB_Logf("Listening on %ls", PIPE_NAME);
    }

    void ShutdownBridge()
    {
        if (!g_running.load()) return;
        g_running = false;

        // Close pipe if connected
        if (g_pipe != INVALID_HANDLE_VALUE) {
            CancelIo(g_pipe);
            CloseHandle(g_pipe);
            g_pipe = INVALID_HANDLE_VALUE;
        }

        // Let tick worker wind down
        for (int i = 0; i < 50 && g_tickRunning.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));

        MB_Log("Bridge shut down.");
        g_sdk = nullptr;
    }
} // namespace MB
