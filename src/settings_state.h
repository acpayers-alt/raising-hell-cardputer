// settings_state.h
#pragma once
#include "ui_defs.h"
#include "ui_state_utils.h" 

#include <stdint.h>
#include <time.h>

#include "pet.h"

#include "user_toggles_state.h"
#include "settings_nav_state.h"
#include "runtime_flags_state.h"

#include "time_editor_state.h" // canonical g_setTime* declarations

// Settings controls
extern uint8_t autoScreenTimeoutSel;

extern bool petDeathEnabled;
