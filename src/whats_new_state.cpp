#include "whats_new_state.h"

#include "app_state.h"
#include "flow_boot_wizard.h"
#include "graphics.h"
#include "input.h"
#include "return_target.h"
#include "save_manager.h"
#include "sdcard.h"
#include "ui_actions.h"

extern UIState g_bootWizardAfterOkState;
extern Tab g_bootWizardAfterOkTab;

uint8_t g_whatsNewSeen = 0;

static uint32_t s_whatsNewEnterMs = 0;

void whatsNewOnEnter()
{
  s_whatsNewEnterMs = millis();
  whatsNewResetScroll();
  inputForceClear();
  clearInputLatch();
}

bool whatsNewDismissAllowed() { return (uint32_t)(millis() - s_whatsNewEnterMs) >= 250; }

void whatsNewBegin(UIState returnState, Tab returnTab)
{
  uiPushReturnTarget(returnState, returnTab);
  uiActionEnterState(UIState::WHATS_NEW, returnTab, true);
  requestUIRedraw();
}

void whatsNewDismiss()
{
  if (!g_whatsNewSeen)
  {
    g_whatsNewSeen = 1;

    if (g_sdReady)
      saveSettingsToSD();
    else
      Serial.println("[WHATSNEW] dismissed but SD not ready; runtime flag only");
  }

  inputForceClear();
  clearInputLatch();

  const UIReturnTarget ret = uiGetReturnTarget();
  uiPopReturnTarget();

  if (ret.state == UIState::BOOT_WIFI_PROMPT)
  {
    bootWizardBegin(g_bootWizardAfterOkState, g_bootWizardAfterOkTab);
    requestUIRedraw();
    return;
  }

  uiActionEnterState(ret.state, ret.tab, true);
  requestUIRedraw();
}