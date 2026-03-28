# Changelog

All notable changes to this project will be documented in this file.

---

## [Unreleased] → v1.1.0 (Post v1.0.0)

### ✨ Added
- **Eldritch pet type fully integrated**
  - Unique mini-game theming across Flappy, Dodger, and Crossy
  - New assets (merman, sigils, underwater/cosmic environments)
  - Pet-specific flavor text (e.g. “Curse the Merman!”)
- **Pet-dependent mini-game assets**
  - Dynamic asset resolution based on pet type
  - Separate asset pipelines for Devil vs Eldritch variants
- **Developer console improvements**
  - Commands such as `healpet`, `setrest`, and pet switching
  - Improved debugging and testing workflows
- **Improved installation paths**
  - Better support for M5Launcher, M5Burner, and manual flashing
  - Clearer first-boot asset provisioning flow

---

### 🔧 Changed
- **Mini-game asset system overhaul**
  - Introduced staged loading (preload vs deferred assets)
  - Deterministic asset lifecycle using `mgmem` sessions
  - Improved logging for heap usage and asset events
- **Dodger background system refactored**
  - Replaced large shared background with split sprites (fragmentation-safe)
- **Mini-game rendering logic hardened**
  - Animation systems now tolerate partial asset loads
  - Fallback rendering prevents missing sprites (no more flicker)
- **General codebase direction**
  - Ongoing refactor toward modular state management
  - Reduced reliance on global state
  - Improved separation of systems (UI, gameplay, platform)

---

### 🐛 Fixed
- **Onboarding / Wi-Fi setup issues**
  - Fixed stale credential handling from HLauncher installs
  - Added full SSID scan and fallback handling
- **Boot pipeline stability**
  - Fixed cases where onboarding state could be skipped or partially resumed
  - Improved handling of incomplete setup states
- **Pet persistence regression**
  - Fixed issue where existing pets could revert to egg selection after reboot
- **Mini-game rendering bugs**
  - Fixed flashing sprites caused by missing animation frames
  - Fixed missing background issues caused by heap fragmentation
- **Asset provisioning reliability**
  - Improved handling of partial/missing asset states
  - Reduced failure cases during first boot

---

### ⚙️ Technical
- ~79 commits and 130+ files changed since v1.0.0
- Significant improvements to memory stability on ESP32-S3 (no PSRAM)
- Introduced consistent asset session lifecycle:
  - `beginSession()` / `endSession()`
  - deterministic load/unload behavior
- Improved heap fragmentation resilience:
  - smaller sprite allocations
  - staged loading patterns
- Expanded internal logging:
  - `[MG HEAP]`, `[MGMEM]`, `[BOOTPIPE]`, `[OTA]`

---

### 🧪 Stability Improvements
- Eliminated gameplay hitches during critical transitions (e.g. Dodger coast phase)
- Ensured repeat-run stability across mini-games
- Improved recovery of heap state between sessions
- Prevented asset-related crashes and visual corruption

---

### ⚠️ Known Limitations
- Some assets may fall back to single-frame rendering under extreme memory pressure
- No PSRAM support — all optimizations assume constrained heap
- Continued refactor work in progress (state manager, modularization)

---

# 🧠 Notes

This release represents a major step beyond the initial v1.0.0 launch, focusing on:
- **stability**
- **memory safety**
- **content expansion**
- **developer tooling**

The asset system and mini-game architecture have been significantly hardened to support future expansion.

---

## [1.0.1] - 2026-03-16


Added
Console system (public build enabled)
Version command (firmware + asset version reporting)
Uptime command
OTA asset version visibility via console
Rename Pet option in Game Options menu
Expanded timezone support for global users
Mini Games Graphics
OTA Asset Provisioning

Improved
Boot pipeline logging and diagnostics
Mini-game memory session handling (mgmem lifecycle)
Return flow from mini-games (correct state restoration)
Game Options menu layout and UI polish
General UI responsiveness and redraw consistency

Fixed
Keyboard/input inconsistencies across flows
Incorrect mini-game cleanup leading to unstable state transitions

Changed
Unified version reporting (firmware + assets combined)
Improved asset manifest handling and parsing robustness
Refined mini-game asset loading strategy to reduce fragmentation risk

Notes
First post-release stability update following v1.0.0.
Focus on crash fixes, OTA reliability, and user-facing diagnostics.
Lays groundwork for future full OTA firmware updates.

---
## [0.1.0] - 2026-02-21


### Added
- Initial public GitHub release
- Modular state architecture
- Resurrection mini-game
- Infernal Dodgers mini-game
- Power menu system
- Sleep cycle handling
- SD-based sprite animation system

### Refactored
- Removed legacy global usage across major systems
- Centralized UI redraw logic
- Modularized UI state transitions

### Notes
Stable known-good build prior to deeper platform separation refactor.