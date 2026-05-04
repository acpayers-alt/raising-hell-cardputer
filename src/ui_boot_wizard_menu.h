#pragma once

#include <stdint.h>

struct InputState;

namespace UiBootWizardMenu
{

// BOOT_WIFI_PROMPT: generic setup prompt for WiFi/time recovery.
// Returns true if it handled the input.
bool HandleWifiPrompt(InputState &in);

// BOOT_TZ_PICK: choose timezone, commit on ENTER, allow ESC to skip.
// Returns true if it handled the input.
bool HandleTimezonePick(InputState &in);

// Optional: call when entering BOOT_WIFI_PROMPT to ensure consistent default.
void ResetWifiPromptChoice();

} // namespace UiBootWizardMenu