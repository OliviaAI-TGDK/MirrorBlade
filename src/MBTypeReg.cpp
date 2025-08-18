// src/MBTypeReg.cpp
// IMPORTANT: Do NOT include "pch.hpp" here. Keep this file header-only.

namespace RED4ext { struct CRTTISystem; }  // forward decl only

namespace MB {

    void RegisterTypes(RED4ext::CRTTISystem* /*rtti*/) {
        // no-op; define real wiring elsewhere when ready
    }

    void PostRegisterTypes(RED4ext::CRTTISystem* /*rtti*/) {
        // no-op
    }

} // namespace MB
