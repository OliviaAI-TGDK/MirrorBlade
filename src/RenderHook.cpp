// RenderHook.cpp - production-grade skeleton for per-frame upscaler dispatch on D3D12 Present
//
// This file is self-contained: it provides safe logging, device discovery from the swapchain,
// per-backbuffer allocator/fence management, and a Present-hook shim that records a small
// command list to run your upscaler right before Present.
//
// How to use:
//   1) Ensure you have an "Upscaler.hpp" that declares MB::Upscaler_* APIs used below.
//   2) Install your Present hook (MinHook, RED4ext, your engine) and point it at hkPresent.
//      Store the original Present in g_oPresent (see RenderHook_Attach below for MinHook).
//   3) Provide the engine's ID3D12CommandQueue* via RenderHook_SetQueue() (e.g., from your
//      own ExecuteCommandLists hook or engine init).
//   4) Implement FeedRequiredResourcesOncePerFrame() to pass color/depth/motion/exposure.
//
// Notes:
//   - This TU intentionally avoids any dependence on your other project files.
//   - If MB_ENABLE_MINHOOK is defined and MinHook is linked, RenderHook_Attach() will
//     install the Present hook for you given a Present address.
//   - If you don't define MB_ENABLE_MINHOOK, just call your hooker and use hkPresent.

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cstdarg>
#include <vector>
#include <atomic>

#include "Upscaler.hpp" // declares MB::Upscaler_* API

#if defined(MB_ENABLE_MINHOOK)
#include <MinHook.h>
#endif

using Microsoft::WRL::ComPtr;

// -------------------------------------------------------------------------------------------------
// Local logging (TU-local; no external header required)
// -------------------------------------------------------------------------------------------------
static void MB_Log(const char* s)
{
    OutputDebugStringA("[MirrorBlade/RenderHook] ");
    OutputDebugStringA(s);
    OutputDebugStringA("\n");
}
static void MB_Logf(const char* fmt, ...)
{
    char buf[1024]{};
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    MB_Log(buf);
}

// -------------------------------------------------------------------------------------------------
// Globals
// -------------------------------------------------------------------------------------------------
typedef HRESULT(STDMETHODCALLTYPE* PresentFn)(IDXGISwapChain* swap, UINT sync, UINT flags);
static PresentFn g_oPresent = nullptr;                // filled by your hooker or RenderHook_Attach()

static ID3D12Device* gDevice = nullptr;  // AddRef'ed here
static ID3D12CommandQueue* gQueue = nullptr;  // weak capture; set via RenderHook_SetQueue()
static UINT                 gBufferCount = 3;

struct FrameCtx
{
    ComPtr<ID3D12CommandAllocator> alloc;
    UINT64                         fenceValue = 0;
};
static std::vector<FrameCtx>             gFrames;
static ComPtr<ID3D12GraphicsCommandList> gCmd;
static ComPtr<ID3D12Fence>               gFence;
static HANDLE                            gFenceEvent = nullptr;
static UINT64                            gFenceCursor = 1;
static std::atomic<bool>                 gInitialized{ false };

// -------------------------------------------------------------------------------------------------
// Helper: current backbuffer index (IDXGISwapChain3 preferred; rotate if unavailable)
// -------------------------------------------------------------------------------------------------
static UINT GetCurrentBBIndex(IDXGISwapChain* swap)
{
    ComPtr<IDXGISwapChain3> sc3;
    if (SUCCEEDED(swap->QueryInterface(IID_PPV_ARGS(&sc3))))
        return sc3->GetCurrentBackBufferIndex();

    static UINT s_fake = 0;
    s_fake = (s_fake + 1u) % gBufferCount;
    return s_fake;
}

// -------------------------------------------------------------------------------------------------
// One-time init from swapchain (device, allocators, fence, command list)
// Queue is optional at init and can be provided later via RenderHook_SetQueue().
// -------------------------------------------------------------------------------------------------
static void EnsureInit(IDXGISwapChain* swap)
{
    if (gInitialized.load(std::memory_order_acquire))
        return;

    // Device
    ID3D12Device* dev = nullptr;
    if (FAILED(swap->GetDevice(__uuidof(ID3D12Device), (void**)&dev)) || !dev)
    {
        MB_Log("EnsureInit: swap->GetDevice(ID3D12Device) failed");
        return;
    }
    gDevice = dev; // Keep ref

    // Buffer count
    DXGI_SWAP_CHAIN_DESC scDesc{};
    if (SUCCEEDED(swap->GetDesc(&scDesc)) && scDesc.BufferCount >= 2 && scDesc.BufferCount <= 8)
        gBufferCount = scDesc.BufferCount;
    else
        gBufferCount = 3;

    // Fence + event
    if (FAILED(gDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gFence))))
    {
        MB_Log("EnsureInit: CreateFence failed");
        return;
    }
    gFenceEvent = CreateEventW(nullptr, FALSE, FALSE, L"MB_RenderHook_Fence");
    if (!gFenceEvent)
    {
        MB_Log("EnsureInit: CreateEvent failed");
        return;
    }

    // Per-backbuffer command allocators
    gFrames.resize(gBufferCount);
    for (UINT i = 0; i < gBufferCount; ++i)
    {
        if (FAILED(gDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&gFrames[i].alloc))))
        {
            MB_Logf("EnsureInit: CreateCommandAllocator failed (frame %u)", i);
            return;
        }
    }

    // Single reusable graphics command list
    if (FAILED(gDevice->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        gFrames[0].alloc.Get(), nullptr,
        IID_PPV_ARGS(&gCmd))))
    {
        MB_Log("EnsureInit: CreateCommandList failed");
        return;
    }
    gCmd->Close();

    // Initialize the upscaler with whatever we have now (queue may be null initially)
    MB::Upscaler_Init_D3D12(gDevice, gQueue);

    gInitialized.store(true, std::memory_order_release);
    MB_Logf("EnsureInit: OK (buffers=%u)", gBufferCount);
}

// -------------------------------------------------------------------------------------------------
// Provide engine resources to the upscaler each frame (YOU must fill these).
// Replace the nullptrs with your engine's resources (or descriptor info as your SDK requires).
// -------------------------------------------------------------------------------------------------
static void FeedRequiredResourcesOncePerFrame()
{
    MB::UpscalerResourcesD3D12 res{};
    res.color = nullptr; // REQUIRED: scene color (render resolution)
    res.depth = nullptr; // REQUIRED: depth    (render resolution)
    res.motionVectors = nullptr; // REQUIRED: motion vectors (render resolution)
    res.exposure = nullptr; // optional: exposure/auto-exposure buffer

    MB::Upscaler_SetResourcesD3D12(res);

    // If your SDK needs size updates (dynamic res), call your setters here:
    // MB::Upscaler_SetRenderSize(renderW, renderH);
    // MB::Upscaler_SetOutputSize(outputW, outputH); // e.g., 3840x2160 for 4K
}

// -------------------------------------------------------------------------------------------------
// Record and execute a tiny command list on gQueue to run the upscaler before Present.
// Uses a ring of allocators (one per backbuffer) guarded by a fence.
// -------------------------------------------------------------------------------------------------
static void ExecuteUpscalerNow(IDXGISwapChain* swap)
{
    if (!gInitialized.load() || !gDevice || !gCmd || !gFence)
        return;
    if (!gQueue) // not captured yet
        return;

    const UINT frame = GetCurrentBBIndex(swap);
    FrameCtx& ctx = gFrames[frame];

    // Wait for previous work using this allocator
    if (ctx.fenceValue && gFence->GetCompletedValue() < ctx.fenceValue)
    {
        gFence->SetEventOnCompletion(ctx.fenceValue, gFenceEvent);
        WaitForSingleObject(gFenceEvent, 16); // short wait to avoid long stalls
    }

    // Reset allocator and list
    if (FAILED(ctx.alloc->Reset()))
    {
        MB_Log("ExecuteUpscalerNow: alloc->Reset failed");
        return;
    }
    if (FAILED(gCmd->Reset(ctx.alloc.Get(), nullptr)))
    {
        MB_Log("ExecuteUpscalerNow: cmd->Reset failed");
        return;
    }

    // Provide resources and evaluate
    FeedRequiredResourcesOncePerFrame();

    if (MB::Upscaler_IsEnabled())
    {
        MB::Upscaler_Evaluate_D3D12(gCmd.Get());
    }

    // Submit
    gCmd->Close();
    ID3D12CommandList* lists[] = { gCmd.Get() };
    gQueue->ExecuteCommandLists(1, lists);

    // Fence this frame
    const UINT64 fv = gFenceCursor++;
    if (SUCCEEDED(gQueue->Signal(gFence.Get(), fv)))
        ctx.fenceValue = fv;
}

// -------------------------------------------------------------------------------------------------
// Present hook
// -------------------------------------------------------------------------------------------------
static HRESULT STDMETHODCALLTYPE hkPresent(IDXGISwapChain* swap, UINT sync, UINT flags)
{
    EnsureInit(swap);

    // If the upscaler is enabled, inject our pass before the app's Present
    if (MB::Upscaler_IsEnabled())
        ExecuteUpscalerNow(swap);

    return g_oPresent ? g_oPresent(swap, sync, flags) : S_OK;
}

// -------------------------------------------------------------------------------------------------
// Public helpers to integrate with your hooker / engine
// -------------------------------------------------------------------------------------------------

// Provide the engine's graphics queue when you have it (e.g., from your own queue hook).
extern "C" void RenderHook_SetQueue(ID3D12CommandQueue* queue)
{
    gQueue = queue;
    if (gDevice) {
        // Re-init with queue for better scheduling once available
        MB::Upscaler_Init_D3D12(gDevice, gQueue);
        MB_Logf("RenderHook: queue captured = %p", (void*)queue);
    }
}

#if defined(MB_ENABLE_MINHOOK)
// Attach Present hook using MinHook when you know the Present function address (IDXGISwapChain::Present).
extern "C" bool RenderHook_Attach(void* presentAddr)
{
    if (!presentAddr) { MB_Log("RenderHook_Attach: null Present address"); return false; }

    if (MH_Initialize() != MH_OK && MH_Initialize() != MH_ERROR_ALREADY_INITIALIZED)
    {
        MB_Log("RenderHook_Attach: MinHook init failed");
        return false;
    }

    if (MH_CreateHook(presentAddr, &hkPresent, reinterpret_cast<void**>(&g_oPresent)) != MH_OK)
    {
        MB_Log("RenderHook_Attach: MH_CreateHook failed");
        return false;
    }
    if (MH_EnableHook(presentAddr) != MH_OK)
    {
        MB_Log("RenderHook_Attach: MH_EnableHook failed");
        return false;
    }
    MB_Log("RenderHook: Present hook installed");
    return true;
}

// Detach all hooks (optional at shutdown)
extern "C" void RenderHook_Detach()
{
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
    MB_Log("RenderHook: hooks removed");
}
#else
// Stubs if you don't use MinHook in this TU. Install your hook elsewhere and make it call hkPresent.
extern "C" bool RenderHook_Attach(void* /*presentAddr*/)
{
    MB_Log("RenderHook_Attach: MB_ENABLE_MINHOOK not defined; install the hook in your host and target hkPresent.");
    return false;
}
extern "C" void RenderHook_Detach()
{
    MB_Log("RenderHook_Detach: MB_ENABLE_MINHOOK not defined.");
}
#endif
