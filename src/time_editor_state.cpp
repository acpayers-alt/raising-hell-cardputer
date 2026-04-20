// time_editor_state.cpp
#include <time.h>
#include "time_editor_state.h"
#include "ui_defs.h"   // UIState, Tab
#include "settings_state.h"

bool     g_setTimeActive = false;
tm       g_setTimeTm = {};
uint8_t  g_setTimeField = 0;
bool     g_setTimeForceNoCancel = false;
