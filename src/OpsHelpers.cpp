#include "OpsHelpers.hpp"

namespace MB {

    static inline RED4ext::CRTTISystem* RTTI()
    {
        return RED4ext::CRTTISystem::Get();
    }

    RED4ext::IScriptable* GetOps(RED4ext::CGameEngine* eng)
    {
        if (!eng || !eng->framework)
            return nullptr;

        auto* gi = eng->framework->gameInstance;
        auto* rtti = RTTI();
        if (!gi || !rtti)
            return nullptr;

        auto* cls = rtti->GetClass("MirrorBlade.MirrorBladeOps");
        if (!cls)
            return nullptr;

        auto* fn = cls->GetFunction(RED4ext::CName("Get"));
        if (!fn)
            return nullptr;

        RED4ext::IScriptable* resultObj = nullptr;
        RED4ext::CStackType result{ cls, &resultObj };

        RED4ext::CStackType arg;
        arg.type = rtti->GetType("gameInstance");
        arg.value = &gi;

        RED4ext::CStack stack(nullptr, &arg, 1, &result);
        if (!fn->Execute(&stack))
            return nullptr;

        return resultObj;
    }

    template <typename T>
    static bool CallNoArgReturning(
        RED4ext::IScriptable* obj,
        const char* funcName,
        const char* rttiType,
        T& out)
    {
        if (!obj)
            return false;

        auto* cls = obj->GetType();
        auto* rtti = RTTI();
        if (!cls || !rtti)
            return false;

        auto* fn = cls->GetFunction(RED4ext::CName(funcName));
        if (!fn)
            return false;

        auto* type = rtti->GetType(rttiType);
        if (!type)
            return false;

        RED4ext::CStackType result{ type, &out };
        RED4ext::CStack stack(obj, nullptr, 0, &result);
        return fn->Execute(&stack);
    }

    bool CallBool(RED4ext::IScriptable* obj, const char* funcName)
    {
        bool v = false;
        if (!CallNoArgReturning<bool>(obj, funcName, "Bool", v))
            return false;
        return v;
    }

    int32_t CallInt(RED4ext::IScriptable* obj, const char* funcName)
    {
        int32_t v = 0;
        if (!CallNoArgReturning<int32_t>(obj, funcName, "Int32", v))
            return 0;
        return v;
    }

    float CallFloat(RED4ext::IScriptable* obj, const char* funcName)
    {
        float v = 0.0f;
        if (!CallNoArgReturning<float>(obj, funcName, "Float", v))
            return 0.0f;
        return v;
    }

    bool HasFunction(RED4ext::IScriptable* obj, const char* funcName)
    {
        if (!obj)
            return false;
        auto* cls = obj->GetType();
        if (!cls)
            return false;
        return cls->GetFunction(RED4ext::CName(funcName)) != nullptr;
    }

} // namespace MB
