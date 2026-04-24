#pragma once

#include <stdint.h>
#include "ui_defs.h"

extern uint8_t g_controlsHelpSeen;

void controlsHelpBegin(UIState returnState, Tab returnTab);
void controlsHelpDismiss();

void controlsHelpOnEnter();
bool controlsHelpDismissAllowed();