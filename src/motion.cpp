#include "motion.h"

#include <Arduino.h>
#include "M5Cardputer.h"
#include <math.h>

// -----------------------------------------------------------------------------
// Cardputer-Adv IMU backend (M5Unified)
// - Docs: use M5.Imu.update() and M5.Imu.getImuData()
// -----------------------------------------------------------------------------

bool motionAvailable = false;

static bool g_inited = false;

// Shake detector state
static float    g_lpMag = 1.0f;          // low-pass magnitude (g)
static uint32_t g_windowStartMs = 0;
static int      g_shakeHits = 0;
static uint32_t g_cooldownUntilMs = 0;

// Tunables (tweak if needed)
static constexpr float    SHAKE_DELTA_G     = 0.55f;  // |mag - lpMag| threshold (g)
static constexpr int      SHAKE_HITS_N      = 3;      // hits needed
static constexpr uint32_t SHAKE_WINDOW_MS   = 650;    // hits must occur within this window
static constexpr uint32_t SHAKE_COOLDOWN_MS = 1200;   // ignore shakes after trigger

void motionResetShakeDetector(uint32_t cooldownMs)
{
  const uint32_t now = millis();

  g_lpMag = 1.0f;
  g_windowStartMs = 0;
  g_shakeHits = 0;
  g_cooldownUntilMs = now + cooldownMs;
}

void initMotion() {
  if (g_inited) return;
  g_inited = true;

  // M5Cardputer.begin(cfg, ...) should already prep M5Unified,
  // but calling begin() here is safe and avoids "IMU never enabled" cases.
  motionAvailable = M5.Imu.begin();

  g_lpMag = 1.0f;
  g_windowStartMs = 0;
  g_shakeHits = 0;
  g_cooldownUntilMs = 0;
}

MotionData readMotion() {
  MotionData out{};
  if (!motionAvailable) return out;

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

bool motionShakeDetected() {
  if (!motionAvailable) return false;

  const uint32_t now = millis();
  if (now < g_cooldownUntilMs) return false;

  M5.Imu.update();
  m5::imu_data_t d = M5.Imu.getImuData();

  const float ax = d.accel.x;
  const float ay = d.accel.y;
  const float az = d.accel.z;

  const float mag = sqrtf(ax*ax + ay*ay + az*az);

  // Low-pass the magnitude so gravity + slow movement doesn't trigger.
  g_lpMag = (g_lpMag * 0.92f) + (mag * 0.08f);

  const float delta = fabsf(mag - g_lpMag);

  // Maintain a short window of "hits"
  if (g_windowStartMs == 0 || (now - g_windowStartMs) > SHAKE_WINDOW_MS) {
    g_windowStartMs = now;
    g_shakeHits = 0;
  }

  if (delta >= SHAKE_DELTA_G) {
    g_shakeHits++;
  }

  if (g_shakeHits >= SHAKE_HITS_N) {
    g_windowStartMs = 0;
    g_shakeHits = 0;
    g_cooldownUntilMs = now + SHAKE_COOLDOWN_MS;
    return true;
  }

  return false;
}
