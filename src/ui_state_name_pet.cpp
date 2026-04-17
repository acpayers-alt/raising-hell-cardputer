#include "ui_state_name_pet.h"

// ─────────────────────────────────────
// Standard / C
// ─────────────────────────────────────
#include <string.h>

// ─────────────────────────────────────
// Arduino / platform
// ─────────────────────────────────────
#include <Arduino.h>

// ─────────────────────────────────────
// Core app state & models
// ─────────────────────────────────────
#include "app_state.h"
#include "pet.h"
#include "save_manager.h"

// ─────────────────────────────────────
// Input system
// ─────────────────────────────────────
#include "input.h"
#include "name_entry_state.h"
#include "ui_input_common.h"

// ─────────────────────────────────────
// UI runtime / actions
// ─────────────────────────────────────
#include "ui_actions.h"
#include "ui_runtime.h"

// ─────────────────────────────────────
// UI states / flows
// ─────────────────────────────────────
#include "new_pet_flow_state.h"
#include "settings_flow_state.h"
#include "ui_new_pet_flow.h"
#include "ui_state_settings.h"

// ─────────────────────────────────────
// Rendering / feedback
// ─────────────────────────────────────
#include "graphics.h"
#include "sound.h"

void uiNamePetHandle(InputState &in)
{
  // One-time cleanup after entering the screen
  if (g_namePetJustOpened)
  {
    g_namePetJustOpened = false;
    uiDrainKb(in);
    inputForceClear();
    clearInputLatch();
  }

  // Allow cancel only when this is a normal rename flow.
  // New-pet naming remains mandatory.
  if (g_namePetRenameMode && (in.escOnce || in.menuOnce || in.hotSettings))
  {
    inputSetTextCapture(false);
    g_textCaptureMode = false;

    g_namePetRenameMode = false;
    g_namePetJustOpened = false;

    uiActionSwallowAll(in);
    uiDrainKb(in);
    inputForceClear();
    clearInputLatch();

    playBeep();
    closeSettingsAndReturn(in);
    requestFullUIRedraw();
    requestUIRedraw();
    return;
  }

  // New-pet naming: ESC is intentionally inert
  if (!g_namePetRenameMode && in.escOnce)
  {
    playBeep();
    clearInputLatch();
    return;
  }

  bool changed = false;

  while (in.kbHasEvent())
  {
    KeyEvent ev = in.kbPop();
    const uint8_t c = ev.code;

    // Enter → finalize
    if (c == '\n' || c == RH_KEY_ENTER)
    {
      if (g_pendingPetName[0] == '\0')
      {
        playBeep();
        continue;
      }

      if (g_namePetRenameMode)
      {
        inputSetTextCapture(false);
        g_textCaptureMode = false;

        pet.setName(g_pendingPetName);
        saveManagerMarkDirty();
        Serial.printf("[PET] named '%s'\n", pet.getName());

        g_namePetRenameMode = false;
        g_namePetJustOpened = false;

        while (in.kbHasEvent())
          (void)in.kbPop();
        inputForceClear();
        clearInputLatch();

        playBeep();
        closeSettingsAndReturn(in);
        requestFullUIRedraw();
        requestUIRedraw();
        invalidateBackgroundCache();
        return;
      }

      finalizeNewPetFromName(in);
      return;
    }

    // Backspace
    if (c == '\b' || c == RH_KEY_BACKSPACE)
    {
      size_t n = strnlen(g_pendingPetName, PET_NAME_MAX);
      if (n > 0)
      {
        g_pendingPetName[n - 1] = '\0';
        changed = true;
      }
      continue;
    }

    // Printable ASCII only
    if (c < 32 || c > 126)
      continue;

    size_t n = strnlen(g_pendingPetName, PET_NAME_MAX);
    if (n >= PET_NAME_MAX)
    {
      playBeep();
      continue;
    }

    g_pendingPetName[n] = (char)c;
    g_pendingPetName[n + 1] = '\0';
    changed = true;
  }

  if (changed)
    requestUIRedraw();
  clearInputLatch();
}
