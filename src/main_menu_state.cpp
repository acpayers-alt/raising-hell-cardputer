#include "main_menu_state.h"
#include "app_state.h"
#include "game_state.h"
#include "input.h"
#include "sound.h"
#include "state_manager.h"
#include "ui_actions.h"
#include "ui_state_settings.h"

void MainMenuState::enter()
{
  Serial.println("Entering MainMenuState...");
  menuOption = 0;
}

void MainMenuState::exit() { Serial.println("Exiting MainMenuState..."); }

void MainMenuState::update()
{
  InputState input = readInput();

  // Navigate options
  if (input.upOnce || input.leftOnce)
  {
    if (menuOption > 0)
    {
      menuOption--;
      playBeep();
    }
  }
  if (input.downOnce || input.rightOnce)
  {
    if (menuOption < 1)
    {
      menuOption++;
      playBeep();
    }
  }

  // Confirm selection
  if (input.selectOnce || input.encoderPressOnce)
  {
    if (menuOption == 0)
    {
      Serial.println("Starting game...");
      state_manager.setStateOwned(new GameState());
    }
    else
    {
      Serial.println("Opening settings...");
      openSettingsWithReturn(UIState::PET_SCREEN, Tab::TAB_PET, SettingsPage::GAME);    }
    return;
  }
}