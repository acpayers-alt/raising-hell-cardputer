#include "ui_input_utils.h"
#include "ui_settings_pages.h"

#include "app_state.h"
#include "game_options_state.h"
#include "graphics.h"
#include "input.h"
#include "led_status.h"
#include "save_manager.h"
#include "sdcard.h"
#include "settings_flow_state.h"
#include "sound.h"
#include "ui_actions.h"
#include "ui_runtime.h"

namespace UiSettingsPages
{

static bool s_newPetConfirmActive = false;
static int s_newPetConfirmIndex = 0; // 0 = YES (store first), 1 = NO

bool GameNewPetConfirmActive()
{
  return s_newPetConfirmActive;
}

int GameNewPetConfirmIndex()
{
  return s_newPetConfirmIndex;
}

void ShowGameNewPetConfirm()
{
  s_newPetConfirmActive = true;
  s_newPetConfirmIndex = 0;
}

void HideGameNewPetConfirm()
{
  s_newPetConfirmActive = false;
}

void SetGameNewPetConfirmIndex(int idx)
{
  s_newPetConfirmIndex = (idx != 0) ? 1 : 0;
}

void Handle_GAME(InputState &input, int move)
{
  (void)move;

  // ---------------------------------------------------------------------------
  // New Pet confirm overlay input
  // ---------------------------------------------------------------------------
  if (s_newPetConfirmActive)
  {
    if (input.leftOnce || input.upOnce)
    {
      s_newPetConfirmIndex = 0;
      playBeep();
      requestUIRedraw();
      clearInputLatch();
      return;
    }

    if (input.rightOnce || input.downOnce)
    {
      s_newPetConfirmIndex = 1;
      playBeep();
      requestUIRedraw();
      clearInputLatch();
      return;
    }

    if (input.menuOnce || input.escOnce)
    {
      s_newPetConfirmActive = false;
      requestUIRedraw();
      clearInputLatch();
      return;
    }

    if (uiIsSelect(input))
    {
      const bool storeFirst = (s_newPetConfirmIndex == 0);

      if (storeFirst)
      {
        char parkedPath[128];
        if (!saveManagerExportCurrentBubJson(parkedPath, sizeof(parkedPath)))
        {
          playBeep();
          ui_showMessage("Store failed");
          s_newPetConfirmActive = false;
          requestUIRedraw();
          clearInputLatch();
          return;
        }
      }

      s_newPetConfirmActive = false;
      playBeep();
      saveManagerStartFreshPetFlow();
      clearInputLatch();
      return;
    }

    return;
  }

  const int totalItems = 7;

  int mv = input.encoderDelta;
  if (input.upOnce)
    mv = -1;
  if (input.downOnce)
    mv = 1;

  if (mv != 0)
  {
    g_app.gameOptionsIndex += mv;
    if (g_app.gameOptionsIndex < 0)
      g_app.gameOptionsIndex = totalItems - 1;
    if (g_app.gameOptionsIndex >= totalItems)
      g_app.gameOptionsIndex = 0;

    requestUIRedraw();
    playBeep();
    clearInputLatch();
    return;
  }

  if (uiIsSelect(input))
  {
    // 0 = Rename Pet
    if (g_app.gameOptionsIndex == 0)
    {
      playBeep();
      clearInputLatch();
      return;
    }

    // 1 = Store Pet
    if (g_app.gameOptionsIndex == 1)
    {
      char path[128];
      if (!saveManagerExportCurrentBubJson(path, sizeof(path)))
        ui_showMessage("Store failed");
      else
        ui_showMessage("Pet stored");

      requestUIRedraw();
      playBeep();
      clearInputLatch();
      return;
    }

    // 2 = Stored Pets
    if (g_app.gameOptionsIndex == 2)
    {
      uiActionEnterState(UIState::IMPORT_PET_LIST, Tab::TAB_PET, true);
      playBeep();
      clearInputLatch();
      return;
    }

    // 3 = New Pet
    if (g_app.gameOptionsIndex == 3)
    {
      s_newPetConfirmActive = true;
      s_newPetConfirmIndex = 0;
      requestUIRedraw();
      playBeep();
      clearInputLatch();
      return;
    }

    // 4 = Decay Mode
    if (g_app.gameOptionsIndex == 4)
    {
      g_app.decayModeIndex = (int)saveManagerGetDecayMode();
      g_settingsFlow.settingsReturnPage = SettingsPage::GAME;
      g_settingsFlow.settingsPage = SettingsPage::DECAY_MODE;
      requestUIRedraw();
      playBeep();
      clearInputLatch();
      return;
    }

    // 5 = Pet Death
    if (g_app.gameOptionsIndex == 5)
    {
      petDeathEnabled = !petDeathEnabled;
      saveSettingsToSD();
      saveManagerMarkDirty();
      requestUIRedraw();
      playBeep();
      clearInputLatch();
      return;
    }

    // 6 = LED Alerts
    if (g_app.gameOptionsIndex == 6)
    {
      ledAlertsEnabled = !ledAlertsEnabled;
#if LED_STATUS_ENABLED
      ledUpdatePetStatus(LED_PET_OFF);
#endif
      saveSettingsToSD();
      saveManagerMarkDirty();
      requestUIRedraw();
      playBeep();
      clearInputLatch();
      return;
    }
  }
}

} // namespace UiSettingsPages