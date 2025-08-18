// src/MirrorBladeOps.cpp

#include "MirrorBladeOps.hpp"
// #include "MBConfig.hpp"   // not needed here
#include "MBOps.hpp"         // for MB::Ops::I().Dispatch(op, args)

#include <algorithm>
#include <string>
#include <stdexcept>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

// Minimal RED4ext surface: forward-declare engine, include CString type
namespace RED4ext { struct CGameEngine; }   // forward decl only
#include "RED4ext/NativeTypes.hpp"          // RED4ext::CString

// -------------------------------------------------------------------------------------------------
// This file intentionally DOES NOT define MB::RegisterTypes / MB::PostRegisterTypes.
// Those are defined exactly once in src/MBTypeReg.cpp to avoid duplicate definitions.
// -------------------------------------------------------------------------------------------------

namespace
{
    // Optional native entrypoint you may wire later via RTTI:
    // Script: ScriptGameInstance.MirrorBlade_Op(op: String, argsJson: String) -> String
    static RED4ext::CString MB_Op(RED4ext::CGameEngine*, RED4ext::CString op, RED4ext::CString argsJson)
    {
        json args = json::object();
        try {
            const char* c = argsJson.c_str();
            if (c && *c) args = json::parse(c);
        }
        catch (...) {
            // keep args empty on parse error
        }

        // Dispatch to your ops registry
        auto reply = MB::Ops::I().Dispatch(op.c_str(), args);

        // Return JSON as CString
        const std::string s = reply.dump();
        return RED4ext::CString(s.c_str());
    }

    // If you later wire RTTI in MBTypeReg.cpp, you can reference &MB_Op from there.
} // anonymous namespace
