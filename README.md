# MirrorBlade

**Status:** foundational bridge skeleton for future RED4ext integrations.  
This repository provides a minimal plugin scaffold and documentation only.  
It **does not** include gameplay features (e.g., traffic, upscaling) by default.

## Overview
- Named pipe bridge for simple JSON request/response patterns.
- Clean separation for future op registration and dispatch.
- Minimal configuration file loader.

> This package is intentionally featureless. It’s a label-only drop: **MirrorBlade**.

## File Layout
- `README.md` – this document
- `LICENSE.md` – Broadcasted Fee Entry (BFE) License
- `config.json` – minimal config (empty onLoad)

## Build & Install (high-level)
1. Build your RED4ext plugin using your own CMake/VS solution.
2. Place the resulting DLL under `red4ext/plugins/MirrorBlade/`.
3. Copy `config.json` next to the DLL (same folder).

## Runtime
- Pipe name and ops are up to you; the sample code you add should document its own schema.
- This package ships with no ops and no behavior.

## Support
Open an issue or PR once you add your own modules.

Broadcasted Fee Entry (BFE) License
==================================

License ID: BFE-TGDK-MIRRORBLADE-001
Applies To: MirrorBlade and all associated source, binary, and configuration files.
Licensor: TGDK LLC, EIN 99-4502079, Fredericksburg, Virginia.
Date of Issue: 2025-08-18

1. Grant of Use
---------------
This License permits any individual or organization to use, modify, and distribute the
software, provided all usage is broadcasted (publicly verifiable), and all derivative
works maintain this license header.

2. Broadcast Requirement
------------------------
- All substantial deployments or redistributions must include a visible notice
  (README, website, or documentation) stating:
    "Powered by MirrorBlade — Licensed under TGDK BFE"
- Any concealment, misattribution, or false claims of authorship voids this license.

3. Fee Structure
----------------
- No direct monetary fee is required for personal or academic use.
- Commercial, enterprise, or government adoption requires a Broadcasted Fee Entry:
  • A one-time symbolic entry fee recorded in the TGDK BFE ledger.
  • Optional ongoing support/partnership agreements negotiated separately.

4. Derivative Works
-------------------
- Forks, modifications, or derivative projects must:
  • Retain TGDK copyright and BFE notice.
  • Register their own BFE license ID with TGDK if redistributed outside of private use.

5. Liability & Warranty
-----------------------
- The software is provided "AS IS", without warranty of any kind.
- TGDK LLC and contributors are not liable for damages, direct or indirect,
  arising from use.

6. Revocation
-------------
TGDK LLC reserves the right to revoke or deny BFE validation if a user is found
violating terms, obscuring authorship, or broadcasting false claims.

---------------------------------------------------------------
© 2025 TGDK LLC. All rights reserved.
Registered under the TGDK Vault and Broadcasted Fee Entry Ledger.

