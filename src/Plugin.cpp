#include <RED4ext/RED4ext.hpp>
#include <RED4ext/Api/EMainReason.hpp>
#include "MirrorBladeOps.hpp"
#include "MBConfig.hpp"
#include "MBLog.hpp"
#include "MBIPC.hpp"
#include "MBTypeReg.cpp"
#include "MBConfig.hpp"
#include "MBLog.hpp"

using namespace RED4ext;

RED4EXT_C_EXPORT void RED4EXT_CALL OnRegisterTypes(CRTTISystem* rtti, CGameEngine* eng)
{
    MB::RegisterTypes(rtti);          // only type/func registration
}

RED4EXT_C_EXPORT void RED4EXT_CALL OnPostRegisterTypes(CRTTISystem* rtti, CGameEngine* eng)
{
    MB::PostRegisterTypes(rtti);      // often empty; ok to leave as no-op
}

// New-style single entrypoint
RED4EXT_C_EXPORT bool RED4EXT_CALL PluginMain(PluginHandle, EMainReason reason, const Sdk* /*sdk*/)
{
    if (reason == EMainReason::Load)
    {
        MB::InitLogs();
        MB::Log().Log(MB::LogLevel::Info, "MirrorBladeBridge: Load");

        MB::InitConfig();             // initial load + start watcher
        MB::GetIPC().Start();         // fixed well-known pipe name inside Start()
        return true;
    }
    else if (reason == EMainReason::Unload)
    {
        MB::Log().Log(MB::LogLevel::Info, "MirrorBladeBridge: Unload");
        MB::GetIPC().Stop();
        MB::ShutdownConfig();
        MB::ShutdownLogs();
        return true;
    }
    return false;
}
