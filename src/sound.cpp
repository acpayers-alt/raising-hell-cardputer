#include "sound.h"

#include <Arduino.h>
#include <EEPROM.h>

#include "app_state.h"
#include "ui_runtime.h"
#include "user_toggles_state.h"

// M5Cardputer buzzer
#include "M5Cardputer.h"

static constexpr int EEPROM_SOUND_ADDR = 500;
static constexpr int EEPROM_VOL_ADDR = 501; // 0..3 (OFF/LOW/MED/HIGH)

static bool s_deathDirgePlayed = false;
static SoundVolumeLevel s_volumeLevel = SOUND_VOL_HIGH;
static bool s_restoreVolumeAfterSequence = false;

static uint32_t s_lastFlapMs = 0;

void soundFuneralDirge();         // longer dirge for death screen
void soundResetDeathDirgeLatch(); // call when leaving death screen / resurrect

static bool s_flatlineFadeActive = false;
static uint32_t s_flatlineStartMs = 0;
static uint32_t s_flatlineDurationMs = 0;
static uint8_t s_flatlineStartVol = 0;

// -----------------------------------------------------------------------------
// Speaker volume mapping (0..3 -> driver volume)
// -----------------------------------------------------------------------------
static void applySpeakerVolume()
{
  static const uint8_t kVolMap[4] = {
      0,   // OFF
      120, // LOW
      180, // MED
      255, // HIGH
  };

  uint8_t lvl = (uint8_t)s_volumeLevel;
  if (lvl > (uint8_t)SOUND_VOL_HIGH)
    lvl = (uint8_t)SOUND_VOL_HIGH;

  M5Cardputer.Speaker.setVolume(kVolMap[lvl]);
}

// -----------------------------------------------------------------------------
// UI SFX mute gate (shared across the whole program)
// -----------------------------------------------------------------------------
static uint32_t s_uiMuteUntilMs = 0;

bool uiMutedNow() { return (int32_t)(millis() - s_uiMuteUntilMs) < 0; }

void soundMuteUi(uint32_t ms)
{
  const uint32_t now = millis();
  s_uiMuteUntilMs = now + ms;
}

// -----------------------------------------------------------------------------
// Tiny non-blocking tone sequencer
// -----------------------------------------------------------------------------
struct ToneStep
{
  uint16_t freqHz; // 0 = silence
  uint16_t ms;     // duration
  uint16_t gapMs;  // silence after
};

static const ToneStep *s_seq = nullptr;
static uint8_t s_seqCount = 0;
static uint8_t s_seqIdx = 0;
static uint32_t s_nextChangeMs = 0;
static bool s_playing = false;

static uint32_t s_lastUiSfxMs = 0; // anti-spam
static uint32_t s_lastLowHpMs = 0; // low HP pulse

static inline void speakerTone(uint16_t hz, uint16_t ms)
{
  if (!soundEnabled)
    return;
  if (hz == 0 || ms == 0)
    return;
  M5Cardputer.Speaker.tone(hz, ms);
}

static void startSequence(const ToneStep *seq, uint8_t count)
{
  if (!soundEnabled)
    return;
  if (!seq || count == 0)
    return;

  s_seq = seq;
  s_seqCount = count;
  s_seqIdx = 0;
  s_playing = true;

  const uint32_t now = millis();

  // Kick the first step immediately so the very first UI sound after boot
  // can't get "lost" waiting for the next soundTick().
  const ToneStep &st0 = s_seq[0];
  speakerTone(st0.freqHz, st0.ms);

  s_seqIdx = 1;
  s_nextChangeMs = now + (uint32_t)st0.ms + (uint32_t)st0.gapMs;
}

void soundTick()
{
  if (!soundEnabled)
    return;
  if (!s_playing || !s_seq || s_seqCount == 0)
    return;

  const uint32_t now = millis();
  if ((int32_t)(now - s_nextChangeMs) < 0)
    return;

  if (s_seqIdx >= s_seqCount)
  {
    s_playing = false;
    s_seq = nullptr;
    s_seqCount = 0;
    s_seqIdx = 0;

    if (s_restoreVolumeAfterSequence)
    {
      s_restoreVolumeAfterSequence = false;
      applySpeakerVolume();
    }

    return;
  }

  const ToneStep &st = s_seq[s_seqIdx++];
  speakerTone(st.freqHz, st.ms);
  s_nextChangeMs = now + (uint32_t)st.ms + (uint32_t)st.gapMs;
}

// -----------------------------------------------------------------------------
// Near-death warning beep (rate-limited)
// -----------------------------------------------------------------------------
void soundTickNearDeath(bool dangerActive)
{
  static uint32_t s_nextBeepMs = 0;
  static uint8_t s_phase = 0;
  static bool s_finalPlayed = false;

  if (!dangerActive)
  {
    s_nextBeepMs = 0;
    s_phase = 0;
    s_finalPlayed = false;
    return;
  }

  // Block near-death audio anywhere except the live pet screen.
  // This prevents beeps/flatline during boot, preboot, OTA provisioning,
  // setup flows, choose-pet, console, death UI, etc.
  if (g_app.uiState != UIState::PET_SCREEN)
  {
    s_nextBeepMs = 0;
    s_phase = 0;
    s_finalPlayed = false;
    return;
  }

  if (!soundEnabled)
    return;
  if (uiMutedNow())
    return;

  const uint32_t now = millis();

  if (s_nextBeepMs != 0 && (int32_t)(now - s_nextBeepMs) < 0)
    return;

  // --- Phase timing (slowing heartbeat) ---
  static const uint16_t kIntervals[] = {90, 130, 180, 240};
  const uint16_t freq = 2600;
  const uint16_t dur = 40;

  if (s_phase < (sizeof(kIntervals) / sizeof(kIntervals[0])))
  {
    // Normal beeps (slowing)
    M5Cardputer.Speaker.tone(freq, dur);

    s_nextBeepMs = now + kIntervals[s_phase];
    s_phase++;
    return;
  }

  // --- Final flatline tone ---
  if (!s_finalPlayed)
  {
    s_finalPlayed = true;

    // Start long tone
    M5Cardputer.Speaker.tone(1800, 1400);

    if (soundGetVolumeLevel() != SOUND_VOL_OFF)
    {
      uint8_t baseVol = M5Cardputer.Speaker.getVolume();

      for (int i = 0; i < 5; i++)
      {
        uint8_t v = (baseVol * (5 - i)) / 5;
        M5Cardputer.Speaker.setVolume(v);
        delay(80);
      }

      applySpeakerVolume();
    }
  }
}

// -----------------------------------------------------------------------------
// Volume (typed-only API)
// -----------------------------------------------------------------------------
SoundVolumeLevel soundGetVolumeLevel()
{
  uint8_t v = (uint8_t)s_volumeLevel;
  if (v > (uint8_t)SOUND_VOL_HIGH)
    v = (uint8_t)SOUND_VOL_HIGH;
  return (SoundVolumeLevel)v;
}

void soundSetVolumeLevel(SoundVolumeLevel level)
{
  uint8_t v = (uint8_t)level;
  if (v > (uint8_t)SOUND_VOL_HIGH)
    v = (uint8_t)SOUND_VOL_HIGH;

  s_volumeLevel = (SoundVolumeLevel)v;

  EEPROM.write(EEPROM_VOL_ADDR, v);
  EEPROM.commit();

  // OFF mutes by disabling sound entirely
  soundEnabled = (s_volumeLevel != SOUND_VOL_OFF);

  // Re-apply volume immediately (some drivers reset it)
  applySpeakerVolume();
}

void soundAdjustVolume(int delta)
{
  int v = (int)((uint8_t)soundGetVolumeLevel());
  v += delta;

  // wrap 0..3
  if (v < 0)
    v = (int)SOUND_VOL_HIGH;
  if (v > (int)SOUND_VOL_HIGH)
    v = 0;

  soundSetVolumeLevel((SoundVolumeLevel)v);
}

const char *soundVolumeToText(SoundVolumeLevel level)
{
  switch (level)
  {
  case SOUND_VOL_OFF:
    return "OFF";
  case SOUND_VOL_LOW:
    return "LOW";
  case SOUND_VOL_MED:
    return "MED";
  case SOUND_VOL_HIGH:
    return "HIGH";
  default:
    return "HIGH";
  }
}

// -----------------------------------------------------------------------------
// Persisted setting
// -----------------------------------------------------------------------------
static void loadVolumeSetting()
{
  uint8_t v = EEPROM.read(EEPROM_VOL_ADDR);
  if (v == 0xFF)
  {
    // First boot default
    s_volumeLevel = SOUND_VOL_LOW;
    EEPROM.write(EEPROM_VOL_ADDR, (uint8_t)s_volumeLevel);
    EEPROM.commit();
  }
  else
  {
    if (v > (uint8_t)SOUND_VOL_HIGH)
      v = (uint8_t)SOUND_VOL_HIGH;
    s_volumeLevel = (SoundVolumeLevel)v;
  }
}

void initSound()
{
  loadVolumeSetting();
  applySpeakerVolume();

  // OFF means fully muted (including sequences)
  soundEnabled = (s_volumeLevel != SOUND_VOL_OFF);

  // Prime the speaker PWM/timer once so the first "real" UI beep isn't lost.
  // (First tone after boot can be swallowed by the driver init.)
  if (soundEnabled)
  {
    M5Cardputer.Speaker.tone(2000, 1); // 1ms: effectively inaudible
    delay(2);                          // let the driver latch the timer
  }
}

void toggleSound()
{
  soundEnabled = !soundEnabled;
  EEPROM.write(EEPROM_SOUND_ADDR, soundEnabled ? 1 : 0);
  EEPROM.commit();

  // Re-apply volume when turning ON (some drivers reset it).
  if (soundEnabled)
    applySpeakerVolume();

  // Tiny feedback if turning ON
  if (soundEnabled)
  {
    static const ToneStep kOn[] = {{2600, 20, 6}, {3200, 30, 0}};
    startSequence(kOn, (uint8_t)(sizeof(kOn) / sizeof(kOn[0])));
  }
}

// -----------------------------------------------------------------------------
// UI SFX sequences
// -----------------------------------------------------------------------------
static inline bool uiSfxCooldown(uint32_t minMs)
{
  const uint32_t now = millis();

  // Allow the very first UI sound after boot.
  if (s_lastUiSfxMs == 0)
  {
    s_lastUiSfxMs = now;
    return false;
  }

  if ((uint32_t)(now - s_lastUiSfxMs) < minMs)
    return true;
  s_lastUiSfxMs = now;
  return false;
}

void soundClick()
{
  if (!soundEnabled)
    return;
  if (uiSfxCooldown(35))
    return;
  static const ToneStep k[] = {{3000, 18, 0}};
  startSequence(k, 1);
}

void soundMenuTick()
{
  if (!soundEnabled)
    return;
  if (uiSfxCooldown(25))
    return;
  static const ToneStep k[] = {{2200, 14, 0}};
  startSequence(k, 1);
}

void soundConfirm()
{
  if (!soundEnabled)
    return;
  if (uiSfxCooldown(50))
    return;
  static const ToneStep k[] = {{2400, 22, 8}, {3200, 34, 0}};
  startSequence(k, (uint8_t)(sizeof(k) / sizeof(k[0])));
}

void soundCancel()
{
  if (!soundEnabled)
    return;
  if (uiSfxCooldown(50))
    return;
  static const ToneStep k[] = {{2200, 18, 6}, {1600, 36, 0}};
  startSequence(k, (uint8_t)(sizeof(k) / sizeof(k[0])));
}

void soundError()
{
  if (!soundEnabled)
    return;
  if (uiSfxCooldown(120))
    return;
  static const ToneStep k[] = {{260, 120, 0}};
  startSequence(k, 1);
}

void soundSleep()
{
  if (!soundEnabled)
    return;
  if (uiSfxCooldown(120))
    return;
  static const ToneStep k[] = {{2200, 30, 8}, {1600, 55, 0}};
  startSequence(k, (uint8_t)(sizeof(k) / sizeof(k[0])));
}

void soundWake()
{
  if (!soundEnabled)
    return;
  if (uiSfxCooldown(120))
    return;
  static const ToneStep k[] = {{1600, 30, 8}, {2400, 55, 0}};
  startSequence(k, (uint8_t)(sizeof(k) / sizeof(k[0])));
}

void soundDeath()
{
  if (!soundEnabled)
    return;
  // let this play even if spammed, but still guard a bit
  if (uiSfxCooldown(250))
    return;
  static const ToneStep k[] = {
      {520, 70, 14},
      {360, 70, 14},
      {220, 120, 0},
  };
  startSequence(k, (uint8_t)(sizeof(k) / sizeof(k[0])));
}

void soundLowHealthTick(uint8_t hp, bool sleeping, bool screenOn, bool inDeathScreen)
{
  (void)screenOn; // intentionally ignored; warning should work screen-on or screen-off

  static uint32_t s_nextHeartbeatMs = 0;

  if (!soundEnabled)
    return;
  if (uiMutedNow())
    return;

  if (sleeping || inDeathScreen)
  {
    s_lastLowHpMs = 0;
    s_nextHeartbeatMs = 0;
    return;
  }

  if (hp > 20)
  {
    s_lastLowHpMs = 0;
    s_nextHeartbeatMs = 0;
    return;
  }

  // Don't stomp other currently-playing SFX sequences.
  if (s_playing)
    return;

  const uint32_t now = millis();
  if (s_nextHeartbeatMs != 0 && (int32_t)(now - s_nextHeartbeatMs) < 0)
    return;

  // Heartbeat cadence tightens as HP gets worse.
  const ToneStep *seq = nullptr;
  uint8_t seqCount = 0;
  uint16_t cycleMs = 0;

  static const ToneStep kHeartbeat20[] = {
      {1500, 22, 55},
      {1200, 34, 0},
  };

  static const ToneStep kHeartbeat10[] = {
      {1600, 24, 45},
      {1250, 38, 0},
  };

  static const ToneStep kHeartbeat05[] = {
      {1700, 26, 35},
      {1300, 42, 0},
  };

  if (hp <= 5)
  {
    seq = kHeartbeat05;
    seqCount = (uint8_t)(sizeof(kHeartbeat05) / sizeof(kHeartbeat05[0]));
    cycleMs = 900;
  }
  else if (hp <= 10)
  {
    seq = kHeartbeat10;
    seqCount = (uint8_t)(sizeof(kHeartbeat10) / sizeof(kHeartbeat10[0]));
    cycleMs = 1400;
  }
  else
  {
    seq = kHeartbeat20;
    seqCount = (uint8_t)(sizeof(kHeartbeat20) / sizeof(kHeartbeat20[0]));
    cycleMs = 2200;
  }

  s_lastLowHpMs = now;
  s_nextHeartbeatMs = now + cycleMs;
  startSequence(seq, seqCount);
}

void soundDeathFlatline()
{
  if (soundGetVolumeLevel() == SOUND_VOL_OFF)
    return;
  if (!soundEnabled)
    return;
  if (uiMutedNow())
    return;

  const uint16_t freq = 1800;
  const uint32_t durMs = 3200; // or whatever final duration you want

  s_playing = false;
  s_seq = nullptr;
  s_seqCount = 0;
  s_seqIdx = 0;
  s_nextChangeMs = 0;

  s_flatlineFadeActive = true;
  s_flatlineStartMs = millis();
  s_flatlineDurationMs = durMs;

  // Capture current speaker volume as the starting point.
  s_flatlineStartVol = M5Cardputer.Speaker.getVolume();

  M5Cardputer.Speaker.tone(freq, durMs);
}

void soundResetDeathDirgeLatch() { s_deathDirgePlayed = false; }

void soundFuneralDirge()
{
  if (soundGetVolumeLevel() == SOUND_VOL_OFF)
    return;
  if (!soundEnabled)
    return;
  if (s_deathDirgePlayed)
    return;
  s_deathDirgePlayed = true;

  // Ensure speaker is at current menu volume (some speaker paths reset it)
  // Apply base volume first
  applySpeakerVolume();

  // Boost dirge if volume is LOW (to avoid it sounding too quiet)
  s_restoreVolumeAfterSequence = false;

  if (soundGetVolumeLevel() == SOUND_VOL_LOW)
  {
    // Temporary boost for dirge only
    M5Cardputer.Speaker.setVolume(180); // match MED feel
    s_restoreVolumeAfterSequence = true;
  }

  // Don't let UI spam immediately re-trigger
  s_lastUiSfxMs = millis();

  static const ToneStep k[] = {
      {440, 520, 80},  // A4
      {392, 520, 80},  // G4
      {349, 520, 80},  // F4
      {330, 520, 80},  // E4
      {294, 620, 140}, // D4
      {220, 900, 0},   // A3 (final low note)
  };

  startSequence(k, (uint8_t)(sizeof(k) / sizeof(k[0])));
}

void soundTickFlatlineFade()
{
  if (!s_flatlineFadeActive)
    return;

  const uint32_t now = millis();
  const uint32_t elapsed = now - s_flatlineStartMs;

  if (elapsed >= s_flatlineDurationMs)
  {
    M5Cardputer.Speaker.setVolume(s_flatlineStartVol);
    M5Cardputer.Speaker.end();
    s_flatlineFadeActive = false;
    return;
  }

  // Keep full volume for most of the tone, then fade in the last 35%.
  const uint32_t fadeStartMs = (s_flatlineDurationMs * 60) / 100;

  if (elapsed < fadeStartMs)
    return;

  const uint32_t fadeElapsed = elapsed - fadeStartMs;
  const uint32_t fadeLen = s_flatlineDurationMs - fadeStartMs;

  uint8_t v = s_flatlineStartVol;
  if (fadeLen > 0)
  {
    v = (uint8_t)(((uint32_t)s_flatlineStartVol * (fadeLen - fadeElapsed)) / fadeLen);
  }

  M5Cardputer.Speaker.setVolume(v);
}

void soundEvoZap()
{
  if (!soundEnabled)
    return;

  // quick rising chirp (non-blocking tone calls are okay if your Speaker.tone is async)
  M5Cardputer.Speaker.tone(1200, 30);
  delay(10);
  M5Cardputer.Speaker.tone(1700, 40);
}

void soundFlap()
{
  if (!soundEnabled)
    return;

  const uint32_t now = millis();

  // Much less spammy than soundConfirm(). Tune 120–180ms to taste.
  const uint32_t kMinMs = 140;
  if (s_lastFlapMs != 0 && (uint32_t)(now - s_lastFlapMs) < kMinMs)
    return;
  s_lastFlapMs = now;

  // Short, softer chirp (single step, not the 2-step "confirm" fanfare)
  static const ToneStep k[] = {{2000, 16, 0}};
  startSequence(k, 1);
}
