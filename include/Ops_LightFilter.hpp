#pragma once

namespace MB {

	// Registers JSON ops with MB::Ops (if available):
	//   - "lights.fake.adverts"      { "enabled": bool }
	//   - "lights.fake.portals"      { "enabled": bool }
	//   - "lights.fake.forceportals" { "enabled": bool }
	//   - "lights.fake.sweep"        {}
	//
	// Safe to call unconditionally; becomes a no-op if MBOps.hpp is not present.
	void RegisterLightFilterOps_JSON();

} // namespace MB
