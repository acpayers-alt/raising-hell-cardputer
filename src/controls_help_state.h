// controls_help_state.h
#pragma once

#include <stdint.h>
#include "ui_defs.h"
#include "ui_state_utils.h"   
extern uint8_t g_controlsHelpSeen;

void controlsHelpBegin(UIState returnState, Tab returnTab);
void controlsHelpDismiss();

void controlsHelpOnEnter();
bool controlsHelpDismissAllowed();