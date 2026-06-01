// ---------------------------------------------------------------------------
// Mini-game implementation toggle
//
// Default: implementation lives in mini_games.cpp.
// If you ever move implementation back to the pause-menu module, set
// RH_MINIGAMES_IMPL_IN_PAUSE_MENU=1 (e.g. via build_flags.h) and this file
// becomes an intentional stub to avoid duplicate symbols.
// ---------------------------------------------------------------------------

#ifndef RH_MINIGAMES_IMPL_IN_PAUSE_MENU
#define RH_MINIGAMES_IMPL_IN_PAUSE_MENU 0
#endif

#if RH_MINIGAMES_IMPL_IN_PAUSE_MENU

#else

#include "graphics_sd_draw.h"

#include "esp_heap_caps.h"
#include <stdint.h>

#include "mini_game_assets.h"
#include "mini_game_runtime.h"
#include "mini_games.h"
#include "mini_games_internal.h"

#include <Arduino.h>
#include <FS.h>
#include <SD.h>

#include "app_state.h" // g_app
#include "display.h"
#include "graphics.h"   // spr, screenW/screenH, invalidateBackgroundCache()
#include "sdcard.h"     // g_sdReady
#include "sound.h"      // soundFlap/soundConfirm/soundError/playBeep
#include "ui_defs.h"    // UIState
#include "ui_runtime.h" // requestUIRedraw()
#include <string.h>     // strrchr
#include <strings.h>    // strcasecmp (ESP32 toolchain usually has this)

#include "currency.h"
#include "input.h"
#include "inventory.h"     // ItemType
#include "mg_pause_core.h" // mgPause* + MGPAUSE_* constants
#include "mg_pause_menu.h"
#include "mini_game_return_ui.h" // miniGameSetReturnUi / miniGameGetReturnUiOrDefault / miniGameClearReturnUi
#include "pet.h"
#include "save_manager.h"
#include "ui_actions.h"

static inline void exitMiniGameToReturnUi(bool beginLockout = true);

static void releaseMiniGameAssetsFor(MiniGame game)
{
  switch (game)
  {
  case MiniGame::FLAPPY_FIREBALL:
    freeFlappyPipeSprites();
    freeFlappyFireballSprites();
    freeImpWaveSprites();
    freeFlappyBgCache();
    break;

  case MiniGame::CROSSY_ROAD:
    freeCrossyZoneSprites();
    freeCrossyActorSprites();
    break;

  case MiniGame::INFERNAL_DODGER:
    freeDodgerSprites();
    break;

  case MiniGame::ABDUCTION_BEAM:
    break;

  case MiniGame::RESURRECTION:
    freeResRunSprites();
    break;

  case MiniGame::SIGNAL_RECOVERY:
    freeSignalRecoverySprites();
    break;

  case MiniGame::VOID_RITUAL:
    freeVoidRitualSprites();
    break;

  default:
    break;
  }
}

static inline void exitMiniGameToReturnUi(bool beginLockout)
{
  releaseMiniGameAssetsFor(currentMiniGame);
  mgmem::endSession();
  miniGameExitToReturnUi(beginLockout);
}

void miniGameDrawRewardModal(int gW, int gH)
{
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(CC_DATUM);

  // ---------------------------------------------------------
  // BIG WIN / LOSE HEADER
  // ---------------------------------------------------------
  spr.setTextColor(playerWon ? TFT_GREEN : TFT_RED, TFT_BLACK);
  spr.drawCentreString(playerWon ? "YOU WIN!" : "YOU LOSE!", gW / 2, gH / 2 - 36,
                       4 // big font
  );

  // ---------------------------------------------------------
  // Reward body text (supports 1 or 2 lines)
  // ---------------------------------------------------------
  const char *msg = mgRewardMessage();
  const char *nl = strchr(msg, '\n');

  if (nl)
  {
    char line1[64];
    size_t len = (size_t)(nl - msg);
    if (len > sizeof(line1) - 1)
      len = sizeof(line1) - 1;

    memcpy(line1, msg, len);
    line1[len] = 0;

    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    spr.drawCentreString(line1, gW / 2, gH / 2 - 4, 2);

    spr.setTextColor(TFT_YELLOW, TFT_BLACK);
    spr.drawCentreString(nl + 1, gW / 2, gH / 2 + 16, 2);
  }
  else
  {
    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    spr.drawCentreString(msg, gW / 2, gH / 2, 2);
  }

  // ---------------------------------------------------------
  // Footer
  // ---------------------------------------------------------
  spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  spr.drawCentreString("Press ENTER", gW / 2, gH / 2 + 40, 2);
}

// -----------------------------------------------------------------------------
// Mini-game global state
// -----------------------------------------------------------------------------

// Simple mini-game state
bool s_resultShown = false;

// SHARED MINI GAME INTRO INTERRUPT
bool miniGameIsShowingIntro()
{
  switch (currentMiniGame)
  {
  case MiniGame::FLAPPY_FIREBALL:
    return s_flappyShowIntro;

  case MiniGame::CROSSY_ROAD:
    return crossyIsShowingIntro();

  case MiniGame::INFERNAL_DODGER:
    return dodgerIsShowingIntro();

  case MiniGame::RESURRECTION:
    return resRunIsShowingIntro();

  case MiniGame::SIGNAL_RECOVERY:
    return signalRecoveryIsShowingIntro();

  case MiniGame::VOID_RITUAL:
    return voidRitualIsShowingIntro();

  case MiniGame::ABDUCTION_BEAM:
    return abductionBeamIsShowingIntro();

  default:
    return false;
  }
}

void miniGameCancelFromIntro()
{
  bool refundEnergy = false;

  switch (currentMiniGame)
  {
  case MiniGame::FLAPPY_FIREBALL:
  case MiniGame::CROSSY_ROAD:
  case MiniGame::INFERNAL_DODGER:
  case MiniGame::ABDUCTION_BEAM:
    refundEnergy = true;
    break;

  case MiniGame::RESURRECTION:
  case MiniGame::SIGNAL_RECOVERY:
  case MiniGame::VOID_RITUAL:
  default:
    refundEnergy = false;
    break;
  }

  if (refundEnergy)
  {
    pet.energy = constrain(pet.energy + 10, 0, 100);
    saveManagerMarkDirty();
  }

  clearInputLatch();
  inputForceClear();
  mgPauseReset();
  exitMiniGameToReturnUi(true);
  requestFullUIRedraw();
}

#endif // RH_MINIGAMES_IMPL_IN_PAUSE_MENU