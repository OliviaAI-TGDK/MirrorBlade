#pragma once

#include <cstdint>
#include <RED4ext/RED4ext.hpp>
#include <RED4ext/CName.hpp>
#include <RED4ext/RTTISystem.hpp>
#include <RED4ext/RTTISystem-inl.hpp>
#include <RED4ext/Scripting/Stack.hpp>
#include <RED4ext/GameEngine.hpp>

namespace MB {

	// Fetch singleton MirrorBlade.MirrorBladeOps (or nullptr if not found).
	RED4ext::IScriptable* GetOps(RED4ext::CGameEngine* eng);

	// No-arg calls, safe stubs if RTTI pieces are missing.
	bool    CallBool(RED4ext::IScriptable* obj, const char* funcName);
	int32_t CallInt(RED4ext::IScriptable* obj, const char* funcName);
	float   CallFloat(RED4ext::IScriptable* obj, const char* funcName);

	bool HasFunction(RED4ext::IScriptable* obj, const char* funcName);

} // namespace MB
