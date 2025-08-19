// pch.hpp
// --- harden against macro pollution -----------------------------------------
#ifdef string
#  undef string
#endif
#ifdef format
#  undef format
#endif
#ifdef min
#  undef min
#endif
#ifdef max
#  undef max
#endif

#ifndef NOMINMAX
#  define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
// -----------------------------------------------------------------------------

#pragma once

// =====================
// Windows lean includes
// =====================
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#ifdef _WIN32
#include <windows.h>
#include <combaseapi.h>
#include <synchapi.h>
// D3D12 (used by upscaler backend)
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#endif

// =====================
// Standard C++ headers
// =====================
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>

#include <atomic>
#include <mutex>
#include <thread>
#include <chrono>

#include <string>
#include <string_view>
#include <sstream>
#include <iomanip>

#include <vector>
#include <array>
#include <optional>
#include <memory>
#include <functional>
#include <algorithm>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>

// =====================
// Third-party headers
// =====================
#include <nlohmann/json.hpp>

// =====================
// RED4ext (order matters)
// Make sure RTTISystem is visible BEFORE any inlines that reference it.
// =====================
#include "RED4ext/RTTISystem.hpp"       // defines RED4ext::CRTTISystem and ::Get()
#include "RED4ext/NativeTypes.hpp"      // CString, Variant, etc.
#include "RED4ext/CName.hpp"            // CName

// Some projects (and RED4ext helpers) pull these inlines directly;
// include them AFTER RTTISystem so their references compile cleanly.
#include "RED4ext/Scripting/Stack-inl.hpp"
#include "RED4ext/Scripting/Functions-inl.hpp"
