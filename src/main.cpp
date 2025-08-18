#include <RED4ext/RED4ext.hpp>
#include "MirrorBladeBridge.hpp"

RED4EXT_C_EXPORT uint32_t RED4EXT_CALL Supports()
{
    return RED4EXT_API_VERSION_LATEST;
}

RED4EXT_C_EXPORT void RED4EXT_CALL Query(RED4ext::PluginInfo* aInfo)
{
    aInfo->name = L"MirrorBladeBridge";
    aInfo->author = L"OliviaAI / TGDK";
    aInfo->version = RED4EXT_SEMVER(0, 1, 0);
    aInfo->runtime = RED4EXT_RUNTIME_LATEST; // target latest game version
    aInfo->sdk = RED4EXT_SDK_LATEST;
}

RED4EXT_C_EXPORT bool RED4EXT_CALL Main(RED4ext::PluginHandle aHandle, RED4ext::EMainReason aReason, const RED4ext::Sdk* aSdk)
{
    using RED4ext::EMainReason;

    switch (aReason)
    {
    case EMainReason::Load:
        MB::InitBridge();
        break;
    case EMainReason::Unload:
        MB::ShutdownBridge();
        break;
    default:
        break;
    }

    return true;
}