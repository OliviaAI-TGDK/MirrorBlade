// src/Upscaler_FSR2.cpp
#include "Upscaler.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdio>
#include <cstdarg>
#include <mutex>
#include <atomic>
#include <stdexcept>

#include "MBFeatures.hpp"
#include "MBLog.hpp"
#include "MirrorBladeOps.hpp"

// -----------------------------------------------------------------------------
// Example per-frame hook (or call from your event)
// -----------------------------------------------------------------------------
void MirrorBlade_Tick()
{
    // Upscaler adjustment feature
    MB_GUARDED("upscaler", {
        auto* ops = MB::MirrorBladeOps::Instance();
        if (!ops) throw std::runtime_error("MirrorBladeOps::Instance() returned null");
        if (ops->IsUpscalerEnabled()) {
            // ... do native upscaler work; may throw on unexpected state
        }
        });

    // Traffic feature
    MB_GUARDED("traffic_boost", {
        auto* ops = MB::MirrorBladeOps::Instance();
        const float factor = ops ? ops->GetTrafficBoost() : 1.0f;
        // ... apply factor to your hooked system
        (void)factor;
        });
}

using namespace MB;

// -----------------------------------------------------------------------------
// Local logging helpers
// -----------------------------------------------------------------------------
void MB::Upscaler_Log(const char* msg) {
    OutputDebugStringA("[MB::Upscaler] ");
    OutputDebugStringA(msg);
    OutputDebugStringA("\n");
}
void MB::Upscaler_Logf(const char* fmt, ...) {
    char buf[1024];
    va_list ap; va_start(ap, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    Upscaler_Log(buf);
}

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------
static std::mutex                 g_mx;
static ID3D12Device* g_device = nullptr;
static ID3D12CommandQueue* g_queue = nullptr;
static std::atomic<bool>         g_enabled{ false };
static std::atomic<UpscaleMode>  g_mode{ UpscaleMode::Off };
static UpscalerParams            g_params;
static UpscalerResourcesD3D12    g_res;

// -----------------------------------------------------------------------------
// FSR2 integration (optional; compile with -DWITH_FSR2 and link FidelityFX)
// -----------------------------------------------------------------------------
#ifdef WITH_FSR2
#include <ffx_fsr2.h>
#include <ffx_fsr2_interface.h>
#include <ffx_fsr2_dx12.h>

static FfxFsr2Context g_fsr2Ctx{};
static bool           g_fsr2Created = false;

static FfxFsr2ContextDescription MakeFsr2Desc() {
    FfxFsr2ContextDescription desc{};
    desc.flags = 0;
    // e.g., if HDR: desc.flags |= FFX_FSR2_ENABLE_HIGH_DYNAMIC_RANGE;

    desc.maxRenderSize.width = g_params.renderWidth;
    desc.maxRenderSize.height = g_params.renderHeight;
    desc.displaySize.width = g_params.outputWidth;
    desc.displaySize.height = g_params.outputHeight;

    FfxInterface iface{};
    ffxFsr2GetInterfaceDX12(&iface, g_device, g_queue);
    desc.device = iface;

    return desc;
}

static void CreateFsr2IfNeeded() {
    if (g_fsr2Created) return;
    const auto desc = MakeFsr2Desc();
    const auto rc = ffxFsr2ContextCreate(&g_fsr2Ctx, &desc);
    if (rc != FFX_OK) {
        Upscaler_Logf("FFX: ffxFsr2ContextCreate failed rc=%d", rc);
        return;
    }
    g_fsr2Created = true;
    Upscaler_Log("FFX: FSR2 context created");
}

static void DestroyFsr2() {
    if (!g_fsr2Created) return;
    ffxFsr2ContextDestroy(&g_fsr2Ctx);
    g_fsr2Created = false;
    Upscaler_Log("FFX: FSR2 context destroyed");
}

static void RecreateFsr2() {
    DestroyFsr2();
    CreateFsr2IfNeeded();
}
#else
static void RecreateFsr2() {}
static void DestroyFsr2() {}
static void CreateFsr2IfNeeded() {}
#endif // WITH_FSR2

// -----------------------------------------------------------------------------
// Public API (as declared in Upscaler.hpp)
// -----------------------------------------------------------------------------
bool MB::Upscaler_Init_D3D12(ID3D12Device* device, ID3D12CommandQueue* queue) {
    std::lock_guard<std::mutex> lk(g_mx);
    g_device = device;
    g_queue = queue;

#ifdef WITH_FSR2
    CreateFsr2IfNeeded();
#else
    Upscaler_Log("WITH_FSR2 not defined — building stubs (no upscale yet).");
#endif
    return true;
}

void MB::Upscaler_Shutdown() {
    std::lock_guard<std::mutex> lk(g_mx);
#ifdef WITH_FSR2
    DestroyFsr2();
#endif
    g_device = nullptr;
    g_queue = nullptr;
    g_enabled.store(false, std::memory_order_relaxed);
    g_mode.store(UpscaleMode::Off, std::memory_order_relaxed);
}

void MB::Upscaler_SetMode(UpscaleMode m) {
    std::lock_guard<std::mutex> lk(g_mx);
    if (g_mode.load(std::memory_order_relaxed) == m) return;
    g_mode.store(m, std::memory_order_relaxed);
#ifdef WITH_FSR2
    if (m == UpscaleMode::FSR2) {
        CreateFsr2IfNeeded();
    }
    else {
        DestroyFsr2();
    }
#endif
}

UpscaleMode MB::Upscaler_GetMode() {
    return g_mode.load(std::memory_order_relaxed);
}

void MB::Upscaler_Resize(const UpscalerParams& p) {
    std::lock_guard<std::mutex> lk(g_mx);
    g_params = p;
#ifdef WITH_FSR2
    RecreateFsr2();
#endif
}

void MB::Upscaler_SetParams(const UpscalerParams& p) {
    std::lock_guard<std::mutex> lk(g_mx);
    g_params = p;
    // For FSR2, many params are supplied at dispatch time; no recreate needed here.
}

void MB::Upscaler_SetResourcesD3D12(const UpscalerResourcesD3D12& r) {
    std::lock_guard<std::mutex> lk(g_mx);
    g_res = r;
}

void MB::Upscaler_Enable(bool enabled) {
    g_enabled.store(enabled, std::memory_order_relaxed);
    if (!enabled) {
        // Optional: flag history reset next time we enable
        std::lock_guard<std::mutex> lk(g_mx);
        g_params.resetHistory = true;
    }
}

bool MB::Upscaler_IsEnabled() {
    return g_enabled.load(std::memory_order_relaxed);
}

void MB::Upscaler_Evaluate_D3D12(ID3D12GraphicsCommandList* cmdList) {
    if (!g_enabled.load(std::memory_order_relaxed)) return;
    const auto mode = g_mode.load(std::memory_order_relaxed);
    if (mode != UpscaleMode::FSR2) return;

#ifdef WITH_FSR2
    if (!g_fsr2Created) return;

    // Validate resources
    if (!g_res.color || !g_res.depth || !g_res.motionVectors) {
        Upscaler_Log("FSR2: missing inputs (color/depth/mv). Skipping.");
        return;
    }

    FfxFsr2DispatchDescription d{};
    d.commandList = cmdList;

    d.color.resource = g_res.color;
    d.color.subresourceIndex = 0;
    d.color.mipLevel = 0;

    d.depth.resource = g_res.depth;
    d.depth.subresourceIndex = 0;
    d.depth.mipLevel = 0;

    d.motionVectors.resource = g_res.motionVectors;
    d.motionVectors.subresourceIndex = 0;
    d.motionVectors.mipLevel = 0;

    if (g_res.exposure) {
        d.exposure.resource = g_res.exposure;
        d.exposure.subresourceIndex = 0;
        d.exposure.mipLevel = 0;
    }
    else {
        d.exposure.resource = nullptr; // FSR2 computes exposure if not provided
    }

    d.renderSize.width = g_params.renderWidth;
    d.renderSize.height = g_params.renderHeight;

    d.jitterOffset.x = g_params.jitterX;
    d.jitterOffset.y = g_params.jitterY;

    d.motionVectorScale.x = 1.0f; // set according to your MV convention
    d.motionVectorScale.y = 1.0f;

    d.reset = g_params.resetHistory ? FFX_TRUE : FFX_FALSE;
    d.enableSharpening = FFX_TRUE;
    d.sharpness = g_params.sharpness; // [0..1]

    d.frameTimeDelta = g_params.deltaTime;

    const auto rc = ffxFsr2ContextDispatch(&g_fsr2Ctx, &d);
    if (rc != FFX_OK) {
        Upscaler_Logf("FFX: Dispatch failed rc=%d", rc);
    }

    // Clear the reset flag after use
    g_params.resetHistory = false;
#else
    (void)cmdList;
#endif
}
