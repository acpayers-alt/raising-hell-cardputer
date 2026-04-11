#pragma once
#include <Arduino.h>
#include "led_status.h"
#include "graphics.h"

#define LED_STATUS_ENABLED 1

// True if the LED subsystem is compiled/configured for this build
bool ledIsSupported();

// Init LED hardware (safe to call multiple times)
void ledInit();

// Set LED color (0..255 each channel)
void ledSetRGB(uint8_t r, uint8_t g, uint8_t b);

// Turn LED off
void ledOff();

// led_status.h (or wherever LedPetMode lives)
enum LedPetMode : uint8_t {
  LED_PET_OFF = 0,
  LED_PET_OK,
  LED_PET_HUNGRY,
  LED_PET_TIRED,
  LED_PET_SLEEPING,
  LED_PET_DANGER,
  LED_PET_BORED,
  LED_PET_MAD
};

bool isPetSleepingNow();
LedPetMode computeLedMode();

// Call every loop with current mode
void ledUpdatePetStatus(LedPetMode mode);

// led_status.h
void ledSetScreenOff(bool isOff);

bool ledInputLockActive();