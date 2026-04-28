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
#include "ui_state_choose_pet.h"

namespace UiSettingsPages
{

static bool s_newPetConfirmActive = false;
static int s_newPetConfirmIndex = 0; // 0 = YES (store first), 1 = NO

bool GameNewPetConfirmActive() { return s_newPetConfirmActive; }

int GameNewPetConfirmIndex() { return s_newPetConfirmIndex; }

void ShowGameNewPetConfirm()
{
  s_newPetConfirmActive = true;
  s_newPetConfirmIndex = 0;
}

void HideGameNewPetConfirm() { s_newPetConfirmActive = false; }

void SetGameNewPetConfirmIndex(int idx) { s_newPetConfirmIndex = (idx != 0) ? 1 : 0; }

bool HandleGameNewPetConfirm(InputState &input)
{
  // This file no longer owns Game menu navigation/actions.
  // The authoritative Game menu handler is UiSettingsMenu in ui_settings_menu.cpp.
  // This legacy page now only owns the New Pet confirmation overlay.
  if (!s_newPetConfirmActive)
    return false;

  if (input.leftOnce || input.upOnce)
  {
    s_newPetConfirmIndex = 0;
    playBeep();
    requestUIRedraw();
    clearInputLatch();
    return true;
  }

  if (input.rightOnce || input.downOnce)
  {
    s_newPetConfirmIndex = 1;
    playBeep();
    requestUIRedraw();
    clearInputLatch();
    return true;
  }

  if (input.menuOnce || input.escOnce)
  {
    s_newPetConfirmActive = false;
    requestUIRedraw();
    clearInputLatch();
    return true;
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
        return true;
      }
    }

    s_newPetConfirmActive = false;
    playBeep();
    uiActionEnterState(UIState::CHOOSE_PET, Tab::TAB_PET, true);
    uiChoosePetOnEnter(input);
    requestFullUIRedraw();
    requestUIRedraw();
    clearInputLatch();
    return true;
  }

  return true;
}

} // namespace UiSettingsPages