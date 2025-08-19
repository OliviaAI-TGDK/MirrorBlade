// include/LogisticalValveExports.hpp
#pragma once
//
// LogisticalValveExports.hpp
// Minimal C exports for external callers (tools/tests) to interact with the plugin.
//
// Ownership:
//   - Any non-null char* returned by these APIs must be freed with LV_FreeString().
//   - All functions are thread-safe under typical usage; the dispatcher guards
//     exceptions and returns a JSON error payload on failure.
//

#ifdef _WIN32
#define LV_API extern "C" __declspec(dllexport)
#else
#define LV_API extern "C"
#endif

// Returns a newly allocated C string describing the export surface/version.
// Call LV_FreeString() to free the returned pointer.
LV_API const char* LV_Version();

// Lightweight liveness check. Returns 1 on success.
LV_API int LV_Ping();

// Dispatch an op with a JSON argument object.
//
// Parameters:
//   op       : operation name, e.g. "traffic.mul"
//   argsJson : JSON object as UTF-8 (e.g. {"mult":2.0}); may be nullptr/empty for {}
//
// Returns:
//   Newly-allocated UTF-8 JSON string with the result payload,
//   e.g. {"ok":true,"result":...} or {"ok":false,"error":"..."}.
//   Caller must free via LV_FreeString().
//
// Notes:
//   - Never throws. On parse errors or internal exceptions, an {"ok":false,...} is returned.
//
LV_API const char* LV_DispatchJSON(const char* op, const char* argsJson);

// Frees any string returned by LV_Version() or LV_DispatchJSON().
LV_API void LV_FreeString(const char* s);

