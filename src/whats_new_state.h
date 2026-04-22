#pragma once

#include <stdint.h>
#include "ui_defs.h"

extern uint8_t g_whatsNewSeen;

void whatsNewBegin(UIState returnState, Tab returnTab);
void whatsNewDismiss();

void whatsNewOnEnter();
bool whatsNewDismissAllowed();