#include "OpsHelpers.hpp"
#include "MBState.hpp""
#include <RED4ext/GameEngine.hpp>

namespace {
    struct MBGlobalState {
        RED4ext::IScriptable* ops = nullptr;
    };
    MBGlobalState g_state; // define the global
} // anonymous namespace

namespace MB {

    void Init(RED4ext::CGameEngine* eng)
    {
        g_state.ops = MB::GetOps(eng);
    }

    RED4ext::IScriptable* GetOpsInstance()
    {
        return g_state.ops;
    }

    bool CallOp(const char* name)
    {
        if (!g_state.ops) return false;
        return MB::CallBool(g_state.ops, name);
    }

    int32_t GetOpInt(const char* name)
    {
        if (!g_state.ops) return 0;
        return MB::CallInt(g_state.ops, name);
    }

    float GetOpFloat(const char* name)
    {
        if (!g_state.ops) return 0.0f;
        return MB::CallFloat(g_state.ops, name);
    }

} // namespace MB
