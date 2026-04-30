#include "anomaly_manager.h"

#include "app_state.h"
#include "boot_pipeline.h"
#include "build_flags.h"
#include "console.h"
#include "display.h"
#include "graphics.h"
#include "pet.h"
#include "sound.h"
#include "system_status_state.h"
#include "ui_defs.h"
#include "ui_input_router.h"

extern M5Canvas spr;

namespace
{
#if RH_ANOMALY_TEASER_ENABLED

enum class AnomalyType : uint8_t
{
  Scanline = 0,
  Tear = 1,
  AlienGlimpse = 2,
  LogOnly = 3,
};

bool s_active = false;
AnomalyType s_type = AnomalyType::Scanline;
uint32_t s_untilMs = 0;
uint32_t s_nextCheckMs = 0;
uint32_t s_cooldownUntilMs = 0;
uint32_t s_recentActivityUntilMs = 0;
uint32_t s_earliestActivityAnomalyMs = 0;
bool s_forcePending = false;
uint32_t s_forcePendingUntilMs = 0;
uint8_t s_frame = 0;

// About 1 in 220 checks. At 45s/check, average is roughly once per 2.75 hours
// while sitting in eligible states.
constexpr long kBaseChanceDenom = 220;
constexpr uint32_t kCheckIntervalMs = 45000UL;
constexpr uint32_t kCooldownMs = 45UL * 60UL * 1000UL;
constexpr uint32_t kVisualMs = 90UL;

bool uiAllowsAnomaly(UIState s)
{
  switch (s)
  {
  // Core pet + tabbed experience
  case UIState::PET_SCREEN:     // covers stats/feed/play in your current routing
  case UIState::PET_SLEEPING:
  case UIState::SLEEP_MENU:
  case UIState::INVENTORY:
  case UIState::SHOP:
  case UIState::CLOCK_MODE:
    return true;

  default:
    return false;
  }
}

bool hardSafetyAllowsAnomaly()
{
  if (!isScreenOn())
    return false;

  if (consoleIsOpen())
    return false;

  if (batteryCritical)
    return false;

  if (g_bootAssetProvisionActive || g_bootUiBlockedForAssetProvision)
    return false;

  if (!uiAllowsAnomaly(g_app.uiState))
    return false;

  return true;
}

void logAnomaly(AnomalyType t)
{
  switch (t)
  {
  case AnomalyType::Scanline:
    Serial.println("[WARN] frame mismatch");
    break;
  case AnomalyType::Tear:
    Serial.println("[SYS] render stream anomaly");
    break;
  case AnomalyType::AlienGlimpse:
    Serial.println("[EVENT] anomaly detected");
    break;
  case AnomalyType::LogOnly:
    Serial.println("[NET] unexpected packet signature");
    break;
  }
}

AnomalyType pickType()
{
  const long roll = random(100);

  // ~80% generic glitches, ~20% alien hints.
  if (roll < 40)
    return AnomalyType::Scanline;
  if (roll < 80)
    return AnomalyType::Tear;
  if (roll < 95)
    return AnomalyType::AlienGlimpse;

  return AnomalyType::LogOnly;
}

void triggerAnomalyEvent(uint32_t nowMs)
{
  s_type = pickType();
  s_frame = 0;
  s_cooldownUntilMs = nowMs + kCooldownMs;

  logAnomaly(s_type);

  if (s_type == AnomalyType::LogOnly)
    return;

  s_active = true;
  s_untilMs = nowMs + kVisualMs;

  soundAnomalyBlip();
  requestUIRedraw();
}

void drawScanlineDistortion()
{
  for (int y = TOP_BAR_H; y < SCREEN_H; y += 7)
  {
    const int xOff = (int)random(-5, 6);
    spr.drawFastHLine(max(0, xOff), y, SCREEN_W - abs(xOff), TFT_DARKGREY);
  }

  if ((s_frame & 1) == 0)
    spr.drawRect(0, TOP_BAR_H, SCREEN_W, SCREEN_H - TOP_BAR_H, TFT_WHITE);
}

void drawFrameTear()
{
  const int y = random(TOP_BAR_H + 8, SCREEN_H - 20);
  const int h = random(5, 14);
  const int x = random(-20, 20);

  spr.fillRect(0, y, SCREEN_W, h, TFT_BLACK);
  spr.drawFastHLine(max(0, x), y + h / 2, SCREEN_W - abs(x), TFT_WHITE);

  for (int i = 0; i < 8; ++i)
  {
    const int px = random(0, SCREEN_W);
    const int py = random(TOP_BAR_H, SCREEN_H);
    spr.drawPixel(px, py, TFT_WHITE);
  }
}

void drawSignalIntrusionFrame()
{
  const int cx = SCREEN_W / 2 + random(-18, 19);
  const int cy = TOP_BAR_H + 44 + random(-6, 7);

  // Intentionally incomplete and not a clean alien sprite.
  spr.drawLine(cx - 16, cy + 18, cx - 5, cy - 10, TFT_WHITE);
  spr.drawLine(cx + 16, cy + 18, cx + 5, cy - 10, TFT_WHITE);
  spr.drawLine(cx - 5, cy - 10, cx + 5, cy - 10, TFT_DARKGREY);

  spr.fillRect(cx - 10, cy - 1, 6, 2, TFT_WHITE);
  spr.fillRect(cx + 4, cy - 1, 6, 2, TFT_WHITE);

  spr.drawFastHLine(cx - 20, cy + 10, 12, TFT_DARKGREY);
  spr.drawFastHLine(cx + 8, cy + 13, 17, TFT_DARKGREY);
}

#endif
} // namespace

bool anomalyActive()
{
#if RH_ANOMALY_TEASER_ENABLED
  return s_active;
#else
  return false;
#endif
}

void anomalyNotifyUserActivity(uint32_t nowMs)
{
#if RH_ANOMALY_TEASER_ENABLED
  s_recentActivityUntilMs = nowMs + 45000UL;
  s_earliestActivityAnomalyMs = nowMs + 2500UL;
#else
  (void)nowMs;
#endif
}

void anomalyRequestForceAfterReturn()
{
#if RH_ANOMALY_TEASER_ENABLED
  const uint32_t now = millis();

  s_forcePending = true;
  s_forcePendingUntilMs = now + 10000UL;

  // Delay before anomaly can fire (feels more natural)
  s_earliestActivityAnomalyMs = now + 3000UL;

  Serial.println("[ANOMALY] force pending (delayed)");
#else
  Serial.println("[ANOMALY] disabled");
#endif
}

bool anomalyForceTrigger()
{
#if RH_ANOMALY_TEASER_ENABLED
  if (!hardSafetyAllowsAnomaly())
  {
    Serial.println("[ANOMALY] force blocked by safety gate");
    return false;
  }

  triggerAnomalyEvent(millis());
  return true;
#else
  Serial.println("[ANOMALY] disabled");
  return false;
#endif
}

void anomalyTick(uint32_t nowMs)
{
#if RH_ANOMALY_TEASER_ENABLED
  if (s_active)
  {
    if ((int32_t)(nowMs - s_untilMs) >= 0)
    {
      s_active = false;
      requestUIRedraw();
      return;
    }

    ++s_frame;
    requestUIRedraw();
    return;
  }

  if (s_forcePending)
  {
    // Wait a short delay after console return so it doesn't feel immediate
    if ((int32_t)(nowMs - s_earliestActivityAnomalyMs) < 0)
      return;
  
    if ((int32_t)(nowMs - s_forcePendingUntilMs) >= 0)
    {
      s_forcePending = false;
      Serial.println("[ANOMALY] force expired");
    }
    else if (hardSafetyAllowsAnomaly())
    {
      s_forcePending = false;
      triggerAnomalyEvent(nowMs);
      return;
    }
  }

  if ((int32_t)(nowMs - s_nextCheckMs) < 0)
    return;

  s_nextCheckMs = nowMs + kCheckIntervalMs;

  if ((int32_t)(nowMs - s_cooldownUntilMs) < 0)
    return;

    if (!hardSafetyAllowsAnomaly())
    return;

  // Only roll shortly after real player activity, but never on the exact input moment.
  if ((int32_t)(nowMs - s_recentActivityUntilMs) >= 0)
    return;

  if ((int32_t)(nowMs - s_earliestActivityAnomalyMs) < 0)
    return;

  if (random(kBaseChanceDenom) != 0)
    return;

  triggerAnomalyEvent(nowMs);
#else
  (void)nowMs;
#endif
}

void anomalyDrawOverlay()
{
#if RH_ANOMALY_TEASER_ENABLED
  if (!s_active)
    return;

  if (!hardSafetyAllowsAnomaly())
  {
    s_active = false;
    requestUIRedraw();
    return;
  }

  switch (s_type)
  {
  case AnomalyType::Scanline:
    drawScanlineDistortion();
    break;
  case AnomalyType::Tear:
    drawFrameTear();
    break;
  case AnomalyType::AlienGlimpse:
    drawSignalIntrusionFrame();
    break;
  case AnomalyType::LogOnly:
    break;
  }
#endif
}