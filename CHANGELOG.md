# Changelog

All notable changes to this project will be documented in this file.

<<<<<<< HEAD
## 2.1.0 — Stability & Polish Update

This release focuses on improving overall stability, consistency, and user experience across the entire game.

### Added
Timezone auto-detection has been added along with expanded timezone support, and a new Auto Clock idle mode is now available. The console has been improved with scrollback and better diagnostics, and a “What’s New” screen is now shown after updates.

### Changed
The boot flow and time synchronization handling have been refined, and there has been a general pass on UI polish across multiple screens. Audio behavior has been cleaned up with better state ownership, and the factory reset flow now includes a hold-to-confirm progress indicator for clearer interaction.

### Fixed
Several long-standing issues have been fixed, including the title screen background failing to render after navigation, the pet name intermittently disappearing on the menu, and a Wi-Fi issue where the device could remain enabled but disconnected after boot. Rendering reliability has been improved under memory pressure, and the save/load system has been hardened with better recovery behavior.

### Graphics & UI
Under the hood, the graphics system has been modularized into dedicated components, mini-games have been split into individual implementations, and state management and return flow handling have been significantly improved. The SD/JPG rendering pipeline has also been hardened to avoid edge case failures.


---
=======
## 2.1.0 - Stability & Polish Update
>>>>>>> main

This release focuses on improving overall stability, consistency, and user experience across the entire game.

### Added
Timezone auto-detection has been added along with expanded timezone support, and a new Auto Clock idle mode is now available. The console has been improved with scrollback and better diagnostics, and a “What’s New” screen is now shown after updates.

### Changed
The boot flow and time synchronization handling have been refined, and there has been a general pass on UI polish across multiple screens. Audio behavior has been cleaned up with better state ownership, and the factory reset flow now includes a hold-to-confirm progress indicator for clearer interaction.

### Fixed
Several long-standing issues have been fixed, including the title screen background failing to render after navigation, the pet name intermittently disappearing on the menu, and a Wi-Fi issue where the device could remain enabled but disconnected after boot. Rendering reliability has been improved under memory pressure, and the save/load system has been hardened with better recovery behavior.

### Graphics & UI
Under the hood, the graphics system has been modularized into dedicated components, mini-games have been split into individual implementations, and state management and return flow handling have been significantly improved. The SD/JPG rendering pipeline has also been hardened to avoid edge case failures.


---

## 2.0 Branch Bugfix Releases

## 2.0.3: (Depricated) 
This patch fixes a boot-time issue where the device could get stuck waiting on network time sync indefinitely on some networks.

Added a timeout fallback for BOOT_NTP_WAIT

Fixed the manual timezone path so it starts the NTP wait timer correctly
Added support for the Moscow timezone in boot-time timezone detection

This release improves recovery when NTP is unavailable or blocked and prevents users from getting trapped in the boot time-sync flow.

## 2.0.2: (Deprecated) 
Fixes an issue where asset provisioning could fail with “Enable WiFi first” after using recovery commands.

Fixed reprovision loop triggered by rescue flow
Unified WiFi handling for asset provisioning
repair assets now runs without requiring reboot
Falls back to WiFi setup when credentials are missing

## 2.0.1: (Deprecated) 
Hotfix release to resolve a rare boot stall during time sync.
Added: ntpskip console command to bypass stalled NTP checks
If your device appears stuck during boot:

Open the console (/ key)
Run: ntpskip
This will allow the device to continue booting normally.

No other changes included.


---
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