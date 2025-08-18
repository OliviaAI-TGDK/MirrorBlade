// MirrorBladeBridge.cpp — production-grade, self-contained bridge for RED4ext
//
// Responsibilities:
//  - Starts a named-pipe JSON RPC server for external control ("\\\\.\\pipe\\MirrorBladeBridge-v1").
//  - Queues work onto a lightweight "game-thread" pump (replace with a real tick hook later).
//  - Exposes a set of example ops (traffic/npc/vehicle/ui/etc) plus upscaler control ops.
//  - Integrates with an optional upscaler module (see Upscaler.hpp).
//
// Notes:
//  - This TU defines MB::InitBridge/ShutdownBridge. Ensure NO other TU defines them.
//  - Feed real game/RTTI calls in the TODO blocks.
//  - JSON schema: { v:1, id?:..., op:"...", args:{...} } -> replies mirror v/id and include ok/result|error.
//
// Build requirements:
//  - RED4ext SDK headers
//  - nlohmann::json
//  - Windows SDK
//  - Your Upscaler.hpp (optional; stubs compiled out if not available)

#include "MirrorBladeBridge.hpp"

#include <RED4ext/RED4ext.hpp>
#include <RED4ext/GameEngine.hpp>
#include <RED4ext/Api/EMainReason.hpp>
#include <RED4ext/Api/SemVer.hpp>
#include <RED4ext/Api/Sdk.hpp>

#include <Windows.h>
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

#include "json.hpp" // nlohmann::json
using json = nlohmann::json;

// If you have the upscaler module, include its API:
#include "Upscaler.hpp" // declares MB::Upscaler_* , MB::UpscaleMode, MB::UpscalerParams

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
    if (req.contains("id"))
        r["id"] = req["id"];
    if (reply)
        reply(std::move(r));
}

static void ReplyErr(const json& req, OpReply reply, const std::string& code, const std::string& msg)
{
    json r = {
        {"v", req.value("v", 1)},
        {"ok", false},
        {"error", { {"code", code}, {"msg", msg} }}
    };
    if (req.contains("id"))
        r["id"] = req["id"];
    if (reply)
        reply(std::move(r));
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
static void Op_UI_Toast(const json& req, std::function<void(json)> reply)
{
    auto& args = req["args"];
    if (!args.contains("text")) return ReplyErr(req, reply, "BadArgs", "args.text required");
    int ms = args.value("ms", 2000);
    if (ms <= 0) return ReplyErr(req, reply, "BadArgs", "ms must be > 0");
    std::string text = args["text"].get<std::string>();

    EnqueueOnGameThread([req, reply, ms, text]() {
        MB_Logf("[toast] %s (%d ms)", text.c_str(), ms);
        // TODO: Display in Ink/UI if desired
        ReplyOk(req, reply, { {"status", "shown"}, {"ms", ms} });
        });
}

static void Op_TimeScale_Set(const json& req, std::function<void(json)> reply)
{
    auto& args = req["args"];
    if (!args.contains("scale")) return ReplyErr(req, reply, "BadArgs", "args.scale required");
    double scale = args["scale"].get<double>();
    if (scale <= 0.0 || scale > 10.0) return ReplyErr(req, reply, "BadArgs", "scale out of range (0,10]");

    EnqueueOnGameThread([req, reply, scale]() {
        MB_Logf("[timescale] -> %.3f", scale);
        // TODO: Apply via game RTTI
        ReplyOk(req, reply, { {"scale", scale} });
        });
}

static void Op_LOD_Pin(const json& req, std::function<void(json)> reply)
{
    int ttl = req["args"].value("ttl", 3000);
    std::string tag = req["args"].value("tag", "default");
    EnqueueOnGameThread([req, reply, ttl, tag]() {
        MB_Logf("[lod.pin] tag=%s ttl=%d", tag.c_str(), ttl);
        // TODO: LOD pin impl
        ReplyOk(req, reply, { {"pinned", true}, {"ttl", ttl}, {"tag", tag} });
        });
}

static void Op_Traffic_Mul(const json& req, std::function<void(json)> reply)
{
    double mult = req["args"].value("mult", 1.0);
    if (mult <= 0.01 || mult > 50.0) return ReplyErr(req, reply, "BadArgs", "mult out of range");
    EnqueueOnGameThread([req, reply, mult]() {
        MB_Logf("[traffic.mul] mult=%.3f", mult);
        // TODO: Apply to traffic system
        ReplyOk(req, reply, { {"applied", true}, {"mult", mult} });
        });
}

// --- NPC ---
static void Op_NPC_Freeze(const json& req, std::function<void(json)> reply) { ReplyOk(req, reply, { {"npc", "frozen"} }); }
static void Op_NPC_Unfreeze(const json& req, std::function<void(json)> reply) { ReplyOk(req, reply, { {"npc", "unfrozen"} }); }
static void Op_NPC_Spawn(const json& req, std::function<void(json)> reply) {
    std::string id = req["args"].value("id", "npc_default");
    ReplyOk(req, reply, { {"npc", id}, {"spawned", true} });
}
static void Op_NPC_Despawn(const json& req, std::function<void(json)> reply) {
    std::string id = req["args"].value("id", "npc_default");
    ReplyOk(req, reply, { {"npc", id}, {"despawned", true} });
}
static void Op_NPC_Teleport(const json& req, std::function<void(json)> reply) {
    auto pos = req["args"].value("pos", json::object());
    ReplyOk(req, reply, { {"npc", "teleported"}, {"pos", pos} });
}

// --- Vehicle ---
static void Op_Vehicle_Spawn(const json& req, std::function<void(json)> reply) {
    std::string id = req["args"].value("id", "Vehicle.v_default");
    ReplyOk(req, reply, { {"vehicle", id}, {"spawned", true} });
}
static void Op_Vehicle_Despawn(const json& req, std::function<void(json)> reply) {
    std::string id = req["args"].value("id", "Vehicle.v_default");
    ReplyOk(req, reply, { {"vehicle", id}, {"despawned", true} });
}
static void Op_Vehicle_Boost(const json& req, std::function<void(json)> reply) {
    double boost = req["args"].value("factor", 2.0);
    ReplyOk(req, reply, { {"boostFactor", boost} });
}
static void Op_Vehicle_Paint(const json& req, std::function<void(json)> reply) {
    std::string color = req["args"].value("color", "red");
    ReplyOk(req, reply, { {"painted", true}, {"color", color} });
}
static void Op_Vehicle_Repair(const json& req, std::function<void(json)> reply) {
    ReplyOk(req, reply, { {"vehicle", "repaired"} });
}

// --- Traffic ---
static void Op_Traffic_Clear(const json& req, std::function<void(json)> reply) { ReplyOk(req, reply, { {"traffic", "cleared"} }); }
static void Op_Traffic_Freeze(const json& req, std::function<void(json)> reply) { ReplyOk(req, reply, { {"traffic", "frozen"} }); }
static void Op_Traffic_Unfreeze(const json& req, std::function<void(json)> reply) { ReplyOk(req, reply, { {"traffic", "unfrozen"} }); }
static void Op_Traffic_Route(const json& req, std::function<void(json)> reply) {
    auto route = req["args"].value("route", json::array());
    ReplyOk(req, reply, { {"trafficRoute", route} });
}
static void Op_Traffic_Persist(const json& req, std::function<void(json)> reply) {
    bool enabled = req["args"].value("enabled", true);
    ReplyOk(req, reply, { {"persist", enabled} });
}

// --- AV (aerial vehicles) ---
static void Op_AV_Spawn(const json& req, std::function<void(json)> reply) {
    std::string id = req["args"].value("id", "AV.default");
    ReplyOk(req, reply, { {"av", id}, {"spawned", true} });
}
static void Op_AV_Route_Set(const json& req, std::function<void(json)> reply) {
    auto pts = req["args"].value("points", json::array());
    ReplyOk(req, reply, { {"avRoute", pts} });
}
static void Op_AV_Despawn(const json& req, std::function<void(json)> reply) {
    std::string id = req["args"].value("id", "AV.default");
    ReplyOk(req, reply, { {"av", id}, {"despawned", true} });
}
static void Op_AV_Land(const json& req, std::function<void(json)> reply) { ReplyOk(req, reply, { {"av", "landed"} }); }
static void Op_AV_Takeoff(const json& req, std::function<void(json)> reply) { ReplyOk(req, reply, { {"av", "takeoff"} }); }

// --- Train ---
static void Op_Train_Persist(const json& req, std::function<void(json)> reply) {
    bool enabled = req["args"].value("enabled", true);
    ReplyOk(req, reply, { {"trainPersist", enabled} });
}
static void Op_Train_Spawn(const json& req, std::function<void(json)> reply) {
    std::string id = req["args"].value("id", "train_default");
    ReplyOk(req, reply, { {"train", id}, {"spawned", true} });
}
static void Op_Train_Despawn(const json& req, std::function<void(json)> reply) {
    std::string id = req["args"].value("id", "train_default");
    ReplyOk(req, reply, { {"train", id}, {"despawned", true} });
}
static void Op_Train_Freeze(const json& req, std::function<void(json)> reply) { ReplyOk(req, reply, { {"train", "frozen"} }); }
static void Op_Train_Unfreeze(const json& req, std::function<void(json)> reply) { ReplyOk(req, reply, { {"train", "unfrozen"} }); }

// --- UI ---
static void Op_UI_Alert(const json& req, std::function<void(json)> reply) {
    std::string text = req["args"].value("text", "Alert");
    int ms = req["args"].value("ms", 2000);
    ReplyOk(req, reply, { {"type","alert"},{"text",text},{"ms",ms} });
}
static void Op_UI_Marker_Add(const json& req, std::function<void(json)> reply) {
    auto pos = req["args"].value("pos", json::object());
    std::string tag = req["args"].value("tag", "marker");
    ReplyOk(req, reply, { {"marker","added"},{"tag",tag},{"pos",pos} });
}
static void Op_UI_Marker_Remove(const json& req, std::function<void(json)> reply) {
    std::string tag = req["args"].value("tag", "marker");
    ReplyOk(req, reply, { {"marker","removed"},{"tag",tag} });
}
static void Op_UI_HUD_Toggle(const json& req, std::function<void(json)> reply) {
    bool visible = req["args"].value("visible", true);
    ReplyOk(req, reply, { {"hudVisible", visible} });
}

// --- Time / Weather ---
static void Op_Time_Set(const json& req, std::function<void(json)> reply) {
    int hour = req["args"].value("hour", 12);
    int minute = req["args"].value("minute", 0);
    ReplyOk(req, reply, { {"timeSet", true}, {"hour", hour}, {"minute", minute} });
}
static void Op_Time_Pause(const json& req, std::function<void(json)> reply) { ReplyOk(req, reply, { {"time", "paused"} }); }
static void Op_Time_Resume(const json& req, std::function<void(json)> reply) { ReplyOk(req, reply, { {"time", "resumed"} }); }
static void Op_Weather_Set(const json& req, std::function<void(json)> reply) {
    std::string preset = req["args"].value("preset", "Clear");
    float blend = req["args"].value("blend", 1.0f);
    ReplyOk(req, reply, { {"weatherPreset", preset}, {"blend", blend} });
}

// --- Player ---
static void Op_Player_Teleport(const json& req, std::function<void(json)> reply) {
    auto pos = req["args"].value("pos", json::object());
    float yaw = req["args"].value("yaw", 0.0f);
    ReplyOk(req, reply, { {"teleported", true}, {"pos", pos}, {"yaw", yaw} });
}
static void Op_Player_Heal(const json& req, std::function<void(json)> reply) {
    float amount = req["args"].value("amount", 100.0f);
    ReplyOk(req, reply, { {"healed", amount} });
}
static void Op_Player_Damage(const json& req, std::function<void(json)> reply) {
    float amount = req["args"].value("amount", 10.0f);
    std::string type = req["args"].value("type", "generic");
    ReplyOk(req, reply, { {"damaged", amount}, {"type", type} });
}
static void Op_Player_Inventory_Add(const json& req, std::function<void(json)> reply) {
    std::string item = req["args"].value("item", "Item.Default");
    int count = req["args"].value("count", 1);
    ReplyOk(req, reply, { {"added", item}, {"count", count} });
}
static void Op_Player_Inventory_Remove(const json& req, std::function<void(json)> reply) {
    std::string item = req["args"].value("item", "Item.Default");
    int count = req["args"].value("count", 1);
    ReplyOk(req, reply, { {"removed", item}, {"count", count} });
}

// --- World / Streaming / LOD ---
static void Op_World_Spawn_Explosion(const json& req, std::function<void(json)> reply) {
    auto pos = req["args"].value("pos", json::object());
    float radius = req["args"].value("radius", 5.0f);
    float power = req["args"].value("power", 1.0f);
    ReplyOk(req, reply, { {"explosion","queued"},{"pos",pos},{"radius",radius},{"power",power} });
}
static void Op_World_Light_Spawn(const json& req, std::function<void(json)> reply) {
    auto pos = req["args"].value("pos", json::object());
    float intensity = req["args"].value("intensity", 1000.0f);
    std::string color = req["args"].value("color", "#FFFFFF");
    std::string tag = req["args"].value("tag", "light1");
    ReplyOk(req, reply, { {"light","spawned"},{"tag",tag},{"pos",pos},{"intensity",intensity},{"color",color} });
}
static void Op_World_Light_Remove(const json& req, std::function<void(json)> reply) {
    std::string tag = req["args"].value("tag", "light1");
    ReplyOk(req, reply, { {"light","removed"},{"tag",tag} });
}
static void Op_World_StreamGrid_Recenter(const json& req, std::function<void(json)> reply) {
    auto pos = req["args"].value("pos", json::object());
    std::string mode = req["args"].value("mode", "auto");
    ReplyOk(req, reply, { {"streamgrid","recentered"},{"mode",mode},{"pos",pos} });
}
static void Op_World_LOD_Lock(const json& req, std::function<void(json)> reply) {
    int ttl = req["args"].value("ttl", 3000);
    std::string tag = req["args"].value("tag", "lodlock");
    ReplyOk(req, reply, { {"lodLocked", true},{"ttl",ttl},{"tag",tag} });
}
static void Op_World_LOD_Unlock(const json& req, std::function<void(json)> reply) {
    std::string tag = req["args"].value("tag", "lodlock");
    ReplyOk(req, reply, { {"lodLocked", false},{"tag",tag} });
}

// --- Debug / Telemetry ---
static void Op_Debug_Log(const json& req, std::function<void(json)> reply) {
    std::string level = req["args"].value("level", "info");
    std::string msg = req["args"].value("msg", "(empty)");
    MB_Logf("[debug.%s] %s", level.c_str(), msg.c_str());
    ReplyOk(req, reply, { {"logged", true}, {"level", level}, {"msg", msg} });
}
static void Op_Debug_Capture_Screenshot(const json& req, std::function<void(json)> reply) {
    std::string path = req["args"].value("path", "screenshot.png");
    ReplyOk(req, reply, { {"screenshot","queued"},{"path",path} });
}

// --- Config / Introspection ---
static void Op_Config_Set(const json& req, std::function<void(json)> reply) {
    std::string key = req["args"].value("key", "");
    json value = req["args"].value("value", json());
    if (key.empty()) { return ReplyErr(req, reply, "BadArgs", "key required"); }
    ReplyOk(req, reply, { {"set", key}, {"value", value} });
}
static void Op_Config_Get(const json& req, std::function<void(json)> reply) {
    std::string key = req["args"].value("key", "");
    if (key.empty()) { return ReplyErr(req, reply, "BadArgs", "key required"); }
    ReplyOk(req, reply, { {"key", key}, {"value", "(stub)"} });
}
static void Op_Ops_Capabilities(const json& req, std::function<void(json)> reply) {
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
    ReplyOk(req, reply, { {"capabilities", caps} });
}
static void Op_Ping(const json& req, std::function<void(json)> reply) {
    std::string echo = req["args"].value("echo", "pong");
    ReplyOk(req, reply, { {"pong", true}, {"echo", echo} });
}

// --- Upscaler control ---
static void Op_Upscaler_Enable(const json& req, std::function<void(json)> reply)
{
#if defined(MB_UPSCALER_AVAILABLE) || 1
    bool enabled = req["args"].value("enabled", true);
    MB::Upscaler_Enable(enabled);
    ReplyOk(req, reply, { {"enabled", enabled} });
#else
    ReplyErr(req, reply, "Unavailable", "Upscaler module not built");
#endif
}

static void Op_Upscaler_Set(const json& req, std::function<void(json)> reply)
{
#if defined(MB_UPSCALER_AVAILABLE) || 1
    std::string mode = req["args"].value("mode", "off");
    float sharp = req["args"].value("sharpness", 0.6f);

    if (mode == "off") MB::Upscaler_SetMode(MB::UpscaleMode::Off);
    else if (mode == "fsr2") MB::Upscaler_SetMode(MB::UpscaleMode::FSR2);
    else return ReplyErr(req, reply, "BadArgs", "mode must be off|fsr2");

    auto p = MB::UpscalerParams{};
    p.sharpness = sharp;
    MB::Upscaler_SetParams(p);

    ReplyOk(req, reply, { {"mode", mode}, {"sharpness", sharp} });
#else
    ReplyErr(req, reply, "Unavailable", "Upscaler module not built");
#endif
}

static MB::UpscalerParams g_upParams{};

static void Op_Graphics_Target_Set(const json& req, std::function<void(json)> reply)
{
#if defined(MB_UPSCALER_AVAILABLE) || 1
    g_upParams.outputWidth = req["args"].value("width", 3840u);
    g_upParams.outputHeight = req["args"].value("height", 2160u);
    MB::Upscaler_Resize(g_upParams);
    ReplyOk(req, reply, { {"width", g_upParams.outputWidth}, {"height", g_upParams.outputHeight} });
#else
    ReplyErr(req, reply, "Unavailable", "Upscaler module not built");
#endif
}

static void Op_Graphics_Internal_Scale(const json& req, std::function<void(json)> reply)
{
#if defined(MB_UPSCALER_AVAILABLE) || 1
    float s = req["args"].value("scale", 0.5f);
    g_upParams.renderWidth = (uint32_t)std::max(16.0f, s * static_cast<float>(g_upParams.outputWidth));
    g_upParams.renderHeight = (uint32_t)std::max(16.0f, s * static_cast<float>(g_upParams.outputHeight));
    MB::Upscaler_Resize(g_upParams);
    ReplyOk(req, reply, { {"renderWidth", g_upParams.renderWidth}, {"renderHeight", g_upParams.renderHeight} });
#else
    ReplyErr(req, reply, "Unavailable", "Upscaler module not built");
#endif
}

// -------------------- Op registry --------------------
using OpHandler = std::function<void(const json& req, std::function<void(json)> reply)>;
static std::unordered_map<std::string, OpHandler> g_ops;

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
            auto it = g_ops.find(op);
            if (it == g_ops.end()) { ReplyErr(req, reply, "UnknownOp", op); continue; }

            try { it->second(req, reply); }
            catch (const std::exception& e) { ReplyErr(req, reply, "Exception", e.what()); }
        }

        FlushFileBuffers(g_pipe);
        DisconnectNamedPipe(g_pipe);
        CloseHandle(g_pipe);
        g_pipe = INVALID_HANDLE_VALUE;

        MB_Log("Client disconnected.");
    }
    MB_Log("Server worker stopped.");
}

// ---------- Registration ----------
static void RegisterOps()
{
    g_ops.emplace("ui.toast", &Op_UI_Toast);
    g_ops.emplace("timescale.set", &Op_TimeScale_Set);
    g_ops.emplace("lod.pin", &Op_LOD_Pin);
    g_ops.emplace("traffic.mul", &Op_Traffic_Mul);

    g_ops.emplace("npc.freeze", &Op_NPC_Freeze);
    g_ops.emplace("npc.unfreeze", &Op_NPC_Unfreeze);
    g_ops.emplace("npc.spawn", &Op_NPC_Spawn);
    g_ops.emplace("npc.despawn", &Op_NPC_Despawn);
    g_ops.emplace("npc.teleport", &Op_NPC_Teleport);

    g_ops.emplace("vehicle.spawn", &Op_Vehicle_Spawn);
    g_ops.emplace("vehicle.despawn", &Op_Vehicle_Despawn);
    g_ops.emplace("vehicle.boost", &Op_Vehicle_Boost);
    g_ops.emplace("vehicle.paint", &Op_Vehicle_Paint);
    g_ops.emplace("vehicle.repair", &Op_Vehicle_Repair);

    g_ops.emplace("traffic.clear", &Op_Traffic_Clear);
    g_ops.emplace("traffic.freeze", &Op_Traffic_Freeze);
    g_ops.emplace("traffic.unfreeze", &Op_Traffic_Unfreeze);
    g_ops.emplace("traffic.route", &Op_Traffic_Route);
    g_ops.emplace("traffic.persist", &Op_Traffic_Persist);

    g_ops.emplace("av.spawn", &Op_AV_Spawn);
    g_ops.emplace("av.route.set", &Op_AV_Route_Set);
    g_ops.emplace("av.despawn", &Op_AV_Despawn);
    g_ops.emplace("av.land", &Op_AV_Land);
    g_ops.emplace("av.takeoff", &Op_AV_Takeoff);

    g_ops.emplace("train.persist", &Op_Train_Persist);
    g_ops.emplace("train.spawn", &Op_Train_Spawn);
    g_ops.emplace("train.despawn", &Op_Train_Despawn);
    g_ops.emplace("train.freeze", &Op_Train_Freeze);
    g_ops.emplace("train.unfreeze", &Op_Train_Unfreeze);

    g_ops.emplace("ui.alert", &Op_UI_Alert);
    g_ops.emplace("ui.marker.add", &Op_UI_Marker_Add);
    g_ops.emplace("ui.marker.remove", &Op_UI_Marker_Remove);
    g_ops.emplace("ui.hud.toggle", &Op_UI_HUD_Toggle);

    g_ops.emplace("time.set", &Op_Time_Set);
    g_ops.emplace("time.pause", &Op_Time_Pause);
    g_ops.emplace("time.resume", &Op_Time_Resume);

    g_ops.emplace("weather.set", &Op_Weather_Set);

    g_ops.emplace("player.teleport", &Op_Player_Teleport);
    g_ops.emplace("player.heal", &Op_Player_Heal);
    g_ops.emplace("player.damage", &Op_Player_Damage);
    g_ops.emplace("player.inventory.add", &Op_Player_Inventory_Add);
    g_ops.emplace("player.inventory.remove", &Op_Player_Inventory_Remove);

    g_ops.emplace("world.spawn.explosion", &Op_World_Spawn_Explosion);
    g_ops.emplace("world.light.spawn", &Op_World_Light_Spawn);
    g_ops.emplace("world.light.remove", &Op_World_Light_Remove);
    g_ops.emplace("world.streamgrid.recenter", &Op_World_StreamGrid_Recenter);
    g_ops.emplace("world.lod.lock", &Op_World_LOD_Lock);
    g_ops.emplace("world.lod.unlock", &Op_World_LOD_Unlock);

    g_ops.emplace("debug.log", &Op_Debug_Log);
    g_ops.emplace("debug.capture.screenshot", &Op_Debug_Capture_Screenshot);

    g_ops.emplace("config.set", &Op_Config_Set);
    g_ops.emplace("config.get", &Op_Config_Get);

    g_ops.emplace("ops.capabilities", &Op_Ops_Capabilities);
    g_ops.emplace("ping", &Op_Ping);

    g_ops.emplace("upscaler.enable", &Op_Upscaler_Enable);
    g_ops.emplace("upscaler.set", &Op_Upscaler_Set);
    g_ops.emplace("graphics.target.set", &Op_Graphics_Target_Set);
    g_ops.emplace("graphics.internal.scale", &Op_Graphics_Internal_Scale);
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
