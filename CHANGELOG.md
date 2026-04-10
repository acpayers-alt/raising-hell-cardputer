# Changelog

All notable changes to this project will be documented in this file.


## 2.0.0

### Added

Eldritch pet line
Pet Storage (store and recall pets)
Clock Mode
Pet walking and wandering behavior
Expanded in-game manual with scrolling
Screen flash alerts (paired with LED alerts)
Shake-to-wake option
Additional console/recovery tools

### Changed

Settings now use inline selection instead of popups
Improved asset provisioning and upgrade handling
Minimum asset pack updated to `1.1.9`
Gameplay tuning (mood thresholds, mini-game rewards)
Softer fallback for missing background assets

### Fixed

GO button not responding during walking animation
Clock mode rendering and state issues
Wi-Fi / time sync issues on boot
First-boot and title menu edge cases
Save/load and upgrade migration issues
LED alerts not visible while screen is on

### Animation & UI

Added and refined walk animations across pet stages
Added Eldritch Resurrection Run celebration visuals
General sprite alignment and presentation improvements


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