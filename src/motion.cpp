#include "motion.h"

#include "M5Cardputer.h"
#include <Arduino.h>
#include <math.h>

// -----------------------------------------------------------------------------
// Cardputer-Adv IMU backend (M5Unified)
// - Docs: use M5.Imu.update() and M5.Imu.getImuData()
// -----------------------------------------------------------------------------

bool motionAvailable = false;

static bool g_inited = false;

// Shake detector state
static float g_lpMag = 1.0f; // low-pass magnitude (g)
static uint32_t g_windowStartMs = 0;
static int g_shakeHits = 0;
static uint32_t g_cooldownUntilMs = 0;

// Tunables (tweak if needed)
static uint8_t g_shakeSensitivitySel = 1;   // 0=Off, 1=Low, 2=Medium, 3=High
static constexpr uint32_t SHAKE_COOLDOWN_MS = 1200;

static float shakeDeltaForSel(uint8_t sel)
{
  switch (sel)
  {
  case 0: return 999.0f; // Off
  case 1: return 1.35f;  // Low  = hardest to trigger
  case 2: return 0.90f;  // Medium
  case 3: return 0.45f;  // High = easiest to trigger
  default: return 1.35f;
  }
}

static int shakeHitsForSel(uint8_t sel)
{
  switch (sel)
  {
  case 0: return 999; // Off
  case 1: return 6;   // Low
  case 2: return 4;   // Medium
  case 3: return 2;   // High
  default: return 6;
  }
}

static uint32_t shakeWindowForSel(uint8_t sel)
{
  switch (sel)
  {
  case 0: return 1;    // Off
  case 1: return 450;  // Low = tighter window, harder
  case 2: return 650;  // Medium
  case 3: return 900;  // High = easier
  default: return 450;
  }
}

uint8_t motionGetShakeSensitivity()
{
  return g_shakeSensitivitySel;
}

void motionSetShakeSensitivity(uint8_t sel)
{
  if (sel > 3)
    sel = 1;
  g_shakeSensitivitySel = sel;
}

const char *motionShakeSensitivityToText(uint8_t sel)
{
  switch (sel)
  {
  case 0: return "Off";
  case 1: return "Low";
  case 2: return "Medium";
  case 3: return "High";
  default: return "Low";
  }
}

void motionResetShakeDetector(uint32_t cooldownMs)
{
  const uint32_t now = millis();

  g_lpMag = 1.0f;
  g_windowStartMs = 0;
  g_shakeHits = 0;
  g_cooldownUntilMs = now + cooldownMs;
}

void initMotion()
{
  if (g_inited)
    return;
  g_inited = true;

  // M5Cardputer.begin(cfg, ...) should already prep M5Unified,
  // but calling begin() here is safe and avoids "IMU never enabled" cases.
  motionAvailable = M5.Imu.begin();

  g_lpMag = 1.0f;
  g_windowStartMs = 0;
  g_shakeHits = 0;
  g_cooldownUntilMs = 0;
}

MotionData readMotion()
{
  MotionData out{};
  if (!motionAvailable)
    return out;

  M5.Imu.update();
  m5::imu_data_t d = M5.Imu.getImuData();

  // accel is typically in "g"
  out.ax = (int)lroundf(d.accel.x * 1000.0f);
  out.ay = (int)lroundf(d.accel.y * 1000.0f);
  out.az = (int)lroundf(d.accel.z * 1000.0f);

  // gyro is typically in "dps"
  out.gx = (int)lroundf(d.gyro.x * 100.0f);
  out.gy = (int)lroundf(d.gyro.y * 100.0f);
  out.gz = (int)lroundf(d.gyro.z * 100.0f);

  return out;
}

bool motionShakeDetected()
{
  if (!motionAvailable)
    return false;

  const uint32_t now = millis();
  if (now < g_cooldownUntilMs)
    return false;

    if (g_shakeSensitivitySel == 0)
    return false;

      M5.Imu.update();
  m5::imu_data_t d = M5.Imu.getImuData();

  const float ax = d.accel.x;
  const float ay = d.accel.y;
  const float az = d.accel.z;

  const float mag = sqrtf(ax * ax + ay * ay + az * az);

  // Low-pass the magnitude so gravity + slow movement doesn't trigger.
  g_lpMag = (g_lpMag * 0.92f) + (mag * 0.08f);

  const float delta = fabsf(mag - g_lpMag);

  // Maintain a short window of "hits"
  const float shakeDeltaG = shakeDeltaForSel(g_shakeSensitivitySel);
  const int shakeHitsN = shakeHitsForSel(g_shakeSensitivitySel);
  const uint32_t shakeWindowMs = shakeWindowForSel(g_shakeSensitivitySel);
  
  if (g_windowStartMs == 0 || (now - g_windowStartMs) > shakeWindowMs)
  {
    g_windowStartMs = now;
    g_shakeHits = 0;
  }

  if (delta >= shakeDeltaG)
  {
    g_shakeHits++;
  }

  if (g_shakeHits >= shakeHitsN)
  {
    g_windowStartMs = 0;
    g_shakeHits = 0;
    g_cooldownUntilMs = now + SHAKE_COOLDOWN_MS;
    return true;
  }

  return false;
}
