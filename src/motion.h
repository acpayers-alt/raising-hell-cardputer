#ifndef MOTION_H
#define MOTION_H

#include <Arduino.h>
#include <stdint.h>

extern bool motionAvailable;

bool motionShakeDetected();

struct MotionData {
  int ax, ay, az;  // accel in milli-g (approx)
  int gx, gy, gz;  // gyro in centi-deg/sec (approx)
};

// Motion system interface
void initMotion();
MotionData readMotion();

// Returns true once when a "shake" gesture is detected.
// Intended to be polled every loop; includes an internal cooldown.
bool motionShakeDetected();

// runtime flag: true when IMU was detected and initialized
extern bool motionAvailable;

void motionResetShakeDetector(uint32_t cooldownMs = 0);

uint8_t motionGetShakeSensitivity();
void motionSetShakeSensitivity(uint8_t sel);
const char *motionShakeSensitivityToText(uint8_t sel);

#endif
