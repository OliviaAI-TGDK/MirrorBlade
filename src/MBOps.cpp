#include "MBOps.hpp"
#include "MBLog.hpp"
#include "MBConfig.hpp"
#include "MBFeatures.hpp"
#include "MBState.hpp"          // your live state (upscaler/traffic/etc)

namespace MB
{
    Ops& Ops::I() { static Ops g; return g; }

    void Ops::Register(const std::string& name, OpHandler fn)
    {
        std::scoped_lock lk{ _mtx };
        _map[name] = std::move(fn);
        MB::Log().Log(MB::LogLevel::Debug, "Op registered: %s", name.c_str());
    }

    bool Ops::Exists(const std::string& name) const
    {
        std::scoped_lock lk{ _mtx };
        return _map.find(name) != _map.end();
    }

    json Ops::Dispatch(const std::string& name, const json& args)
    {
        OpHandler fn;
        {
            std::scoped_lock lk{ _mtx };
            auto it = _map.find(name);
            if (it == _map.end())
                return json{ {"ok", false}, {"error", std::string("Unknown op: ") + name} };
            fn = it->second;
        }

        try {
            // You can also wrap per-feature guards here: MB_GUARDED(name, {...})
            json result = fn(args);
            if (!result.contains("ok")) result["ok"] = true;
            return result;
        }
        catch (const std::exception& e) {
            MB::Log().Log(MB::LogLevel::Warn, "Op '%s' threw: %s", name.c_str(), e.what());
            return json{ {"ok", false}, {"error", e.what()} };
        }
        catch (...) {
            MB::Log().Log(MB::LogLevel::Warn, "Op '%s' threw unknown", name.c_str());
            return json{ {"ok", false}, {"error", "unknown error"} };
        }
    }

    // ---------------------------
    // Handlers (sketch/stubs)
    // Fill these in with your real logic; keep signatures json->json.
    // ---------------------------
    static json Op_Ping(const json&) { return { {"ok",true},{"result","Pong"} }; }

    static json Op_Upscaler_Enable(const json& a) {
        bool en = a.value("enabled", false);
        MB::State::I().upscaler.store(en);
        return { {"result", en} };
    }

    static json Op_Graphics_Internal_Scale(const json& a) {
        // example: { "scale": 0.77 }
        float s = a.value("scale", 1.0f);
        // TODO: apply scale into your native path
        return { {"result", s} };
    }

    static json Op_Traffic_Mul(const json& a) {
        float f = a.value("factor", 1.0f);
        if (f < 0.1f) f = 0.1f; if (f > 50.f) f = 50.f;
        MB::State::I().traffic.store(f);
        return { {"result", f} };
    }

    // Placeholder stubs below; return something useful so IPC/REDscript callers get feedback.
    static json Op_UI_Toast(const json& a) { return { {"note","toast queued"}, {"args",a} }; }
    static json Op_TimeScale_Set(const json& a) { return { {"note","timescale set"}, {"args",a} }; }
    static json Op_LOD_Pin(const json& a) { return { {"note","lod pinned"}, {"args",a} }; }
    static json Op_NPC_Freeze(const json& a) { return { {"note","npcs frozen"}, {"args",a} }; }
    static json Op_NPC_Unfreeze(const json& a) { return { {"note","npcs unfrozen"}, {"args",a} }; }
    static json Op_NPC_Spawn(const json& a) { return { {"note","npc spawn requested"}, {"args",a} }; }
    static json Op_NPC_Despawn(const json& a) { return { {"note","npc despawn requested"}, {"args",a} }; }
    static json Op_NPC_Teleport(const json& a) { return { {"note","npc teleport requested"}, {"args",a} }; }

    static json Op_Vehicle_Spawn(const json& a) { return { {"note","vehicle spawn"}, {"args",a} }; }
    static json Op_Vehicle_Despawn(const json& a) { return { {"note","vehicle despawn"}, {"args",a} }; }
    static json Op_Vehicle_Boost(const json& a) { return { {"note","vehicle boost"}, {"args",a} }; }
    static json Op_Vehicle_Paint(const json& a) { return { {"note","vehicle repaint"}, {"args",a} }; }
    static json Op_Vehicle_Repair(const json& a) { return { {"note","vehicle repair"}, {"args",a} }; }

    static json Op_Traffic_Clear(const json& a) { return { {"note","traffic clear"}, {"args",a} }; }
    static json Op_Traffic_Freeze(const json& a) { return { {"note","traffic freeze"}, {"args",a} }; }
    static json Op_Traffic_Unfreeze(const json& a) { return { {"note","traffic unfreeze"}, {"args",a} }; }
    static json Op_Traffic_Route(const json& a) { return { {"note","traffic route set"}, {"args",a} }; }
    static json Op_Traffic_Persist(const json& a) { return { {"note","traffic persist set"}, {"args",a} }; }

    static json Op_AV_Spawn(const json& a) { return { {"note","av spawn"}, {"args",a} }; }
    static json Op_AV_Route_Set(const json& a) { return { {"note","av route set"}, {"args",a} }; }
    static json Op_AV_Despawn(const json& a) { return { {"note","av despawn"}, {"args",a} }; }
    static json Op_AV_Land(const json& a) { return { {"note","av land"}, {"args",a} }; }
    static json Op_AV_Takeoff(const json& a) { return { {"note","av takeoff"}, {"args",a} }; }

    static json Op_Train_Persist(const json& a) { return { {"note","train persist"}, {"args",a} }; }
    static json Op_Train_Spawn(const json& a) { return { {"note","train spawn"}, {"args",a} }; }
    static json Op_Train_Despawn(const json& a) { return { {"note","train despawn"}, {"args",a} }; }
    static json Op_Train_Freeze(const json& a) { return { {"note","train freeze"}, {"args",a} }; }
    static json Op_Train_Unfreeze(const json& a) { return { {"note","train unfreeze"}, {"args",a} }; }

    static json Op_UI_Alert(const json& a) { return { {"note","ui alert"}, {"args",a} }; }
    static json Op_UI_Marker_Add(const json& a) { return { {"note","marker add"}, {"args",a} }; }
    static json Op_UI_Marker_Remove(const json& a) { return { {"note","marker remove"}, {"args",a} }; }
    static json Op_UI_HUD_Toggle(const json& a) { return { {"note","hud toggle"}, {"args",a} }; }

    static json Op_Time_Set(const json& a) { return { {"note","time set"}, {"args",a} }; }
    static json Op_Time_Pause(const json& a) { return { {"note","time pause"}, {"args",a} }; }
    static json Op_Time_Resume(const json& a) { return { {"note","time resume"}, {"args",a} }; }

    static json Op_Weather_Set(const json& a) { return { {"note","weather set"}, {"args",a} }; }

    static json Op_Player_Teleport(const json& a) { return { {"note","player tp"}, {"args",a} }; }
    static json Op_Player_Heal(const json& a) { return { {"note","player heal"}, {"args",a} }; }
    static json Op_Player_Damage(const json& a) { return { {"note","player dmg"}, {"args",a} }; }
    static json Op_Player_Inventory_Add(const json& a) { return { {"note","inv add"}, {"args",a} }; }
    static json Op_Player_Inventory_Remove(const json& a) { return { {"note","inv remove"}, {"args",a} }; }

    static json Op_World_Spawn_Explosion(const json& a) { return { {"note","world explosion"}, {"args",a} }; }
    static json Op_World_Light_Spawn(const json& a) { return { {"note","light spawn"}, {"args",a} }; }
    static json Op_World_Light_Remove(const json& a) { return { {"note","light remove"}, {"args",a} }; }
    static json Op_World_StreamGrid_Recenter(const json& a) { return { {"note","streamgrid recenter"}, {"args",a} }; }
    static json Op_World_LOD_Lock(const json& a) { return { {"note","lod lock"}, {"args",a} }; }
    static json Op_World_LOD_Unlock(const json& a) { return { {"note","lod unlock"}, {"args",a} }; }

    static json Op_Debug_Log(const json& a) { return { {"note","debug log"}, {"args",a} }; }
    static json Op_Debug_Capture_Screenshot(const json& a) { return { {"note","screenshot"}, {"args",a} }; }

    static json Op_Config_Set(const json& a) {
        // Example: { "path":"trafficBoost", "value":5.0 }
        const auto path = a.value("path", std::string{});
        const auto& val = a.contains("value") ? a.at("value") : json();
        // TODO: mutate MB::Config accordingly + SaveConfig()
        return { {"note","config set"}, {"path",path}, {"value",val} };
    }
    static json Op_Config_Get(const json& a) {
        const auto path = a.value("path", std::string{});
        // TODO: read MB::Config and return
        return { {"note","config get"}, {"path",path} };
    }

    static json Op_Ops_Capabilities(const json&) {
        // Return the op names we have
        json list = json::array();
        {
            // snapshot names
            // (rely on Dispatch for thread-safety elsewhere)
        }
        return { {"ops", list} };
    }

    static json Op_Upscaler_Set(const json& a) { return { {"note","upscaler set"}, {"args",a} }; }
    static json Op_Graphics_Target_Set(const json& a) { return { {"note","graphics target"}, {"args",a} }; }

    void Ops::RegisterAll()
    {
        Register("ui.toast", &Op_UI_Toast);
        Register("timescale.set", &Op_TimeScale_Set);
        Register("lod.pin", &Op_LOD_Pin);
        Register("traffic.mul", &Op_Traffic_Mul);

        Register("npc.freeze", &Op_NPC_Freeze);
        Register("npc.unfreeze", &Op_NPC_Unfreeze);
        Register("npc.spawn", &Op_NPC_Spawn);
        Register("npc.despawn", &Op_NPC_Despawn);
        Register("npc.teleport", &Op_NPC_Teleport);

        Register("vehicle.spawn", &Op_Vehicle_Spawn);
        Register("vehicle.despawn", &Op_Vehicle_Despawn);
        Register("vehicle.boost", &Op_Vehicle_Boost);
        Register("vehicle.paint", &Op_Vehicle_Paint);
        Register("vehicle.repair", &Op_Vehicle_Repair);

        Register("traffic.clear", &Op_Traffic_Clear);
        Register("traffic.freeze", &Op_Traffic_Freeze);
        Register("traffic.unfreeze", &Op_Traffic_Unfreeze);
        Register("traffic.route", &Op_Traffic_Route);
        Register("traffic.persist", &Op_Traffic_Persist);

        Register("av.spawn", &Op_AV_Spawn);
        Register("av.route.set", &Op_AV_Route_Set);
        Register("av.despawn", &Op_AV_Despawn);
        Register("av.land", &Op_AV_Land);
        Register("av.takeoff", &Op_AV_Takeoff);

        Register("train.persist", &Op_Train_Persist);
        Register("train.spawn", &Op_Train_Spawn);
        Register("train.despawn", &Op_Train_Despawn);
        Register("train.freeze", &Op_Train_Freeze);
        Register("train.unfreeze", &Op_Train_Unfreeze);

        Register("ui.alert", &Op_UI_Alert);
        Register("ui.marker.add", &Op_UI_Marker_Add);
        Register("ui.marker.remove", &Op_UI_Marker_Remove);
        Register("ui.hud.toggle", &Op_UI_HUD_Toggle);

        Register("time.set", &Op_Time_Set);
        Register("time.pause", &Op_Time_Pause);
        Register("time.resume", &Op_Time_Resume);

        Register("weather.set", &Op_Weather_Set);

        Register("player.teleport", &Op_Player_Teleport);
        Register("player.heal", &Op_Player_Heal);
        Register("player.damage", &Op_Player_Damage);
        Register("player.inventory.add", &Op_Player_Inventory_Add);
        Register("player.inventory.remove", &Op_Player_Inventory_Remove);

        Register("world.spawn.explosion", &Op_World_Spawn_Explosion);
        Register("world.light.spawn", &Op_World_Light_Spawn);
        Register("world.light.remove", &Op_World_Light_Remove);
        Register("world.streamgrid.recenter", &Op_World_StreamGrid_Recenter);
        Register("world.lod.lock", &Op_World_LOD_Lock);
        Register("world.lod.unlock", &Op_World_LOD_Unlock);

        Register("debug.log", &Op_Debug_Log);
        Register("debug.capture.screenshot", &Op_Debug_Capture_Screenshot);

        Register("config.set", &Op_Config_Set);
        Register("config.get", &Op_Config_Get);

        Register("ops.capabilities", &Op_Ops_Capabilities);
        Register("ping", &Op_Ping);

        Register("upscaler.enable", &Op_Upscaler_Enable);
        Register("upscaler.set", &Op_Upscaler_Set);
        Register("graphics.target.set", &Op_Graphics_Target_Set);
        Register("graphics.internal.scale", &Op_Graphics_Internal_Scale);
    }
}
