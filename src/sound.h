#pragma once
#include <Arduino.h>

// -----------------------------------------------------------------------------
// Sound (Cardputer ADV buzzer)
// -----------------------------------------------------------------------------
// The Cardputer ADV exposes a buzzer via M5Cardputer.Speaker.
// Keep sounds SHORT (20-120ms) and avoid blocking delays.
// This module provides a tiny non-blocking sequencer so multi-tone chirps
// don't stall gameplay.

// Initialize / load persisted setting
void initSound();

// Toggle soundEnabled and persist
void toggleSound();

// -----------------------------------------------------------------------------
// Volume (persisted)
// -----------------------------------------------------------------------------
enum SoundVolumeLevel : uint8_t {
  SOUND_VOL_OFF  = 0,
  SOUND_VOL_LOW  = 1,
  SOUND_VOL_MED  = 2,
  SOUND_VOL_HIGH = 3,
};

// Current volume level (0..3). Defaults to HIGH on first boot.
SoundVolumeLevel soundGetVolumeLevel();

// Set volume level (clamped to 0..3) and persist.
void soundSetVolumeLevel(SoundVolumeLevel level);

// Convenience: adjust volume by delta (+1 or -1), wrapping 0..3.
void soundAdjustVolume(int delta);

// Label helper for UI.
const char* soundVolumeToText(SoundVolumeLevel level);

// Call once per loop() to run queued sequences (safe to call always)
void soundTick();

// Call every frame (or often). Will beep when dangerActive is true.
// Works even if the screen is off.
void soundTickNearDeath(bool dangerActive);

// UI SFX mute gate (used by UI + warning beeps)
bool uiMutedNow();
void soundMuteUi(uint32_t ms);

// -----------------------------------------------------------------------------
// Longer / special sequences
// -----------------------------------------------------------------------------
void soundFuneralDirge();            // longer dirge for burial screen (one-shot latched)
void soundResetDeathDirgeLatch();    // resets one-shot latch for dirge

// Low-health warning pulse (call each loop; will self-rate-limit)
void soundLowHealthTick(uint8_t hp, bool sleeping, bool screenOn, bool inDeathScreen);
void soundDeathFlatline();
void soundTickFlatlineFade();

// -----------------------------------------------------------------------------
// UI SFX
// -----------------------------------------------------------------------------
void soundClick();          // button click
void soundMenuTick();       // menu move tick
void soundConfirm();        // confirm chirp
void soundCancel();         // cancel chirp
void soundError();          // error buzz
void soundSleep();          // sleep tone
void soundWake();           // wake tone
void soundDeath();          // death sting (short, dramatic, buzzy)
void soundEvoZap();
void soundFlap();


// -----------------------------------------------------------------------------
// Backwards-compat wrappers (so old code compiles)
// -----------------------------------------------------------------------------
inline void playBeep()       { soundClick(); }
inline void playRotateBeep() { soundMenuTick(); }
