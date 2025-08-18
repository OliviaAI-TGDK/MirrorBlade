// include/Upscaler.hpp
#pragma once
#include <cstdint>
#include <atomic>
#include <d3d12.h>

namespace MB {

    enum class UpscaleMode {
        Off,
        FSR2,
        // Streamline/DLSS/XeSS can be added later
    };

    struct UpscalerResourcesD3D12 {
        // Input color at *render resolution* (before TAA if you want best quality; after works too)
        ID3D12Resource* color = nullptr;          // SRV
        // Camera-space or screen-space linearized depth
        ID3D12Resource* depth = nullptr;          // SRV
        // Motion vectors (usually RG16F; x=right, y=down in pixels or NDC depending on config)
        ID3D12Resource* motionVectors = nullptr;  // SRV
        // Optional: exposure/auto-exposure buffer (luminance history or EV100)
        ID3D12Resource* exposure = nullptr;       // SRV (can be null; FSR2 can auto-generate)
    };

    struct UpscalerParams {
        uint32_t renderWidth = 1920;   // internal render res
        uint32_t renderHeight = 1080;
        uint32_t outputWidth = 3840;   // display/output res
        uint32_t outputHeight = 2160;
        float    sharpness = 0.6f;   // 0..1 (FSR2’s RCAS-style sharpening)
        float    jitterX = 0.0f;   // TAA jitter in pixels or NDC (FSR2 expects NDC [-0.5..0.5] usually)
        float    jitterY = 0.0f;
        float    deltaTime = 1.0f / 60.0f;
        bool     resetHistory = false;
    };

    bool  Upscaler_Init_D3D12(ID3D12Device* device, ID3D12CommandQueue* queue);
    void  Upscaler_Shutdown();

    void  Upscaler_SetMode(UpscaleMode m);
    UpscaleMode Upscaler_GetMode();

    void  Upscaler_Resize(const UpscalerParams& p);  // (re)create internal FSR2 targets
    void  Upscaler_SetParams(const UpscalerParams& p);
    void  Upscaler_SetResourcesD3D12(const UpscalerResourcesD3D12& r);

    // Dispatch upscale into the *current* command list.
    // color/depth/mv/exposure must be set via Upscaler_SetResourcesD3D12 beforehand.
    void  Upscaler_Evaluate_D3D12(ID3D12GraphicsCommandList* cmdList);

    // Convenience: on/off
    void  Upscaler_Enable(bool enabled);
    bool  Upscaler_IsEnabled();

    // Simple logging hooks (implemented in this .cpp to avoid cross-unit coupling)
    void  Upscaler_Log(const char* msg);
    void  Upscaler_Logf(const char* fmt, ...);

} // namespace MB
