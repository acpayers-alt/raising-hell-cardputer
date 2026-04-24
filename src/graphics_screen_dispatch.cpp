#include "graphics_screen_dispatch.h"

#include "graphics.h"

#include "app_state.h"
#include "console.h"
#include "graphics_special_screens.h"
#include "graphics_tab_screens.h"
#include "graphics_wifi_screens.h"
#include "mg_pause_menu.h"
#include "time_state.h"
#include "ui_state_backup_pet_list.h"
#include "ui_state_import_pet_list.h"
#include "wifi_time.h"

// -----------------------------------------------------------------------------
// Forward declarations for draw helpers still owned by other modules
// -----------------------------------------------------------------------------

void drawPetScreen(bool redrawBg);
void drawFeedMenu();
void drawSleepMenu();
void drawInventoryMenu();
void drawShopScreen();

void drawDeathScreen();
void drawBurialScreen();
void drawDeathTransitionScreen(bool redrawBg);
void drawMiniGameScreen();

void drawWifiSetupScreen();
void drawWifiConnectWaitScreen();

void drawSetTimeScreen();
void drawClockModeScreen(bool redrawBg);

void drawImportPetListScreen(bool redrawBg);
void drawBackupPetListScreen(bool redrawBg);

void drawChoosePetScreen(bool redrawBg);
void drawNamePetScreen(bool redrawBg);
void drawHatchingScreen(bool redrawBg);
void drawEvolutionScreen();

void drawControlsHelpScreen();
void drawWhatsNewScreen();

void drawBootWifiPromptScreen();
void drawBootWifiWaitScreen(bool connected, int rssi);
void drawBootWifiImportedScreen();
void drawBootAssetWifiRequiredScreen();
void drawBootTimezonePickScreen();
void drawBootNtpWaitScreen(bool wifiConnected, bool timeSynced);

void drawTitleMenuScreen(bool redrawBg);
void drawSettingsMenu();

bool wifiIsConnected();
int wifiRssi();
bool timeIsSynced();

static void drawTabDrivenScreen(bool redrawBg)
{
  switch (g_app.currentTab)
  {
  case Tab::TAB_PET:
    drawPetScreen(redrawBg);
    break;
  case Tab::TAB_STATS:
    drawStatsTab(redrawBg);
    break;
  case Tab::TAB_FEED:
    drawFeedMenu();
    break;
  case Tab::TAB_PLAY:
    drawPlayTab(redrawBg);
    break;
  case Tab::TAB_SLEEP:
    drawSleepMenu();
    break;
  case Tab::TAB_INV:
    drawInventoryMenu();
    break;
  case Tab::TAB_SHOP:
    drawShopScreen();
    break;
  default:
    drawPetScreen(redrawBg);
    break;
  }
}

bool uiIsBootWifiOnboardingState(UIState s)
{
  switch (s)
  {
  case UIState::BOOT_WIFI_PROMPT:
  case UIState::BOOT_WIFI_IMPORTED:
  case UIState::BOOT_WIFI_WAIT:
  case UIState::BOOT_TZ_PICK:
  case UIState::BOOT_NTP_WAIT:
  case UIState::BOOT_ASSET_WIFI_REQUIRED:
  case UIState::WIFI_SETUP:
    return true;
  default:
    return false;
  }
}

bool uiStateBlocksOverlays(UIState s)
{
  switch (s)
  {
  case UIState::DEATH:
  case UIState::DEATH_TRANSITION:
  case UIState::BURIAL_SCREEN:
  case UIState::PET_SLEEPING:
  case UIState::MINI_GAME:
  case UIState::WIFI_SETUP:
  case UIState::WIFI_CONNECT_WAIT:
  case UIState::SET_TIME:
  case UIState::CHOOSE_PET:
  case UIState::NAME_PET:
  case UIState::EVOLUTION:
  case UIState::CLOCK_MODE:
    return true;
  default:
    return false;
  }
}

void drawCurrentScreen(bool redrawBg)
{
  switch (g_app.uiState)
  {
  case UIState::DEATH:
    (void)redrawBg;
    drawDeathScreen();
    return;

  case UIState::BURIAL_SCREEN:
    drawBurialScreen();
    return;

  case UIState::DEATH_TRANSITION:
    drawDeathTransitionScreen(redrawBg);
    return;

  case UIState::PET_SLEEPING:
    drawSleepScreen();
    return;

  case UIState::MINI_GAME:
    drawMiniGameScreen();
    return;

  case UIState::WIFI_SETUP:
    drawWifiSetupScreen();
    return;

  case UIState::WIFI_CONNECT_WAIT:
    drawWifiConnectWaitScreen();
    return;

  case UIState::SET_TIME:
    drawSetTimeScreen();
    return;

  case UIState::TITLE_MENU:
    drawTitleMenuScreen(redrawBg);
    return;

  case UIState::CLOCK_MODE:
    drawClockModeScreen(redrawBg);
    return;

  case UIState::IMPORT_PET_LIST:
    drawImportPetListScreen(redrawBg);
    return;

  case UIState::BACKUP_PET_LIST:
    drawBackupPetListScreen(redrawBg);
    return;

  case UIState::CHOOSE_PET:
    drawChoosePetScreen(redrawBg);
    return;

  case UIState::NAME_PET:
    drawNamePetScreen(redrawBg);
    return;

  case UIState::HATCHING:
    drawHatchingScreen(redrawBg);
    return;

  case UIState::EVOLUTION:
    drawEvolutionScreen();
    return;

  case UIState::CONTROLS_HELP:
    drawControlsHelpScreen();
    return;

  case UIState::WHATS_NEW:
    drawWhatsNewScreen();
    return;

  case UIState::BOOT_WIFI_PROMPT:
    drawBootWifiPromptScreen();
    return;

  case UIState::BOOT_WIFI_WAIT:
    drawBootWifiWaitScreen(wifiIsConnected(), wifiRssi());
    return;

  case UIState::BOOT_WIFI_IMPORTED:
    drawBootWifiImportedScreen();
    return;

  case UIState::BOOT_ASSET_WIFI_REQUIRED:
    drawBootAssetWifiRequiredScreen();
    return;

  case UIState::BOOT_TZ_PICK:
    drawBootTimezonePickScreen();
    return;

  case UIState::BOOT_NTP_WAIT:
    drawBootNtpWaitScreen(wifiIsConnected(), timeIsSynced());
    return;

  case UIState::MG_PAUSE:
    drawMiniGameScreen();
    mgDrawPauseOverlay();
    return;

  case UIState::SETTINGS:
    drawSettingsMenu();
    break;

  case UIState::SLEEP_MENU:
    drawSleepMenu();
    break;

  case UIState::INVENTORY:
    drawInventoryMenu();
    break;

  case UIState::SHOP:
    drawShopScreen();
    break;

  case UIState::CONSOLE:
    drawConsoleScreen();
    return;

  case UIState::POWER_MENU:
    break;

  case UIState::BOOT:
    drawBootSplash();
    return;

  case UIState::PET_SCREEN:
  default:
    drawTabDrivenScreen(redrawBg);
    break;
  }

  if (!uiStateBlocksOverlays(g_app.uiState) && consoleIsOpen())
  {
    drawConsoleScreen();
  }
}