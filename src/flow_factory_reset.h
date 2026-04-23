#pragma once

#include <stdint.h>
#include <stdbool.h>

bool factoryResetConfirmActive();
void factoryResetCancelConfirm();

struct InputState;

// Immediate action
void doFactoryResetNow();

// Settings/System page hook (called from settings menu/page code)
// Return true if it consumed the input / changed UI.
bool factoryResetSystemSettingsHook(InputState& in, int cursor);

// Reset any factory-reset overlay UI state (called when entering/leaving Settings, etc.)
void factoryResetResetUiState();

// Hold button down to confirm
uint8_t factoryResetHoldProgress255();

// Factory reset confirmation overlay handler (called by ui_input_interceptors)
void uiFactoryResetHandle(InputState& in);