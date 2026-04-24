#pragma once
#include <stdint.h>
#include <time.h>
#include "settings_state.h"

// Set-time flow/editor state

extern bool     g_setTimeActive;
extern tm       g_setTimeTm;
extern uint8_t  g_setTimeField;
extern bool     g_setTimeForceNoCancel;