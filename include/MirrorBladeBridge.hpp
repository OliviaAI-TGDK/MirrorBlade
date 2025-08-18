#pragma once
#include <cstdint>
#include "RED4ext/Api/Sdk.hpp"


namespace MB
{
    // Start everything (ops registry, pipe server, tick worker).
    // Pass the SDK if you want to stash it; it's optional for now.
    void InitBridge(const RED4ext::Sdk* sdk = nullptr);

    // Stop workers, close pipe, cleanup.
    void ShutdownBridge();

    // Optional: run queued tasks once (useful if you later wire a real game-tick).
    void PumpOnce();
}
