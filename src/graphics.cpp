// -----------------------------------------------------------------------------
// Primary
// -----------------------------------------------------------------------------
#include "graphics.h"

// -----------------------------------------------------------------------------
// Core System / Platform
// -----------------------------------------------------------------------------
#include "app_state.h"
#include "boot_pipeline.h"
#include "display.h"
#include "display_dims_state.h"
#include "motion.h"
#include "sdcard.h"
#include "tft_compat.h"
#include <ctype.h>

// -----------------------------------------------------------------------------
// Arduino / Std Libs
// -----------------------------------------------------------------------------
#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <cstring>
#include <time.h>

// -----------------------------------------------------------------------------
// External Libraries
// -----------------------------------------------------------------------------
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <lgfx/v1/misc/DataWrapper.hpp>

// -----------------------------------------------------------------------------
// Core Runtime / Engine
// -----------------------------------------------------------------------------
#include "auto_screen.h"
#include "brightness_state.h"
#include "input.h"
#include "sound.h"
#include "ui_runtime.h"

// -----------------------------------------------------------------------------
// Game Systems
// -----------------------------------------------------------------------------
#include "inventory.h"
#include "pet.h"
#include "pet_age.h"
#include "save_manager.h"
#include "savegame.h"
#include "shop_items.h"
#include "timezone.h"
#include "wifi_time.h"

// -----------------------------------------------------------------------------
// Animation / Assets
// -----------------------------------------------------------------------------
#include "anim_clips.h"
#include "anim_engine.h"
#include "graphics_assets.h"

// -----------------------------------------------------------------------------
// UI / State System
// -----------------------------------------------------------------------------
#include "boot_state.h"
#include "death_state.h"
#include "factory_reset_state.h"
#include "flow_boot_wifi.h"
#include "game_options_state.h"
#include "inventory_state.h"
#include "mg_pause_menu.h"
#include "mini_game_pause_menu.h"
#include "mini_games.h"
#include "name_entry_state.h"
#include "new_pet_flow_state.h"
#include "settings_flow_state.h"
#include "settings_nav_state.h"
#include "settings_state.h"
#include "system_status_state.h"
#include "time_state.h"
#include "ui_death_menu.h"
#include "ui_feed_menu.h"
#include "ui_menu_state.h"
#include "ui_play_menu.h"
#include "ui_power_menu.h"
#include "ui_settings_menu.h"
#include "ui_settings_pages.h"
#include "ui_sleep_menu.h"
#include "ui_state_backup_pet_list.h"
#include "ui_state_import_pet_list.h"
#include "ui_state_title_menu.h"
#include "user_toggles_state.h"
#include "wifi_setup_state.h"

// -----------------------------------------------------------------------------
// OTA / Build / Config
// -----------------------------------------------------------------------------
#include "asset_ota.h"
#include "asset_ota_config.h"
#include "build_flags.h"
#include "version.h"

// -----------------------------------------------------------------------------
// Misc / Tools
// -----------------------------------------------------------------------------
#include "console.h"

// END of includes

// --- Cache/Draw Helpers
bool g_forcePetBgCache = false;
void drawHatchingScreen(bool redrawBg);
static void drawBurialScreen();
static void drawEvolutionScreen();
static void drawMiniStatPreview();
static void drawMiniStatPreviewSleepLeft();
static void drawPetPerfHud();
static void drawBackupPetListScreen(bool redrawBg);
static void drawImportPetListScreen(bool redrawBg);

// --- Graphics and Runtime
void resetPetScreenPositionToHome();
void startPetIntroWalkFromLeft();
static bool drawIntroWalkingPetOverride();
static void resetPetWanderToHome();

// --- Compatibility wrappers / missing helpers (compile fix) ---
static void drawSettingsScreen();
static void drawInventoryScreen();
static void drawPetSleepingScreen();
static void drawMiniGameScreen();
static bool getPngWH(const char *path, int &outW, int &outH);
static void drawCenteredImageSpr(const char *path, int cx, int cy);
static bool getPngWH(const char *path, int &outW, int &outH);
static void drawCenteredImageSpr(const char *path, int cx, int cy);
static void drawCrackedEggBig(int cx, int topY, const char *path);

// SD image helpers (avoid LGFX template instantiation on fs::SDFS)
bool sprDrawJpgFromSD(const char *path, int x, int y);
bool sprDrawPngFromSD(const char *path, int x, int y);
bool canvasDrawPngFromSD(m5gfx::M5Canvas &canvas, const char *path, int x, int y);
bool canvasDrawJpgFromSD(m5gfx::M5Canvas &canvas, const char *path, int x, int y);

// Provide no-arg wrappers for existing bool-signature screens
static void drawDeathScreen();   // calls drawDeathScreen(bool)
static void drawNamePetScreen(); // calls drawNamePetScreen(bool)
// Provide no-arg wrappers for existing bool-signature screens
static void drawDeathScreen();   // calls drawDeathScreen(bool)
static void drawNamePetScreen(); // calls drawNamePetScreen(bool)

// Set-time helpers
static void drawSetTimePanel(int x, int y, int w, int h, const char *title, int selectedField, int fieldId);
static void drawButton(int x, int y, int w, int h, const char *label, bool selected);

// Level-Up Popup helpers
static bool g_levelUpPopupActive = false;
static uint16_t g_levelUpPopupLevel = 0;
void uiInitLevelPopupTracker();
void uiResetLevelUpPopupState();

// ---------------------------------------------------------------------------
// Pet screen position (anchor-based, bottom-center)
// ---------------------------------------------------------------------------

static int s_petScreenX = 0;
static int s_petScreenY = 0;
static bool s_petScreenPosInitialized = false;

static bool s_petIntroWalkActive = false;
static uint32_t s_petIntroWalkLastStepMs = 0;
static constexpr int kPetIntroWalkStepPx = 3;
static constexpr uint32_t kPetIntroWalkStepMs = 40;

static bool s_petIntroArriveTurnActive = false;
static uint32_t s_petIntroArriveTurnStartMs = 0;
static constexpr uint32_t kPetIntroArriveTurnMs = 180;

static bool s_petIntroStandHoldActive = false;
static uint32_t s_petIntroStandHoldStartMs = 0;
static constexpr int kPetIntroYOffset = -10; // tune this
static constexpr uint32_t kPetIntroStandHoldMs = 300;
static bool s_petIntroHandoffActive = false;
static constexpr uint32_t kPetIntroWalkFrameMs = 60;

static int s_petHomeX = 0;
static int s_petHomeY = 0;

enum class PetWanderState : uint8_t
{
  HOME_IDLE = 0,
  MOVING_TO_SIDE_A,
  PAUSE_AWAY_1,
  MOVING_TO_SIDE_B,
  PAUSE_AWAY_2,
  RETURNING_HOME
};

static PetWanderState s_petWanderState = PetWanderState::HOME_IDLE;
static int s_petWanderTargetX = 0;
static int s_petWanderSideAX = 0;
static int s_petWanderSideBX = 0;
static uint32_t s_petWanderUntilMs = 0;
static uint32_t s_petWanderLastStepMs = 0;

static constexpr int kPetWanderRangePx = 55;
static constexpr int kPetWanderMinMovePx = 28;
static constexpr int kPetWanderStepPx = 2;
static constexpr uint32_t kPetWanderStepMs = 30;
static constexpr uint32_t kPetWanderPauseAwayMs = 5000;
static constexpr uint32_t kPetWanderMinIdleMs = 5000;
static constexpr uint32_t kPetWanderMaxIdleMs = 7000;

static bool petWalkOverrideActive()
{
  return s_petIntroWalkActive || s_petIntroArriveTurnActive || s_petIntroStandHoldActive || s_petIntroHandoffActive ||
         s_petWanderState == PetWanderState::MOVING_TO_SIDE_A || s_petWanderState == PetWanderState::MOVING_TO_SIDE_B ||
         s_petWanderState == PetWanderState::RETURNING_HOME;
}

// -- Pet Walking Paths

// -- Devil
// -- Devil Baby
static const char *PATH_DEV_BB_WALK1 = "/raising_hell/graphics/pet/anim/dev/bb/wlk/dev_bb_walk1.png";
static const char *PATH_DEV_BB_WALK2 = "/raising_hell/graphics/pet/anim/dev/bb/wlk/dev_bb_walk2.png";
static const char *PATH_DEV_BB_WALK1_L = "/raising_hell/graphics/pet/anim/dev/bb/wlk/dev_bb_walkleft1.png";
static const char *PATH_DEV_BB_WALK2_L = "/raising_hell/graphics/pet/anim/dev/bb/wlk/dev_bb_walkleft2.png";

// -- Devil Teen
static const char *PATH_DEV_TN_WALK1 = "/raising_hell/graphics/pet/anim/dev/tn/wlk/dev_tn_walk1.png";
static const char *PATH_DEV_TN_WALK2 = "/raising_hell/graphics/pet/anim/dev/tn/wlk/dev_tn_walk2.png";
static const char *PATH_DEV_TN_WALK1_L = "/raising_hell/graphics/pet/anim/dev/tn/wlk/dev_tn_walkleft1.png";
static const char *PATH_DEV_TN_WALK2_L = "/raising_hell/graphics/pet/anim/dev/tn/wlk/dev_tn_walkleft2.png";

// -- Devil Teen
static const char *PATH_DEV_AD_WALK1 = "/raising_hell/graphics/pet/anim/dev/ad/wlk/dev_ad_walk1.png";
static const char *PATH_DEV_AD_WALK2 = "/raising_hell/graphics/pet/anim/dev/ad/wlk/dev_ad_walk2.png";
static const char *PATH_DEV_AD_WALK1_L = "/raising_hell/graphics/pet/anim/dev/ad/wlk/dev_ad_walkleft1.png";
static const char *PATH_DEV_AD_WALK2_L = "/raising_hell/graphics/pet/anim/dev/ad/wlk/dev_ad_walkleft2.png";

// -- Devil Elder
static const char *PATH_DEV_EL_WALK1 = "/raising_hell/graphics/pet/anim/dev/ed/wlk/dev_edr_walk1.png";
static const char *PATH_DEV_EL_WALK2 = "/raising_hell/graphics/pet/anim/dev/ed/wlk/dev_edr_walk2.png";
static const char *PATH_DEV_EL_WALK1_L = "/raising_hell/graphics/pet/anim/dev/ed/wlk/dev_edr_walkleft1.png";
static const char *PATH_DEV_EL_WALK2_L = "/raising_hell/graphics/pet/anim/dev/ed/wlk/dev_edr_walkleft2.png";

// -- Eldritch
// -- Eldritch Baby
static const char *PATH_ELD_BB_WALK1 = "/raising_hell/graphics/pet/anim/eld/bb/wlk/eld_bb_walk1.png";
static const char *PATH_ELD_BB_WALK2 = "/raising_hell/graphics/pet/anim/eld/bb/wlk/eld_bb_walk2.png";
static const char *PATH_ELD_BB_WALK1_L = "/raising_hell/graphics/pet/anim/eld/bb/wlk/eld_bb_walkleft1.png";
static const char *PATH_ELD_BB_WALK2_L = "/raising_hell/graphics/pet/anim/eld/bb/wlk/eld_bb_walkleft2.png";

void uiResetLevelUpPopupState()
{
  g_levelUpPopupActive = false;
  g_levelUpPopupLevel = 0;
}

// Utility: toast message overlay (timed)
static bool g_toastActive = false;
static uint32_t g_toastUntilMs = 0;
static char g_toastMsg[64] = {0};

static void uiDrawToastOverlay();
// Sleep Graphics Kick
static volatile bool g_sleepBgKick = false;

// Random Shit
static void drawNonPetTabBackground();

void sleepBgKickNow()
{
  g_sleepBgKick = true;
  requestUIRedraw();
}

// Clip Picker for Evolution Stages
static AnimId evoHappyClipFor(PetType type, uint8_t stage)
{
  if (stage > 3)
    stage = 3;

  switch (type)
  {
  case PET_DEVIL:
    switch (stage)
    {
    case 0:
      return ANIM_DEV_BABY_HAPPY_BALL;
    case 1:
      return ANIM_DEV_TEEN_HAPPY_POSE;
    case 2:
      return ANIM_DEV_ADULT_HAPPY_TAIL;
    default:
      return ANIM_DEV_ELDER_HAPPY_SHAKE;
    }

  case PET_ELDRITCH:
    switch (stage)
    {
    case 0:
      return ANIM_ELD_BABY_HAPPY_SIT;
    case 1:
      return ANIM_ELD_TEEN_HAPPY_BOB;
    case 2:
      return ANIM_ELD_ADULT_HAPPY_SPIN;
    default:
      return ANIM_ELD_ELDER_HAPPY_PASS;
    }

  case PET_KAIJU:
    return ANIM_KAI_IDLE_1F;
  case PET_ALIEN:
    return ANIM_AL_IDLE_1F;
  case PET_ANUBIS:
    return ANIM_ANU_IDLE_1F;
  case PET_AXOLOTL:
    return ANIM_AXO_IDLE_1F;
  default:
    return ANIM_NONE;
  }
}

// -----------------------------------------------------------------------------
// SD image helpers for sprites
//
// IMPORTANT:
// Do NOT call spr.drawJpgFile(SD, ...) / spr.drawPngFile(SD, ...) on this toolchain.
// That path instantiates DataWrapperT<fs::...> and fails to compile.
// Instead, setFileStorage(SD) once, then use the "path-only" overload.
// -----------------------------------------------------------------------------
static void ensureSprFileStorage() { spr.setFileStorage(SD); }

bool sprDrawJpgFromSD(const char *path, int x, int y)
{
  if (!g_sdReady)
    return false;
  if (!path || !*path)
    return false;
  ensureSprFileStorage();
  return spr.drawJpgFile(path, x, y);
}

bool sprDrawPngFromSD(const char *path, int x, int y)
{
  if (!g_sdReady)
    return false;
  if (!path || !*path)
    return false;
  ensureSprFileStorage();
  return spr.drawPngFile(path, x, y);
}

bool canvasDrawPngFromSD(M5Canvas &canvas, const char *path, int x, int y)
{
  canvas.setFileStorage(SD);
  return canvas.drawPngFile(path, x, y);
}

bool canvasDrawJpgFromSD(M5Canvas &canvas, const char *path, int x, int y)
{
  canvas.setFileStorage(SD);
  return canvas.drawJpgFile(path, x, y);
}
// -----------------------------------------------------------------------------
// LovyanGFX DataWrapper for Arduino fs::File
// -----------------------------------------------------------------------------
class RH_FileDataWrapper : public lgfx::v1::DataWrapper
{
public:
  explicit RH_FileDataWrapper(fs::File &f) : _f(&f) {}

  int read(uint8_t *buf, uint32_t len) override
  {
    if (!_f)
      return 0;
    return (int)_f->read(buf, len);
  }

  void skip(int32_t offset) override
  {
    if (!_f)
      return;
    _f->seek(_f->position() + offset);
  }

  bool seek(uint32_t offset) override
  {
    if (!_f)
      return false;
    return _f->seek(offset);
  }

  void close(void) override
  {
    // no-op; caller closes the file
  }

  int32_t tell(void) override
  {
    if (!_f)
      return 0;
    return (int32_t)_f->position();
  }

private:
  fs::File *_f = nullptr;
};

// -----------------------------------------------------------------------------
// Nudge offsets (tweak as desired)
// -----------------------------------------------------------------------------
static constexpr int PET_X_OFFSET = 2;
static constexpr int PET_Y_OFFSET = 8;

// Pet sprite expected size
static constexpr int PET_SPR_W = 84;
static constexpr int PET_SPR_H = 84;

// Optional offscreen layer (kept; not required for current draw path)
static M5Canvas petLayer(&spr);
static bool petLayerReady = false;
static void drawSetTimeScreen();

// -----------------------------------------------------------------------------
// Pet UI color scheme
// -----------------------------------------------------------------------------
struct PetUIColorScheme
{
  // Top bar
  uint16_t topBg;
  uint16_t topOutline;
  uint16_t topText;

  // Tab bar
  uint16_t tabBg;
  uint16_t tabOutline;
  uint16_t tabFillSel;
  uint16_t tabTextOff;
  uint16_t tabTextOn;
};

static inline PetUIColorScheme uiSchemeForPet(PetType t)
{
  switch (t)
  {

  case PET_KAIJU:
    // Purple theme
    return PetUIColorScheme{
        0x1803, // topBg (very dark purple)
        0x780F, // topOutline (purple)
        0xFFFF, // topText

        0x1803, // tabBg
        0x780F, // tabOutline
        0xA81F, // tabFillSel (bright purple/pink-ish)
        0xFFFF, // tabTextOff
        0x0000  // tabTextOn
    };

  case PET_ALIEN:
    // Green theme
    return PetUIColorScheme{
        0x0200, // topBg (very dark green)
        0x07E0, // topOutline (green)
        0xFFFF, // topText

        0x0200, // tabBg
        0x07E0, // tabOutline
        0x87F0, // tabFillSel (light green)
        0xFFFF, // tabTextOff
        0x0000  // tabTextOn
    };

  case PET_ANUBIS:
    // Gold / yellow theme
    return PetUIColorScheme{
        0x4200, // topBg (dark brown/gold base)
        0xFFE0, // topOutline (yellow)
        0xFFFF, // topText

        0x4200, // tabBg
        0xFFE0, // tabOutline
        0xFD20, // tabFillSel (gold)
        0xFFFF, // tabTextOff
        0x0000  // tabTextOn
    };

  case PET_AXOLOTL:
    // Pink theme
    return PetUIColorScheme{
        0x3808, // topBg (dark pink)
        0xF81F, // topOutline (magenta/pink)
        0xFFFF, // topText

        0x3808, // tabBg
        0xF81F, // tabOutline
        0xFB56, // tabFillSel (pink)
        0xFFFF, // tabTextOff
        0x0000  // tabTextOn
    };

  case PET_ELDRITCH:
    return PetUIColorScheme{
        0x0010, // topBg
        0x001F, // topOutline (blue)
        0xFFFF, // topText

        0x0010, // tabBg
        0x001F, // tabOutline
        0x07FF, // tabFillSel (cyan)
        0xFFFF, // tabTextOff
        0x0000  // tabTextOn
    };

  case PET_DEVIL:
  default:
    return PetUIColorScheme{
        0x2000, // topBg
        0xF800, // topOutline (red)
        0xFFFF, // topText

        0x2000, // tabBg
        0xF800, // tabOutline
        0xFBE0, // tabFillSel (yellow)
        0xFFFF, // tabTextOff
        0x0000  // tabTextOn
    };
  }
}

static inline uint16_t uiPillOutline(PetType t) { return uiSchemeForPet(t).topOutline; }

static inline uint16_t uiPillFillSelected(PetType t)
{
  switch (t)
  {
  case PET_KAIJU:
    return 0x780F; // purple
  case PET_ALIEN:
    return 0x07E0; // green
  case PET_ANUBIS:
    return 0xFD20; // gold
  case PET_AXOLOTL:
    return 0xFB56; // pink
  case PET_ELDRITCH:
    return 0x0018; // slightly darker blue
  case PET_DEVIL:
  default:
    return 0x2104; // devil-ish pill fill
  }
}

static inline uint16_t uiModalOutline(PetType t) { return uiSchemeForPet(t).topOutline; }

// -----------------------------------------------------------------------------
// Paths (SD)
// -----------------------------------------------------------------------------
static const char *PATH_BG_PET = "/raising_hell/graphics/bg/hell_bg.jpg";
static const char *PATH_BG_SLEEP = "/raising_hell/graphics/background/sleep_bg.jpg";
static const char *PATH_BG_SPLASH = "/raising_hell/graphics/background/flow/rh_splash.jpg";
static const char *PATH_BG_NONPET_TILE_DEV = "/raising_hell/graphics/background/flow/dev_tab_bg.png";
static const char *PATH_BG_NONPET_TILE_ELD = "/raising_hell/graphics/background/flow/eld_tab_bg.png";

static const char *PATH_INF_COIN = "/raising_hell/graphics/ui/icons/inf_coin.png";
static const char *PATH_LIFE_ICON = "/raising_hell/graphics/ui/icons/life_icon.png";
static const char *PATH_FOOD_ICON = "/raising_hell/graphics/ui/icons/food_icon.png";
static const char *PATH_MOOD_ICON = "/raising_hell/graphics/ui/icons/mood_icon.png";
static const char *PATH_REST_ICON = "/raising_hell/graphics/ui/icons/rest_icon.png";

static constexpr int MINI_STAT_ICON_W = 18;
static constexpr int MINI_STAT_ICON_H = 18;
static constexpr uint16_t MINI_STAT_ICON_TRANSPARENT = 0xF81F;

static M5Canvas s_miniStatLifeIcon(&spr);
static bool s_miniStatLifeIconReady = false;
static M5Canvas s_miniStatCoinIcon(&spr);
static bool s_miniStatCoinIconReady = false;

static constexpr int HUD_HEADER_ICON_W = 12;
static constexpr int HUD_HEADER_ICON_H = 12;
static constexpr int HUD_STAT_ICON_W = 10;
static constexpr int HUD_STAT_ICON_H = 10;
static constexpr uint16_t HUD_ICON_TRANSPARENT = 0xF81F;

static M5Canvas s_hudLifeIconSmall(&spr);
static bool s_hudLifeIconSmallReady = false;
static M5Canvas s_hudCoinIconSmall(&spr);
static bool s_hudCoinIconSmallReady = false;

static M5Canvas s_hudFoodIcon(&spr);
static bool s_hudFoodIconReady = false;
static M5Canvas s_hudMoodIcon(&spr);
static bool s_hudMoodIconReady = false;
static M5Canvas s_hudRestIcon(&spr);
static bool s_hudRestIconReady = false;

static int deathTransitionYNudgeForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return -6;
  case PET_DEVIL:
    return -2;
  default:
    return -2;
  }
}

static bool ensureMiniStatIconCache(M5Canvas &canvas, bool &ready, const char *path)
{
  if (ready)
    return true;
  if (!g_sdReady || !path || !*path)
    return false;

  canvas.setColorDepth(16);

  if (!canvas.width() || !canvas.height())
  {
    if (!canvas.createSprite(MINI_STAT_ICON_W, MINI_STAT_ICON_H))
      return false;
  }

  canvas.fillSprite(MINI_STAT_ICON_TRANSPARENT);

  if (!canvasDrawPngFromSD(canvas, path, 1, 1))
  {
    canvas.deleteSprite();
    ready = false;
    return false;
  }

  ready = true;
  return true;
}

static bool drawMiniStatIconCached(const char *path, int x, int y)
{
  M5Canvas *canvas = nullptr;
  bool *ready = nullptr;

  if (path == PATH_LIFE_ICON)
  {
    canvas = &s_miniStatLifeIcon;
    ready = &s_miniStatLifeIconReady;
  }
  else if (path == PATH_INF_COIN)
  {
    canvas = &s_miniStatCoinIcon;
    ready = &s_miniStatCoinIconReady;
  }

  if (canvas && ready && ensureMiniStatIconCache(*canvas, *ready, path))
  {
    canvas->pushSprite(x, y, MINI_STAT_ICON_TRANSPARENT);
    return true;
  }

  if (g_sdReady)
    return sprDrawPngFromSD(path, x, y);

  return false;
}

static bool ensureHudIconCache(M5Canvas &canvas, bool &ready, const char *path, int w, int h)
{
  if (ready)
    return true;
  if (!g_sdReady || !path || !*path)
    return false;

  canvas.setColorDepth(16);

  if (!canvas.width() || !canvas.height())
  {
    if (!canvas.createSprite(w, h))
      return false;
  }

  canvas.fillSprite(HUD_ICON_TRANSPARENT);

  if (!canvasDrawPngFromSD(canvas, path, 0, 0))
  {
    canvas.deleteSprite();
    ready = false;
    return false;
  }

  ready = true;
  return true;
}

static bool drawHudIconCached(const char *path, int x, int y)
{
  M5Canvas *canvas = nullptr;
  bool *ready = nullptr;
  int w = 0;
  int h = 0;

  if (path == PATH_LIFE_ICON)
  {
    canvas = &s_hudLifeIconSmall;
    ready = &s_hudLifeIconSmallReady;
    w = HUD_HEADER_ICON_W;
    h = HUD_HEADER_ICON_H;
  }
  else if (path == PATH_INF_COIN)
  {
    canvas = &s_hudCoinIconSmall;
    ready = &s_hudCoinIconSmallReady;
    w = HUD_HEADER_ICON_W;
    h = HUD_HEADER_ICON_H;
  }
  else if (path == PATH_FOOD_ICON)
  {
    canvas = &s_hudFoodIcon;
    ready = &s_hudFoodIconReady;
    w = HUD_STAT_ICON_W;
    h = HUD_STAT_ICON_H;
  }
  else if (path == PATH_MOOD_ICON)
  {
    canvas = &s_hudMoodIcon;
    ready = &s_hudMoodIconReady;
    w = HUD_STAT_ICON_W;
    h = HUD_STAT_ICON_H;
  }
  else if (path == PATH_REST_ICON)
  {
    canvas = &s_hudRestIcon;
    ready = &s_hudRestIconReady;
    w = HUD_STAT_ICON_W;
    h = HUD_STAT_ICON_H;
  }

  if (canvas && ready && ensureHudIconCache(*canvas, *ready, path, w, h))
  {
    canvas->pushSprite(x, y, HUD_ICON_TRANSPARENT);
    return true;
  }

  if (g_sdReady)
    return sprDrawPngFromSD(path, x, y);

  return false;
}

static void drawTitleMenuText(M5Canvas &dst, const char *text, int x, int y, uint8_t font, uint16_t fg,
                              textdatum_t datum)
{
  if (!text || !*text)
    return;

  M5Canvas textSpr(&dst);
  textSpr.setColorDepth(16);

  textSpr.setTextFont(font);
  textSpr.setTextSize(1);

  const int tw = textSpr.textWidth(text);
  const int th = (font == 1) ? 8 : 16;

  const int padX = 2;
  const int padY = 1;
  const int sw = tw + padX * 2;
  const int sh = th + padY * 2;

  if (!textSpr.createSprite(sw, sh))
    return;

  const uint16_t key = TFT_MAGENTA;
  textSpr.fillSprite(key);
  textSpr.setTextColor(fg, key);
  textSpr.setTextDatum(TL_DATUM);
  textSpr.drawString(text, padX, padY, font);

  int px = x;
  int py = y;

  switch (datum)
  {
  case textdatum_t::top_center:
    px = x - (sw / 2);
    py = y;
    break;
  case textdatum_t::top_left:
    px = x;
    py = y;
    break;
  case textdatum_t::top_right:
    px = x - sw;
    py = y;
    break;
  default:
    px = x - (sw / 2);
    py = y;
    break;
  }

  textSpr.pushSprite(&dst, px, py, key);
  textSpr.deleteSprite();
}

static void drawMiniStatNumberRight(int value, int rightX, int y)
{
  char buf[16];
  snprintf(buf, sizeof(buf), "%d", value);
  spr.drawString(buf, rightX, y);
}

// Shop item icons (per-pet theme)
static const char *PATH_SHOP_DEV_FOOD = "/raising_hell/graphics/ui/shop_items/dev/dev_food.png";
static const char *PATH_SHOP_DEV_MOOD = "/raising_hell/graphics/ui/shop_items/dev/dev_mood.png";
static const char *PATH_SHOP_DEV_REST = "/raising_hell/graphics/ui/shop_items/dev/dev_rest.png";
static const char *PATH_SHOP_DEV_HEALTH = "/raising_hell/graphics/ui/shop_items/dev/dev_health.png";
static const char *PATH_SHOP_DEV_EVO = "/raising_hell/graphics/ui/shop_items/dev/dev_evo.png";

static const char *PATH_SHOP_ELD_FOOD = "/raising_hell/graphics/ui/shop_items/eld/eld_food.png";
static const char *PATH_SHOP_ELD_MOOD = "/raising_hell/graphics/ui/shop_items/eld/eld_mood.png";
static const char *PATH_SHOP_ELD_REST = "/raising_hell/graphics/ui/shop_items/eld/eld_rest.png";
static const char *PATH_SHOP_ELD_HEALTH = "/raising_hell/graphics/ui/shop_items/eld/eld_health.png";
static const char *PATH_SHOP_ELD_EVO = "/raising_hell/graphics/ui/shop_items/eld/eld_evo.png";

#define DEV_EGG_PNG "/raising_hell/graphics/pet/egg/dev_egg.png"
#define ELD_EGG_PNG "/raising_hell/graphics/pet/egg/eld_egg.png"
#define KAI_EGG_PNG "/raising_hell/graphics/pet/egg/kai_egg.png"
#define ANU_EGG_PNG "/raising_hell/graphics/pet/egg/anu_egg.png"
#define AXO_EGG_PNG "/raising_hell/graphics/pet/egg/axo_egg.png"
#define AL_EGG_PNG "/raising_hell/graphics/pet/egg/al_egg.png"

// Pet-area background (drawn at PET_AREA_Y)
static const char *PATH_BG_DEVIL_BABY = "/raising_hell/graphics/background/dev/hell_bg.jpg";
static const char *PATH_BG_DEVIL_TEEN = "/raising_hell/graphics/background/dev/dev_teen_bg.jpg";
static const char *PATH_BG_DEVIL_ADULT = "/raising_hell/graphics/background/dev/dev_ad_bg.jpg";
static const char *PATH_BG_DEVIL_ELDER = "/raising_hell/graphics/background/dev/dev_el_bg.jpg";

static const char *PATH_BG_ELDRITCH = "/raising_hell/graphics/background/eld/eld_bg.jpg";
static const char *PATH_BG_ELDRITCH_TEEN = "/raising_hell/graphics/background/eld/eld_teen_bg.jpg";
static const char *PATH_BG_ELDRITCH_ADULT = "/raising_hell/graphics/background/eld/eld_ad_bg.jpg";
static const char *PATH_BG_ELDRITCH_ELDER = "/raising_hell/graphics/background/eld/eld_el_bg.jpg";

static const char *PATH_BG_KAIJU = "/raising_hell/graphics/background/kai/kai_bg.jpg";
static const char *PATH_BG_ALIEN = "/raising_hell/graphics/background/al/al_bg.jpg";
static const char *PATH_BG_ANUBIS = "/raising_hell/graphics/background/anu/anu_bg.jpg";
static const char *PATH_BG_AXOLOTL = "/raising_hell/graphics/background/axo/axo_bg.jpg";

static inline const char *bgPathForPet(PetType t)
{
  switch (t)
  {
  case PET_KAIJU:
    return PATH_BG_KAIJU;
  case PET_ALIEN:
    return PATH_BG_ALIEN;
  case PET_ANUBIS:
    return PATH_BG_ANUBIS;
  case PET_AXOLOTL:
    return PATH_BG_AXOLOTL;
  case PET_ELDRITCH:
    return PATH_BG_ELDRITCH;
  case PET_DEVIL:
  default:
    return PATH_BG_PET;
  }
}

static const char *bgPathForPetWithStage(PetType t, int evoStage)
{
  if (t == PET_DEVIL)
  {
    if (evoStage >= 3)
      return PATH_BG_DEVIL_ELDER;
    if (evoStage == 2)
      return PATH_BG_DEVIL_ADULT;
    if (evoStage == 1)
      return PATH_BG_DEVIL_TEEN;
    return PATH_BG_DEVIL_BABY;
  }

  if (t == PET_ELDRITCH)
  {
    if (evoStage >= 3)
      return PATH_BG_ELDRITCH_ELDER;
    if (evoStage == 2)
      return PATH_BG_ELDRITCH_ADULT;
    if (evoStage == 1)
      return PATH_BG_ELDRITCH_TEEN;
    return PATH_BG_ELDRITCH;
  }

  return bgPathForPet(t);
}

static inline const char *nonPetTilePathForPet(PetType t)
{
  switch (t)
  {
  case PET_ELDRITCH:
    return PATH_BG_NONPET_TILE_ELD;
  case PET_DEVIL:
  default:
    return PATH_BG_NONPET_TILE_DEV;
  }
}

// -----------------------------------------------------------------------------
// Burial helpers (self-contained; no external dependencies)
// -----------------------------------------------------------------------------
static void formatDateYMD(time_t t, char *out, size_t outSz)
{
  if (!out || outSz == 0)
    return;
  if (t <= 0)
  {
    snprintf(out, outSz, "----/--/--");
    return;
  }

  tm lt;
  localtime_r(&t, &lt);
  snprintf(out, outSz, "%04d/%02d/%02d", lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
}

// IMPORTANT: wire this to whatever you *actually* named your stored birth epoch.
// For now this returns 0 so it compiles until you point it at the real field.
static time_t getPetBirthEpoch() { return 0; }

// -----------------------------------------------------------------------------
// Sleep anim heartbeat (so frames can advance even when nothing else triggers redraw)
// -----------------------------------------------------------------------------
static volatile bool g_sleepBgWakeKick = false;

void sleepBgNotifyScreenWake()
{
  g_sleepBgWakeKick = true;
  requestUIRedraw();
}

static uint32_t g_sleepAnimNextFrameMs = 0;
static bool g_sleepAnimActive = false;

void sleepAnimHeartbeat(uint32_t now)
{
  if (!g_sleepAnimActive)
    return;
  if (g_sleepAnimNextFrameMs == 0)
    return;

  if ((int32_t)(now - g_sleepAnimNextFrameMs) >= 0)
  {
    requestUIRedraw();
  }
}

// -----------------------------------------------------------------------------
// Pet Sleep Background
// -----------------------------------------------------------------------------
static inline const char *sleepBgForPet(PetType type)
{
  switch (type)
  {
  case PET_ELDRITCH:
    return "/raising_hell/graphics/background/eld_sleep.jpg";
  case PET_DEVIL:
  default:
    return PATH_BG_SLEEP;
  }
}

enum class HelpLineType : uint8_t
{
  TITLE,
  SECTION,
  BODY,
  GAP
};

struct HelpLine
{
  HelpLineType type;
  const char *text;
};

static const HelpLine kControlsManual[] = {

    {HelpLineType::SECTION, "Welcome to Raising Hell"},
    {HelpLineType::BODY, "Thank you for playing"},
    {HelpLineType::GAP, nullptr},

    {HelpLineType::SECTION, "Basic Controls"},
    {HelpLineType::BODY, "LEFT/RIGHT  Switch tabs"},
    {HelpLineType::BODY, "UP/DOWN     Move selection"},
    {HelpLineType::BODY, "ENTER/G     Confirm / interact"},
    {HelpLineType::BODY, "DEL/Q       Back / home"},
    {HelpLineType::BODY, "ESC         Open Settings / cancel"},
    {HelpLineType::BODY, "Z-M         Jump to tab"},
    {HelpLineType::BODY, "GO          Toggle screen on/off"},
    {HelpLineType::BODY, "\\           Console"},
    {HelpLineType::BODY, "Shake       Wake console"},
    {HelpLineType::GAP, nullptr},

    {HelpLineType::SECTION, "Alt Navigation Clusters"},
    {HelpLineType::BODY, "(E A S D) and (O J K L)"},
    {HelpLineType::GAP, nullptr},

    {HelpLineType::SECTION, "Congratulations!!"},
    {HelpLineType::BODY, "You summoned something from"},
    {HelpLineType::BODY, "beyond our mortal plane."},
    {HelpLineType::BODY, "Now it's your problem!."},

    {HelpLineType::GAP, nullptr},
};

static constexpr int kControlsManualCount = (int)(sizeof(kControlsManual) / sizeof(kControlsManual[0]));

static int g_controlsHelpScroll = 0;

static int controlsHelpLineHeight(const HelpLine &line)
{
  switch (line.type)
  {
  case HelpLineType::TITLE:
    return 16;
  case HelpLineType::SECTION:
    return 16;
  case HelpLineType::BODY:
    return 14;
  case HelpLineType::GAP:
    return 8;
  default:
    return 14;
  }
}

static int controlsHelpMaxScroll()
{
  const int topY = 6;
  const int bottomHelpH = 14;
  const int viewY = topY;
  const int viewH = SCREEN_H - topY - bottomHelpH - 4;

  int lastValidStart = 0;

  for (int start = 0; start < kControlsManualCount; ++start)
  {
    int y = viewY;
    bool drewAnything = false;

    for (int i = start; i < kControlsManualCount; ++i)
    {
      const int lineH = controlsHelpLineHeight(kControlsManual[i]);
      if (y + lineH > viewY + viewH)
        break;

      y += lineH;
      drewAnything = true;
    }

    if (!drewAnything)
      break;

    lastValidStart = start;
  }

  return lastValidStart;
}

void controlsHelpResetScroll() { g_controlsHelpScroll = 0; }

bool controlsHelpScrollUp()
{
  if (g_controlsHelpScroll <= 0)
    return false;

  --g_controlsHelpScroll;
  requestUIRedraw();
  return true;
}

bool controlsHelpScrollDown()
{
  const int maxScroll = controlsHelpMaxScroll();
  if (g_controlsHelpScroll >= maxScroll)
    return false;

  ++g_controlsHelpScroll;
  requestUIRedraw();
  return true;
}

// -----------------------------------------------------------------------------
// Controls Helper
// -----------------------------------------------------------------------------
static void drawControlsHelpScreen()
{
  drawNonPetTabBackground();

  const int screenW = SCREEN_W;
  const int screenH = SCREEN_H;

  const int outerMargin = 8;
  const int panelPad = 6;
  const int panelRadius = 6;

  const int panelX = outerMargin;
  const int panelY = outerMargin;
  const int panelW = screenW - (outerMargin * 2);
  const int panelH = screenH - (outerMargin * 2);

  const int viewX = panelX + panelPad;
  const int viewY = panelY + panelPad;
  const int viewW = panelW - (panelPad * 2);
  const int viewH = panelH - (panelPad * 2);

  // ---------------------------------------------------------------------------
  // Background panel for controls help
  // ---------------------------------------------------------------------------
  spr.fillRoundRect(panelX, panelY, panelW, panelH, panelRadius, TFT_BLACK);
  spr.drawRoundRect(panelX, panelY, panelW, panelH, panelRadius, TFT_DARKGREY);

  spr.setTextDatum(TL_DATUM);
  spr.setTextSize(1);

  // Larger fonts than before
  const int titleFont = 2;
  const int sectionFont = 2;
  const int bodyFont = 2;

  const int titleH = 16;
  const int sectionH = 16;
  const int bodyH = 14;
  const int gapH = 8;

  int y = viewY;
  int drawn = 0;

  spr.setClipRect(viewX, viewY, viewW, viewH);

  for (int i = g_controlsHelpScroll; i < kControlsManualCount; ++i)
  {
    const HelpLine &line = kControlsManual[i];

    int lineH = bodyH;
    switch (line.type)
    {
    case HelpLineType::TITLE:
      lineH = titleH;
      break;
    case HelpLineType::SECTION:
      lineH = sectionH;
      break;
    case HelpLineType::BODY:
      lineH = bodyH;
      break;
    case HelpLineType::GAP:
      lineH = gapH;
      break;
    }

    if (y + lineH > viewY + viewH)
      break;

    switch (line.type)
    {
    case HelpLineType::TITLE:
      spr.setTextColor(TFT_CYAN);
      spr.drawString(line.text ? line.text : "", viewX, y, titleFont);
      break;

    case HelpLineType::SECTION:
      spr.setTextColor(TFT_YELLOW);
      spr.drawString(line.text ? line.text : "", viewX, y, sectionFont);
      break;

    case HelpLineType::BODY:
      spr.setTextColor(TFT_WHITE);
      spr.drawString(line.text ? line.text : "", viewX, y, bodyFont);
      break;

    case HelpLineType::GAP:
      break;
    }

    y += lineH;
    drawn++;
  }

  spr.clearClipRect();

  // Scroll indicators
  spr.setTextColor(TFT_LIGHTGREY, TFT_TRANSPARENT);
  if (g_controlsHelpScroll > 0)
    spr.drawString("^", panelX + panelW - 12, panelY + 4, 1);

  if (g_controlsHelpScroll + drawn < kControlsManualCount)
    spr.drawString("v", panelX + panelW - 12, panelY + panelH - 12, 1);
}

void drawBootWifiPromptScreen()
{
  spr.fillScreen(TFT_BLACK);
  spr.setTextDatum(TL_DATUM);
  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);

  spr.drawString("First boot setup", 10, 10);
  spr.drawString("Setup WiFi to auto-set time?", 10, 40);

  spr.drawString("ENTER: Setup WiFi", 10, 80);
  spr.drawString("ESC: Enter Time Manually", 10, 100);

  spr.pushSprite(0, 0);
}

void drawBootAssetWifiRequiredScreen()
{
  spr.fillScreen(TFT_BLACK);
  spr.setTextDatum(TL_DATUM);
  spr.setTextFont(2);
  spr.setTextSize(1);

  spr.setTextColor(TFT_RED, TFT_BLACK);
  spr.drawString("Initial asset download", 10, 10);

  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.drawString("Raising Hell requires", 10, 36);
  spr.drawString("an internet connection", 10, 54);
  spr.drawString("for initial asset", 10, 72);
  spr.drawString("download.", 10, 90);

  spr.setTextColor(TFT_GREEN, TFT_BLACK);
  spr.drawString("ENTER: Set up Wi-Fi", 10, 118);

  spr.setTextColor(TFT_DARKGREY, TFT_BLACK);
  spr.drawString("\\: Console", 10, 136);

  spr.pushSprite(0, 0);
}

// -----------------------------------------------------------------------------
// Import wifi credentials from HLauncher
// -----------------------------------------------------------------------------
static void drawBootWifiImportedScreen()
{
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(CC_DATUM);

  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.drawCentreString("Wi-Fi Settings", screenW / 2, 16, 2);
  spr.drawCentreString("Imported from Launcher", screenW / 2, 34, 2);

  const char *ssid = bootWifiImportedSsid();
  if (ssid && ssid[0])
  {
    spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    String line = String("SSID: ") + ssid;
    spr.drawCentreString(line.c_str(), screenW / 2, 54, 2);
  }

  const AssetOtaStatus st = assetOtaStatus();
  const char *statusText = assetOtaStatusString();
  const char *errText = assetOtaLastErrorString();

  const uint16_t cur = assetOtaCurrentFileIndex();
  const uint16_t total = assetOtaTotalFileCount();

  spr.setTextColor(TFT_WHITE, TFT_BLACK);

  switch (st)
  {
  case AssetOtaStatus::CHECKING:
    spr.drawCentreString("Checking assets...", screenW / 2, 74, 2);
    break;
  case AssetOtaStatus::DOWNLOADING:
    spr.drawCentreString("Downloading assets...", screenW / 2, 74, 2);
    break;
  case AssetOtaStatus::INSTALLING:
    spr.drawCentreString("Installing assets...", screenW / 2, 74, 2);
    break;
  case AssetOtaStatus::SUCCESS:
    spr.setTextColor(TFT_GREEN, TFT_BLACK);
    spr.drawCentreString("Assets ready", screenW / 2, 74, 2);
    break;
  case AssetOtaStatus::FAILED:
    spr.setTextColor(TFT_RED, TFT_BLACK);
    spr.drawCentreString("Asset setup failed", screenW / 2, 74, 2);
    break;
  case AssetOtaStatus::IDLE:
  default:
    spr.setTextColor(TFT_GREEN, TFT_BLACK);
    spr.drawCentreString("Connecting...", screenW / 2, 74, 2);
    break;
  }

  if (total > 0)
  {
    spr.setTextColor(TFT_WHITE, TFT_BLACK);

    char prog[32];
    snprintf(prog, sizeof(prog), "%u / %u", (unsigned)cur, (unsigned)total);
    spr.drawCentreString(prog, screenW / 2, 94, 2);

    const int barX = 20;
    const int barY = 106;
    const int barW = screenW - 40;
    const int barH = 10;

    spr.drawRect(barX, barY, barW, barH, TFT_WHITE);

    int fillW = 0;
    if (total > 0)
      fillW = (int)(((uint32_t)cur * (uint32_t)(barW - 2)) / (uint32_t)total);

    if (fillW < 0)
      fillW = 0;
    if (fillW > (barW - 2))
      fillW = barW - 2;

    if (fillW > 0)
      spr.fillRect(barX + 1, barY + 1, fillW, barH - 2, TFT_GREEN);
  }

  if (st == AssetOtaStatus::FAILED && errText && errText[0])
  {
    spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    spr.drawCentreString(errText, screenW / 2, 126, 1);
  }
  else if (statusText && statusText[0])
  {
    spr.setTextColor(TFT_DARKGREY, TFT_BLACK);
    spr.drawCentreString(statusText, screenW / 2, 126, 1);
  }

  spr.setTextDatum(TL_DATUM);
}

// -----------------------------------------------------------------------------
// First boot wifi setup
// -----------------------------------------------------------------------------
void drawBootWifiWaitScreen(bool connected, int rssi)
{
  (void)connected;
  (void)rssi;

  spr.fillScreen(TFT_BLACK);
  spr.setTextDatum(TL_DATUM);
  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);

  const char *ssid = wifiConsoleSsid();
  const uint32_t ageMs = wifiConsoleConnectAgeMs();
  const uint32_t ageS = ageMs / 1000;
  const int wl = WiFi.status();

  const char *st = nullptr;
  switch (wl)
  {
  case WL_CONNECTED:
    st = "Connected";
    break;
  case WL_IDLE_STATUS:
    st = "Authorizing...";
    break;
  case WL_NO_SSID_AVAIL:
    st = "SSID not found";
    break;
  case WL_CONNECT_FAILED:
    st = "Connect failed";
    break;
  case WL_CONNECTION_LOST:
    st = "Connection lost";
    break;
  case WL_DISCONNECTED:
    st = "Disconnected";
    break;
  default:
    st = "Connecting...";
    break;
  }

  const bool reallyConnected = (wl == WL_CONNECTED);
  const int liveRssi = reallyConnected ? WiFi.RSSI() : 0;

  spr.drawString("Connecting WiFi...", 10, 10);

  if (ssid && ssid[0])
    spr.drawString((String("SSID: ") + ssid).c_str(), 10, 28);
  else
    spr.drawString("SSID: (none)", 10, 28);

  spr.drawString((String("Status: ") + st).c_str(), 10, 46);
  spr.drawString((String("Elapsed: ") + String(ageS) + "s").c_str(), 10, 64);

  if (reallyConnected)
  {
    spr.drawString("WiFi connected", 10, 86);
    spr.drawString(("RSSI: " + String(liveRssi)).c_str(), 10, 104);
    spr.drawString("Advancing to Timezone...", 10, 126);
  }
  else
  {
    if (ageS >= 20)
    {
      spr.drawString("Still not connected.", 10, 92);
      spr.drawString("Check password/signal.", 10, 110);
      spr.drawString("ESC: Skip  (or re-enter WiFi)", 10, 132);
    }
    else
    {
      spr.drawString("Not connected yet", 10, 92);
      spr.drawString("ESC: Skip", 10, 132);
    }
  }

  spr.pushSprite(0, 0);
}

void drawBootTimezonePickScreen()
{
  spr.fillScreen(TFT_BLACK);
  spr.setTextDatum(TL_DATUM);
  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);

  spr.drawString("Select Timezone", 10, 10);
  spr.drawString(tzName((uint8_t)tzIndex), 10, 45);

  spr.drawString("UP/DN: Change", 10, 90);
  spr.drawString("ENTER: Confirm", 10, 110);
  spr.drawString("ESC: Skip", 10, 130);

  spr.pushSprite(0, 0);
}

void drawBootNtpWaitScreen(bool connected, bool synced)
{
  spr.fillScreen(TFT_BLACK);
  spr.setTextDatum(TL_DATUM);
  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);

  spr.drawString("Setting time from NTP...", 10, 10);

  spr.drawString(connected ? "WiFi: Connected" : "WiFi: Not connected", 10, 45);
  spr.drawString(synced ? "NTP: Synced" : "NTP: Waiting...", 10, 65);

  spr.drawString("ESC: Skip", 10, 120);

  spr.pushSprite(0, 0);
}

static void drawSleepScreenImpl(bool redrawBg);

void drawSleepScreen() { drawSleepScreenImpl(true); }

// // -----------------------------------------------------------------------------
// EGG Cracker - Cracks your eggs
// -----------------------------------------------------------------------------

static const char *pendingEggClosedPng()
{
  if (g_pendingPetType == PET_ELDRITCH)
    return "/raising_hell/graphics/pet/egg/eld_egg.png";

  return "/raising_hell/graphics/pet/egg/dev_egg.png";
}

static const char *pendingEggCrackedPng()
{
  if (g_pendingPetType == PET_ELDRITCH)
    return "/raising_hell/graphics/pet/egg/eld_egg_cracked.png";

  return "/raising_hell/graphics/pet/egg/dev_egg_cracked.png";
}

static const char *const *pendingEggCrackFrames()
{
  static const char *const devFrames[4] = {
      "/raising_hell/graphics/pet/egg/anim/dev/devil_crack1.png",
      "/raising_hell/graphics/pet/egg/anim/dev/devil_crack2.png",
      "/raising_hell/graphics/pet/egg/anim/dev/devil_crack3.png",
      "/raising_hell/graphics/pet/egg/anim/dev/devil_crack4.png",
  };

  static const char *const eldFrames[4] = {
      "/raising_hell/graphics/pet/egg/anim/eld/eld_crack1.png",
      "/raising_hell/graphics/pet/egg/anim/eld/eld_crack2.png",
      "/raising_hell/graphics/pet/egg/anim/eld/eld_crack3.png",
      "/raising_hell/graphics/pet/egg/anim/eld/eld_crack4.png",
  };

  return (g_pendingPetType == PET_ELDRITCH) ? eldFrames : devFrames;
}

static const char *pendingHatchMessage()
{
  switch (g_pendingPetType)
  {
  case PET_ELDRITCH:
    return "You hatched a baby eldritch";
  case PET_DEVIL:
  default:
    return "You hatched a baby devil";
  }
}

// -----------------------------------------------------------------------------
// Background cache invalidation flag (public API)
// -----------------------------------------------------------------------------
static volatile bool g_forceBgRedraw = false;

void invalidateBackgroundCache() { g_forceBgRedraw = true; }
bool consumeBackgroundInvalidation()
{
  bool v = g_forceBgRedraw;
  g_forceBgRedraw = false;
  return v;
}
bool backgroundCacheInvalidated() { return g_forceBgRedraw; }

// -----------------------------------------------------------------------------
// Local helpers (forward decls)
// -----------------------------------------------------------------------------
static bool drawJpegBackground(const char *path);
static void drawSleepScreenImpl(bool redrawBg);
static void drawPetScreenImpl(bool redrawBg);
static void drawMiniStatPreview();
static void listWindow(int total, int current, int maxVisible, int &start, int &count);
static void drawCurrentScreen(bool redrawBg);
static void drawDeathScreen(bool redrawBg);
static void drawDeathTransitionScreen(bool redrawBg);
static void drawWifiSetupScreen();
static void drawNamePetScreen(bool redrawBg);
static void drawDecayModePickerMenu();
static void drawScreenSettingsMenu();
static void drawSystemSettingsMenu();
static void drawWifiSettingsMenu();
static void drawPlaceholderMenu(const char *title);
static void drawCreditsScreen();
static void uiDrawToastOverlay();

void drawBootLowBatteryChargingScreen(int mv, int pct, bool usb, bool readyToBoot);
void drawChoosePetScreen(bool redrawBg);
void drawTitleMenuScreen(bool redrawBg);
void drawPowerMenu(); // non-static (renderUI calls it)
void ui_drawMessageWindow(const char *title, const char *line1, const char *line2, bool maskLine2, bool showCursor);
void ui_showMessage(const char *msg);

static bool ensurePetLayer();
static void cachePetAreaBackgroundIfNeeded(bool needPetBg);
static void restorePetAreaFromCache();

// -----------------------------------------------------------------------------
// Background caching per UI state
// -----------------------------------------------------------------------------
static UIState lastDrawnState = UIState::PET_SCREEN;
static bool bgDrawnForState = false;
static M5Canvas s_nonPetTile(&M5.Display);
static bool s_nonPetTileReady = false;
static int s_nonPetTileW = 0;
static int s_nonPetTileH = 0;
static PetType s_nonPetTileCachedType = (PetType)255;
static constexpr int NONPET_TILE_W = 35;
static constexpr int NONPET_TILE_H = 70;

static bool s_petScreenIntroFadeActive = false;
static bool s_petScreenWasActiveLastFrame = false;
static uint32_t s_petScreenIntroFadeStartMs = 0;
static constexpr uint32_t kPetScreenIntroFadeMs = 1200;
void startPetScreenIntroFadeNow();
// Mini-stat panel sizing (must match drawMiniStatPreview)
static constexpr int MINI_STAT_W = 56;
static constexpr int MINI_STAT_PAD = 4;

static int clampi(int v, int lo, int hi)
{
  if (v < lo)
    return lo;
  if (v > hi)
    return hi;
  return v;
}

// -----------------------------------------------------------------------------
// Low Battery Screen
// -----------------------------------------------------------------------------

void drawBootLowBatteryChargingScreen(int mv, int pct, bool usb, bool readyToBoot)
{
  spr.fillScreen(TFT_BLACK);
  spr.setTextDatum(TL_DATUM);
  spr.setTextFont(2);
  spr.setTextSize(1);

  spr.setTextColor(TFT_RED, TFT_BLACK);
  spr.drawString("Battery Too Low", 10, 10);

  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.drawString("Raising Hell needs more", 10, 34);
  spr.drawString("power before it can boot.", 10, 52);

  char batBuf[40];
  snprintf(batBuf, sizeof(batBuf), "Battery: %d%%  %dmV", pct, mv);
  spr.drawString(batBuf, 10, 78);

  spr.drawString(usb ? "USB: Connected" : "USB: Not connected", 10, 96);

  if (readyToBoot)
  {
    spr.setTextColor(TFT_GREEN, TFT_BLACK);
    spr.drawString("Battery OK - starting...", 10, 122);
  }
  else if (usb)
  {
    spr.setTextColor(TFT_YELLOW, TFT_BLACK);
    spr.drawString("Charging... waiting for safe", 10, 122);
    spr.drawString("voltage to continue boot.", 10, 140);
  }
  else
  {
    spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    spr.drawString("Plug in USB to charge and", 10, 122);
    spr.drawString("boot automatically.", 10, 140);
  }

  spr.pushSprite(0, 0);
}

// -----------------------------------------------------------------------------
// Splash screen (silent fallback)
// -----------------------------------------------------------------------------
static void drawSplashScreen(bool forceRedraw)
{
  if (!isScreenOn())
    return;

  if (forceRedraw)
  {
    spr.fillRect(0, 0, SCREEN_W, SCREEN_H, TFT_BLACK);
  }

  bool ok = false;
  if (g_sdReady)
    ok = sprDrawJpgFromSD(PATH_BG_SPLASH, 0, 0);

  if (!ok)
  {
    spr.fillRect(0, 0, SCREEN_W, SCREEN_H, TFT_BLACK);
  }
}

void drawBootSplash()
{
  spr.fillRect(0, 0, SCREEN_W, SCREEN_H, TFT_BLACK);

  bool ok = false;
  if (g_sdReady)
    ok = sprDrawJpgFromSD(PATH_BG_SPLASH, 0, 0);

  if (!ok)
  {
    spr.setTextDatum(MC_DATUM);
    spr.setTextFont(2);
    spr.setTextSize(1);
    spr.setTextColor(TFT_RED, TFT_BLACK);
    spr.drawString("BOOTING...", SCREEN_W / 2, SCREEN_H / 2);
  }

  spr.pushSprite(0, 0);
}

// -----------------------------------------------------------------------------
// Boot splash lock
// -----------------------------------------------------------------------------
void ui_setBootSplashActive(bool on)
{
  g_bootSplashActive = on;
  bgDrawnForState = false;
  invalidateBackgroundCache();

  if (!on)
    requestFullUIRedraw();
}

bool ui_isBootSplashActive() { return g_bootSplashActive; }

// -----------------------------------------------------------------------------
// JPEG background draw into SPR
// -----------------------------------------------------------------------------
static bool drawJpegBackground(const char *path)
{
  if (!g_sdReady || !path)
    return false;
  return sprDrawJpgFromSD(path, 0, 0);
}

void drawBackground(const char *path)
{
  if (!g_sdReady || !path)
    return;
  (void)drawJpegBackground(path);
}

static inline void clearContentArea(uint16_t color = TFT_BLACK)
{
  spr.fillRect(0, PET_AREA_Y, SCREEN_W, PET_AREA_H, color);
}

static bool ensureNonPetTileReady()
{
  const PetType desiredType = pet.type;
  const char *path = nonPetTilePathForPet(desiredType);

  if (s_nonPetTileReady && s_nonPetTileW > 0 && s_nonPetTileH > 0 && s_nonPetTileCachedType == desiredType)
  {
    return true;
  }

  s_nonPetTile.deleteSprite();
  s_nonPetTileReady = false;
  s_nonPetTileW = 0;
  s_nonPetTileH = 0;
  s_nonPetTileCachedType = (PetType)255;

  if (!g_sdReady)
    return false;

  s_nonPetTile.setColorDepth(16);
  if (!s_nonPetTile.createSprite(NONPET_TILE_W, NONPET_TILE_H))
  {
    Serial.println("[NONPET TILE] createSprite failed");
    return false;
  }

  s_nonPetTile.fillSprite(TFT_BLACK);

  bool ok = false;
  const char *ext = strrchr(path, '.');

  if (ext && (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0))
    ok = canvasDrawJpgFromSD(s_nonPetTile, path, 0, 0);
  else
    ok = canvasDrawPngFromSD(s_nonPetTile, path, 0, 0);

  if (!ok)
  {
    static char s_lastNonPetTileFailPath[160] = {0};

    const char *failPath = path ? path : "(null)";
    if (strcmp(s_lastNonPetTileFailPath, failPath) != 0)
    {
      strncpy(s_lastNonPetTileFailPath, failPath, sizeof(s_lastNonPetTileFailPath) - 1);
      s_lastNonPetTileFailPath[sizeof(s_lastNonPetTileFailPath) - 1] = '\0';
      Serial.printf("[NONPET TILE] load failed path='%s'\n", failPath);
    }

    s_nonPetTile.deleteSprite();
    return false;
  }

  s_nonPetTileW = s_nonPetTile.width();
  s_nonPetTileH = s_nonPetTile.height();
  s_nonPetTileReady = (s_nonPetTileW > 0 && s_nonPetTileH > 0);

  if (s_nonPetTileReady)
    s_nonPetTileCachedType = desiredType;

  if (s_nonPetTileW != NONPET_TILE_W || s_nonPetTileH != NONPET_TILE_H)
  {
    Serial.printf("[NONPET TILE] unexpected cache size %dx%d expected %dx%d\n", s_nonPetTileW, s_nonPetTileH,
                  NONPET_TILE_W, NONPET_TILE_H);
  }

  Serial.printf("[NONPET TILE] ready path='%s' w=%d h=%d\n", path ? path : "(null)", s_nonPetTileW, s_nonPetTileH);

  return s_nonPetTileReady;
}

static void drawNonPetTabBackground()
{
  spr.fillScreen(TFT_BLACK);

  if (!ensureNonPetTileReady())
  {
    spr.fillScreen(TFT_RED);
    return;
  }

  for (int y = 0; y < SCREEN_H; y += s_nonPetTileH)
  {
    for (int x = 0; x < SCREEN_W; x += s_nonPetTileW)
    {
      s_nonPetTile.pushSprite(&spr, x, y);
    }
  }
}

// -----------------------------------------------------------------------------
// RAW streaming (legacy)
// -----------------------------------------------------------------------------
static const int MAX_LINE_WIDTH = 240;
static uint16_t lineBuf[MAX_LINE_WIDTH];

static bool clipRectToScreen(int &x, int &y, int &w, int &h)
{
  if (w <= 0 || h <= 0)
    return false;
  if (x >= screenW || y >= screenH)
    return false;
  if (x + w <= 0 || y + h <= 0)
    return false;

  if (x < 0)
  {
    w += x;
    x = 0;
  }
  if (y < 0)
  {
    h += y;
    y = 0;
  }

  if (x + w > screenW)
    w = screenW - x;
  if (y + h > screenH)
    h = screenH - y;

  return (w > 0 && h > 0);
}

bool streamRawImage(const char *path, int x, int y, int w, int h)
{
  if (!g_sdReady || !path)
    return false;
  if (!clipRectToScreen(x, y, w, h))
    return false;

  File f = SD.open(path, FILE_READ);
  if (!f)
    return false;

  if (w > MAX_LINE_WIDTH)
    w = MAX_LINE_WIDTH;
  size_t rowBytes = (size_t)w * 2;

  for (int row = 0; row < h; ++row)
  {
    size_t n = f.read((uint8_t *)lineBuf, rowBytes);
    if (n != rowBytes)
    {
      f.close();
      return false;
    }
    spr.pushImage(x, y + row, w, 1, lineBuf);
  }

  f.close();
  return true;
}

bool streamRawImageFast(const char *path, int x, int y, int w, int h) { return streamRawImage(path, x, y, w, h); }

// -----------------------------------------------------------------------------
// Time formatting helper (HUD)
// -----------------------------------------------------------------------------
String formatTime()
{
  if (currentHour < 0 || currentMinute < 0 || currentHour > 23 || currentMinute > 59)
  {
    return "--:--";
  }

  int h = currentHour;
  bool pm = false;

  if (h == 0)
  {
    h = 12;
    pm = false;
  }
  else if (h == 12)
  {
    pm = true;
  }
  else if (h > 12)
  {
    h -= 12;
    pm = true;
  }

  char buf[9];
  snprintf(buf, sizeof(buf), "%d:%02d %s", h, currentMinute, pm ? "PM" : "AM");
  return String(buf);
}

// ============================================================================
// GLOBAL UI CHROME (Top Bar + Bottom Tab Bar)
// ============================================================================
static int wifiBarsFromRssi(int rssi)
{
  if (rssi >= -55)
    return 4;
  if (rssi >= -67)
    return 3;
  if (rssi >= -75)
    return 2;
  if (rssi >= -85)
    return 1;
  return 0;
}

static void drawWifiIcon(int x, int y)
{
  const int w = 14;
  const int h = 10;
  const uint16_t col = TFT_WHITE;

  if (!wifiIsEnabled() || !wifiIsConnected())
  {
    int cx = x + (w / 2);
    int cy = y + (h / 2);
    int r = 3;
    spr.drawLine(cx - r, cy - r, cx + r, cy + r, col);
    spr.drawLine(cx + r, cy - r, cx - r, cy + r, col);
    return;
  }

  int bars = wifiBarsFromRssi(wifiRssi());
  for (int i = 0; i < 4; i++)
  {
    int barH = (i + 1) * 2;
    int bx = x + i * 3;
    int by = y + (h - barH);
    if (i < bars)
      spr.fillRect(bx, by, 2, barH, col);
    else
      spr.drawRect(bx, by, 2, barH, col);
  }
}

void drawTopBar()
{
  const PetUIColorScheme ui = uiSchemeForPet(pet.type);
  const uint16_t bg = ui.topBg;
  const uint16_t outline = ui.topOutline;
  const uint16_t text = ui.topText;

  const int padL = 10;
  const int padR = 4;

  const int batW = 18;
  const int batH = 8;
  const int wifiW = 14;
  const int wifiH = 10;

  const int gapTimeToWifi = 4;
  const int gapWifiToBat = 4;

  const int boltW = 8;
  const int gapWifiToBolt = 4;
  const int gapBoltToBat = 4;

  spr.fillRect(0, 0, SCREEN_W, TOP_BAR_H, bg);
  spr.drawFastHLine(0, TOP_BAR_H - 1, SCREEN_W, outline);

  spr.setTextFont(1);
  spr.setTextSize(1);
  spr.setTextColor(text, bg);

  String t = formatTime();
  int tw = spr.textWidth(t.c_str());

  int batX = SCREEN_W - padR - batW;
  int batY = (TOP_BAR_H - batH) / 2;

  const int boltX = batX - gapBoltToBat - boltW;
  const int boltY = batY + 1;

  int wifiX = batX - gapWifiToBat - wifiW;
  if (usbPowered)
    wifiX = boltX - gapWifiToBolt - wifiW;
  int wifiY = (TOP_BAR_H - wifiH) / 2;

  int timeRightEdge = wifiX - gapTimeToWifi;
  int timeX = timeRightEdge - tw;
  if (timeX < 0)
    timeX = 0;

  spr.setTextDatum(TL_DATUM);
  spr.drawString(t, timeX, (TOP_BAR_H - 8) / 2);

  drawWifiIcon(wifiX, wifiY);

  int pct = batteryPercent;
  if (pct > 100)
    pct = 100;

  spr.drawRect(batX, batY, batW, batH, text);
  spr.drawRect(batX + batW, batY + 2, 2, batH - 4, text);
  spr.fillRect(batX + 1, batY + 1, batW - 2, batH - 2, bg);

  if (pct >= 0)
  {
    int fillW = (batW - 2) * pct / 100;
    fillW = clampi(fillW, 0, batW - 2);
    spr.fillRect(batX + 1, batY + 1, fillW, batH - 2, text);
  }

  if (usbPowered)
  {
    const uint16_t boltCol = 0xFFE0;
    for (int dx = 0; dx <= 1; dx++)
    {
      spr.drawLine(boltX + dx, boltY + 0, boltX + dx + 4, boltY + 3, boltCol);
      spr.drawLine(boltX + dx + 4, boltY + 3, boltX + dx + 1, boltY + 3, boltCol);
      spr.drawLine(boltX + dx + 1, boltY + 3, boltX + dx + 5, boltY + 6, boltCol);
    }
  }

  // ---------------------------------------------------------------------------
  // Dynamic title: "<PetName> - $<inf>" (fallbacks if too long)
  // ---------------------------------------------------------------------------
  const int titleMaxRight = timeX - 6;
  const int titleY = (TOP_BAR_H - 8) / 2;

  // Grab name (robust against different storage styles)
  const char *petName = nullptr;
  // Common cases:
  // - pet.name is a char array
  // - pet.name is a const char*
  // If your Pet uses something else, swap this line accordingly.
  petName = pet.name;

  if (!petName || !petName[0])
    petName = "Pet";

  const unsigned int inf = (unsigned int)pet.inf;

  char titleBuf[64];
  snprintf(titleBuf, sizeof(titleBuf), "%s - %u Inf", petName, inf);

  // Try shorter fallbacks if needed
  char shortBuf[32];
  snprintf(shortBuf, sizeof(shortBuf), "$%u", inf);

  const char *useTitle = titleBuf;

  int minRight = titleMaxRight;
  if (minRight < padL + 10)
    minRight = padL + 10;

  int wFull = spr.textWidth(titleBuf);
  int wShort = spr.textWidth(shortBuf);

  if (padL + wFull > minRight)
    useTitle = shortBuf;
  if (padL + wShort > minRight)
    useTitle = "";

  spr.setTextDatum(TL_DATUM);
  spr.drawString(useTitle, padL, titleY);
  spr.setTextDatum(TL_DATUM);
}

static void tabWindow(int total, int current, int maxVisible, int &start, int &count)
{
  count = (total < maxVisible) ? total : maxVisible;
  int half = count / 2;
  start = current - half;
  start = clampi(start, 0, total - count);
}

void drawTabBar()
{
  const int y = SCREEN_H - TAB_BAR_H;

  const PetUIColorScheme ui = uiSchemeForPet(pet.type);

  const uint16_t bg = ui.tabBg;
  const uint16_t outline = ui.tabOutline;
  const uint16_t fillSel = ui.tabFillSel;
  const uint16_t textOff = ui.tabTextOff;
  const uint16_t textOn = ui.tabTextOn;

  constexpr int MAX_VISIBLE_TABS = 5;

  static const char *labels[] = {"PET", "STAT", "FEED", "PLAY", "SLEEP", "INV", "SHOP"};
  const int labelsCount = (int)(sizeof(labels) / sizeof(labels[0]));

  spr.fillRect(0, y, SCREEN_W, TAB_BAR_H, bg);
  spr.drawFastHLine(0, y, SCREEN_W, outline);

  int start = 0, visCount = 0;
  const int totalTabs = TAB_COUNT_INT();
  tabWindow(totalTabs, (int)g_app.currentTab, MAX_VISIBLE_TABS, start, visCount);

  const int slotW = SCREEN_W / visCount;
  const int padX = 2;
  const int padY = 2;
  const int tabW = slotW - padX * 2;
  const int tabH = TAB_BAR_H - padY * 2;
  const int r = 4;

  spr.setTextDatum(MC_DATUM);
  spr.setTextFont(1);
  spr.setTextSize(1);

  for (int i = 0; i < visCount; ++i)
  {
    const int tabIndex = start + i;
    const bool selected = (tabIndex == (int)g_app.currentTab);

    const int x = i * slotW + padX;
    const int ty = y + padY;
    const int cx = x + tabW / 2;
    const int cy = ty + tabH / 2;

    if (selected)
    {
      spr.fillRoundRect(x, ty, tabW, tabH, r, fillSel);
      spr.drawRoundRect(x, ty, tabW, tabH, r, outline);
      spr.setTextColor(textOn, fillSel);
    }
    else
    {
      spr.drawRoundRect(x, ty, tabW, tabH, r, outline);
      spr.setTextColor(textOff, bg);
    }

    const char *s = (tabIndex >= 0 && tabIndex < labelsCount) ? labels[tabIndex] : "?";

    int tw = spr.textWidth(s);
    const int th = 8;

    int tx = cx - (tw / 2);
    int tyText = cy - (th / 2);

    spr.setTextDatum(TL_DATUM);
    spr.drawString(s, tx, tyText);
    spr.setTextDatum(MC_DATUM);
  }

  spr.setTextFont(1);
  spr.setTextSize(1);
  spr.setTextColor(textOff, bg);

  const int arrowY = y + (TAB_BAR_H / 2) - 4;

  if (start > 0)
  {
    spr.setTextDatum(TL_DATUM);
    spr.drawString("<", 2, arrowY);
  }

  if (start + visCount < totalTabs)
  {
    spr.setTextDatum(TR_DATUM);
    spr.drawString(">", SCREEN_W - 2, arrowY);
  }

  spr.setTextDatum(MC_DATUM);
}

const char *getBioStatusImagePath()
{
  const PetMood mood = petResolveMood(pet);

  // --------------------------------------------------------------------------
  // DEVIL BIOS
  // --------------------------------------------------------------------------
  if (pet.type == PET_DEVIL)
  {
    // ---------------- BABY ----------------
    if (pet.evoStage == 0)
    {
      switch (mood)
      {
      case MOOD_SICK:
        return "/raising_hell/graphics/pet/bio/dev/bb/dev_bb_sck_bio.png";
      case MOOD_TIRED:
        return "/raising_hell/graphics/pet/bio/dev/bb/dev_bb_trd_bio.png";
      case MOOD_HUNGRY:
        return "/raising_hell/graphics/pet/bio/dev/bb/dev_bb_hgy_bio.png";
      case MOOD_MAD:
        return "/raising_hell/graphics/pet/bio/dev/bb/dev_bb_agy_bio.png";
      case MOOD_BORED:
        return "/raising_hell/graphics/pet/bio/dev/bb/dev_bb_brd_bio.png";
      default:
        return "/raising_hell/graphics/pet/bio/dev/bb/dev_bb_hpy_bio.png";
      }
    }

    // ---------------- TEEN ----------------
    if (pet.evoStage == 1)
    {
      switch (mood)
      {
      case MOOD_SICK:
        return "/raising_hell/graphics/pet/bio/dev/tn/dev_tn_sck_bio.png";
      case MOOD_TIRED:
        return "/raising_hell/graphics/pet/bio/dev/tn/dev_tn_trd_bio.png";
      case MOOD_HUNGRY:
        return "/raising_hell/graphics/pet/bio/dev/tn/dev_tn_hgy_bio.png";
      case MOOD_MAD:
        return "/raising_hell/graphics/pet/bio/dev/tn/dev_tn_agy_bio.png";
      case MOOD_BORED:
        return "/raising_hell/graphics/pet/bio/dev/tn/dev_tn_brd_bio.png";
      default:
        return "/raising_hell/graphics/pet/bio/dev/tn/dev_tn_hpy_bio.png";
      }
    }

    // ---------------- ADULT ----------------
    if (pet.evoStage == 2)
    {
      switch (mood)
      {
      case MOOD_SICK:
        return "/raising_hell/graphics/pet/bio/dev/ad/dev_ad_sck_bio.png";
      case MOOD_TIRED:
        return "/raising_hell/graphics/pet/bio/dev/ad/dev_ad_trd_bio.png";
      case MOOD_HUNGRY:
        return "/raising_hell/graphics/pet/bio/dev/ad/dev_ad_hgy_bio.png";
      case MOOD_MAD:
        return "/raising_hell/graphics/pet/bio/dev/ad/dev_ad_agy_bio.png";
      case MOOD_BORED:
        return "/raising_hell/graphics/pet/bio/dev/ad/dev_ad_brd_bio.png";
      default:
        return "/raising_hell/graphics/pet/bio/dev/ad/dev_ad_hpy_bio.png";
      }
    }

    // ---------------- ELDER ----------------
    if (pet.evoStage >= 3)
    {
      switch (mood)
      {
      case MOOD_SICK:
        return "/raising_hell/graphics/pet/bio/dev/ed/dev_ed_sck_bio.png";
      case MOOD_TIRED:
        return "/raising_hell/graphics/pet/bio/dev/ed/dev_ed_trd_bio.png";
      case MOOD_HUNGRY:
        return "/raising_hell/graphics/pet/bio/dev/ed/dev_ed_hgy_bio.png";
      case MOOD_MAD:
        return "/raising_hell/graphics/pet/bio/dev/ed/dev_ed_agy_bio.png";
      case MOOD_BORED:
        return "/raising_hell/graphics/pet/bio/dev/ed/dev_ed_brd_bio.png";
      default:
        return "/raising_hell/graphics/pet/bio/dev/ed/dev_ed_hpy_bio.png";
      }
    }
  }

  // --------------------------------------------------------------------------
  // ELDRITCH BIOS
  // --------------------------------------------------------------------------
  if (pet.type == PET_ELDRITCH)
  {
    // ---------------- BABY ----------------
    if (pet.evoStage == 0)
    {
      switch (mood)
      {
      case MOOD_SICK:
        return "/raising_hell/graphics/pet/bio/eld/bb/eld_baby_sck_bio.png";
      case MOOD_TIRED:
        return "/raising_hell/graphics/pet/bio/eld/bb/eld_baby_trd_bio.png";
      case MOOD_HUNGRY:
        return "/raising_hell/graphics/pet/bio/eld/bb/eld_baby_hgy_bio.png";
      case MOOD_MAD:
        return "/raising_hell/graphics/pet/bio/eld/bb/eld_baby_agy_bio.png";
      case MOOD_BORED:
        return "/raising_hell/graphics/pet/bio/eld/bb/eld_baby_brd_bio.png";
      default:
        return "/raising_hell/graphics/pet/bio/eld/bb/eld_baby_hpy_bio.png";
      }
    }

    // ---------------- TEEN ----------------
    if (pet.evoStage == 1)
    {
      switch (mood)
      {
      case MOOD_SICK:
        return "/raising_hell/graphics/pet/bio/eld/tn/eld_teen_sck_bio.png";
      case MOOD_TIRED:
        return "/raising_hell/graphics/pet/bio/eld/tn/eld_teen_trd_bio.png";
      case MOOD_HUNGRY:
        return "/raising_hell/graphics/pet/bio/eld/tn/eld_teen_hgy_bio.png";
      case MOOD_MAD:
        return "/raising_hell/graphics/pet/bio/eld/tn/eld_teen_agy_bio.png";
      case MOOD_BORED:
        return "/raising_hell/graphics/pet/bio/eld/tn/eld_teen_brd_bio.png";
      default:
        return "/raising_hell/graphics/pet/bio/eld/tn/eld_teen_hpy_bio.png";
      }
    }

    // ---------------- ADULT ----------------
    if (pet.evoStage == 2)
    {
      switch (mood)
      {
      case MOOD_SICK:
        return "/raising_hell/graphics/pet/bio/eld/ad/eld_ad_sck_bio.png";
      case MOOD_TIRED:
        return "/raising_hell/graphics/pet/bio/eld/ad/eld_ad_trd_bio.png";
      case MOOD_HUNGRY:
        return "/raising_hell/graphics/pet/bio/eld/ad/eld_ad_hgy_bio.png";
      case MOOD_MAD:
        return "/raising_hell/graphics/pet/bio/eld/ad/eld_ad_agy_bio.png";
      case MOOD_BORED:
        return "/raising_hell/graphics/pet/bio/eld/ad/eld_ad_brd_bio.png";
      default:
        return "/raising_hell/graphics/pet/bio/eld/ad/eld_ad_hpy_bio.png";
      }
    }

    // ---------------- ELDER ----------------
    if (pet.evoStage >= 3)
    {
      switch (mood)
      {
      case MOOD_SICK:
        return "/raising_hell/graphics/pet/bio/eld/ed/eld_ed_sck_bio.png";
      case MOOD_TIRED:
        return "/raising_hell/graphics/pet/bio/eld/ed/eld_ed_trd_bio.png";
      case MOOD_HUNGRY:
        return "/raising_hell/graphics/pet/bio/eld/ed/eld_ed_hgy_bio.png";
      case MOOD_MAD:
        return "/raising_hell/graphics/pet/bio/eld/ed/eld_ed_agy_bio.png";
      case MOOD_BORED:
        return "/raising_hell/graphics/pet/bio/eld/ed/eld_ed_brd_bio.png";
      default:
        return "/raising_hell/graphics/pet/bio/eld/ed/eld_ed_hpy_bio.png";
      }
    }
  }

  // Fallback for future stages
  return "/raising_hell/graphics/pet/bio/eld/eld_baby_hpy_bio.png";
}

// ============================================================================
// Decay Mode Select Helper
// ============================================================================
static const char *decayModeLabel(uint8_t mode)
{
  switch (mode)
  {
  case 0:
    return "Super Slow";
  case 1:
    return "Slow";
  case 2:
    return "Normal";
  case 3:
    return "Fast";
  case 4:
    return "Very Fast";
  case 5:
    return "Insane";
  default:
    return "Normal";
  }
}

// ============================================================================
// Utility: list window
// ============================================================================
static void listWindow(int total, int current, int maxVisible, int &start, int &count)
{
  count = (total < maxVisible) ? total : maxVisible;
  int half = count / 2;
  start = current - half;
  start = clampi(start, 0, total - count);
}

// ============================================================================
// SETTINGS MENU
// ============================================================================
static const char *brightnessToText(int level)
{
  if (level <= 0)
    return "LOW";
  if (level == 1)
    return "MED";
  return "HIGH";
}

static const char *decayModeToText(uint8_t m)
{
  switch (m)
  {
  case 0:
    return "Super Slow";
  case 1:
    return "Slow";
  case 2:
    return "Normal";
  case 3:
    return "Fast";
  case 4:
    return "Super Fast";
  case 5:
    return "Insane";
  default:
    return "Normal";
  }
}

static void drawSettingsTopMenu()
{
  drawNonPetTabBackground();
  drawTopBar();

  const int contentY = TOP_BAR_H;
  const int contentH = SCREEN_H - TOP_BAR_H;
  const int contentBottom = contentY + contentH;

  char volumeLine[24];
  snprintf(volumeLine, sizeof(volumeLine), "Volume: %s", soundVolumeToText(soundGetVolumeLevel()));

  static const char *labelsStatic[] = {
      "Manual",
      nullptr, // 1 => volumeLine
      "Pet Options >",   "Screen Settings >", "System Settings >", "Game Options >", "Console >",
      "System Status >", "Credits",           "Store Pet",         "Main Menu",
  };

  const int totalItems = 11;

  g_app.settingsIndex = clampi(g_app.settingsIndex, 0, totalItems - 1);

  constexpr int MAX_VISIBLE = 5;
  int start = 0, visCount = 0;
  listWindow(totalItems, g_app.settingsIndex, MAX_VISIBLE, start, visCount);

  int itemH = 20;
  int gap = 5;

  int totalH = visCount * itemH + (visCount - 1) * gap;

  while (totalH > contentH && itemH > 16)
  {
    itemH--;
    if (gap > 3)
      gap--;

    totalH = visCount * itemH + (visCount - 1) * gap;
  }

  int startY = contentY + (contentH - totalH) / 2;
  if (startY < contentY)
    startY = contentY;
  if (startY + totalH > contentBottom)
    startY = contentBottom - totalH;

  const int boxW = (SCREEN_W * 3) / 4;
  const int boxX = (SCREEN_W - boxW) / 2;
  const int radius = 10;

  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextDatum(TL_DATUM);

  for (int row = 0; row < visCount; row++)
  {
    const int i = start + row;
    int y = startY + row * (itemH + gap);

    const bool sel = (i == g_app.settingsIndex);

    const uint16_t outline = sel ? uiPillOutline(pet.type) : TFT_DARKGREY;
    const uint16_t fill = sel ? uiPillFillSelected(pet.type) : TFT_BLACK;
    const uint16_t textCol = sel ? TFT_WHITE : TFT_LIGHTGREY;

    spr.fillRoundRect(boxX, y, boxW, itemH, radius, fill);
    spr.drawRoundRect(boxX, y, boxW, itemH, radius, outline);

    const int th = spr.fontHeight();
    const int ty = y + (itemH - th) / 2;

    spr.setTextColor(textCol, fill);

    const char *label = labelsStatic[i];
    if (i == 1)
      label = volumeLine;
    spr.drawString(label, boxX + 10, ty);
  }

  spr.setTextFont(1);
  spr.setTextSize(1);
  spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  spr.setTextDatum(TL_DATUM);

  const int arrowX = boxX + boxW + 6;
  const int arrowUpY = startY - 2;
  const int arrowDownY = startY + totalH - 10;

  if (start > 0)
    spr.drawString("^", arrowX, arrowUpY);
  if (start + visCount < totalItems)
    spr.drawString("v", arrowX, arrowDownY);

  spr.setTextDatum(TL_DATUM);
}

static void drawNewPetConfirmOverlay()
{
  const uint16_t modalOutline = uiModalOutline(pet.type);

  const int pad = 10;
  const int boxW = screenW - (pad * 2);
  const int boxH = 82;
  const int x = pad;
  const int y = (screenH - boxH) / 2;

  spr.fillRoundRect(x, y, boxW, boxH, 8, TFT_BLACK);
  spr.drawRoundRect(x, y, boxW, boxH, 8, modalOutline);

  spr.setTextDatum(TC_DATUM);
  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.drawString("Store current pet first?", screenW / 2, y + 8);

  spr.setTextFont(1);
  spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  spr.drawString("Save slot will be overwritten", screenW / 2, y + 28);
  spr.drawString("Would you like to store your pet?", screenW / 2, y + 40);

  const int pillY = y + 48;
  const int pillH = 22;
  const int gap = 10;

  spr.setTextFont(2);
  spr.setTextSize(1);

  const char *yesLabel = "YES";
  const char *noLabel = "NO";

  const int padX = 14;
  const int yesW = spr.textWidth(yesLabel) + padX;
  const int noW = spr.textWidth(noLabel) + padX;
  const int totalW = yesW + gap + noW;
  const int startX = (screenW - totalW) / 2;

  const bool yesSel = (UiSettingsPages::GameNewPetConfirmIndex() == 0);
  const bool noSel = (UiSettingsPages::GameNewPetConfirmIndex() == 1);

  const uint16_t selFill = uiPillFillSelected(pet.type);
  const uint16_t selOut = uiPillOutline(pet.type);

  const uint16_t yesFill = yesSel ? selFill : TFT_BLACK;
  const uint16_t noFill = noSel ? selFill : TFT_BLACK;
  const uint16_t yesOut = yesSel ? selOut : TFT_DARKGREY;
  const uint16_t noOut = noSel ? selOut : TFT_DARKGREY;

  spr.fillRoundRect(startX, pillY, yesW, pillH, 8, yesFill);
  spr.drawRoundRect(startX, pillY, yesW, pillH, 8, yesOut);

  spr.fillRoundRect(startX + yesW + gap, pillY, noW, pillH, 8, noFill);
  spr.drawRoundRect(startX + yesW + gap, pillY, noW, pillH, 8, noOut);

  spr.setTextDatum(MC_DATUM);
  spr.setTextColor(TFT_WHITE, yesFill);
  spr.drawString(yesLabel, startX + (yesW / 2), pillY + (pillH / 2));

  spr.setTextColor(TFT_WHITE, noFill);
  spr.drawString(noLabel, startX + yesW + gap + (noW / 2), pillY + (pillH / 2));

  spr.setTextFont(1);
  spr.setTextSize(1);
  spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  spr.setTextDatum(BC_DATUM);
  spr.drawString("ENTER: Continue   MENU/ESC: Cancel", screenW / 2, y + boxH - 6);
  spr.setTextDatum(TL_DATUM);
}

static void drawPetSettingsMenu()
{
  drawNonPetTabBackground();
  drawTopBar();

  const int contentY = TOP_BAR_H;
  const int contentH = SCREEN_H - TOP_BAR_H;
  const int contentBottom = contentY + contentH;
  (0, contentY, SCREEN_W, contentH, TFT_BLACK);

  const char *renameLine = "Rename Pet";
  const char *backupLine = "Backup Current Pet";
  const char *restoreLine = "Restore From Backup";
  const char *newPetLine = "New Pet";

  const char *labels[] = {renameLine, backupLine, restoreLine, newPetLine};
  const int totalItems = 4;

  g_app.petSettingsIndex = clampi(g_app.petSettingsIndex, 0, totalItems - 1);

  constexpr int MAX_VISIBLE = 4;
  int start = 0, visCount = 0;
  listWindow(totalItems, g_app.petSettingsIndex, MAX_VISIBLE, start, visCount);

  const int itemH = 22;
  const int gap = 6;

  const int totalH = visCount * itemH + (visCount - 1) * gap;
  const int startY = contentY + (contentH - totalH) / 2;

  const int boxW = (SCREEN_W * 3) / 4;
  const int boxX = (SCREEN_W - boxW) / 2;
  const int radius = 10;

  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextDatum(TL_DATUM);

  for (int row = 0; row < visCount; row++)
  {
    const int i = start + row;
    const int y = startY + row * (itemH + gap);
    const bool sel = (i == g_app.petSettingsIndex);

    const uint16_t outline = sel ? uiPillOutline(pet.type) : TFT_DARKGREY;
    const uint16_t fill = sel ? uiPillFillSelected(pet.type) : TFT_BLACK;
    const uint16_t textCol = sel ? TFT_WHITE : TFT_LIGHTGREY;

    spr.fillRoundRect(boxX, y, boxW, itemH, radius, fill);
    spr.drawRoundRect(boxX, y, boxW, itemH, radius, outline);

    const int th = spr.fontHeight();
    const int ty = y + (itemH - th) / 2;

    spr.setTextColor(textCol, fill);
    spr.drawString(labels[i], boxX + 10, ty);
  }

  spr.setTextFont(1);
  spr.setTextSize(1);
  spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  spr.setTextDatum(TL_DATUM);

  const int arrowX = boxX + boxW + 6;
  const int arrowUpY = startY - 2;
  const int arrowDownY = startY + totalH - 10;

  if (start > 0)
    spr.drawString("^", arrowX, arrowUpY);
  if (start + visCount < totalItems)
    spr.drawString("v", arrowX, arrowDownY);

  spr.setTextDatum(TL_DATUM);
}

static void drawGameOptionsMenu()
{
  drawNonPetTabBackground();
  drawTopBar();

  const int contentY = TOP_BAR_H;
  const int contentH = SCREEN_H - TOP_BAR_H;
  const int contentBottom = contentY + contentH;

  char decayLine[32];
  snprintf(decayLine, sizeof(decayLine), "Decay Mode: %s", decayModeToText(saveManagerGetDecayMode()));

  char deathLine[32];
  snprintf(deathLine, sizeof(deathLine), "Pet Death: %s", petDeathEnabled ? "ON" : "OFF");

  char ledLine[32];
  snprintf(ledLine, sizeof(ledLine), "LED Alerts: %s", ledAlertsEnabled ? "ON" : "OFF");

  const char *labels[] = {decayLine, deathLine, ledLine};
  const int totalItems = 3;

  g_app.gameOptionsIndex = clampi(g_app.gameOptionsIndex, 0, totalItems - 1);

  constexpr int MAX_VISIBLE = 4;
  int start = 0, visCount = 0;
  listWindow(totalItems, g_app.gameOptionsIndex, MAX_VISIBLE, start, visCount);

  const int itemH = 22;
  const int gap = 6;

  const int totalH2 = visCount * itemH + (visCount - 1) * gap;
  const int startY = contentY + (contentH - totalH2) / 2;

  const int boxW = (SCREEN_W * 3) / 4;
  const int boxX = (SCREEN_W - boxW) / 2;
  const int radius = 10;

  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextDatum(TL_DATUM);

  for (int row = 0; row < visCount; row++)
  {
    const int i = start + row;
    const int y = startY + row * (itemH + gap);
    const bool sel = (i == g_app.gameOptionsIndex);

    const uint16_t outline = sel ? uiPillOutline(pet.type) : TFT_DARKGREY;
    const uint16_t fill = sel ? uiPillFillSelected(pet.type) : TFT_BLACK;
    const uint16_t textCol = sel ? TFT_WHITE : TFT_LIGHTGREY;

    spr.fillRoundRect(boxX, y, boxW, itemH, radius, fill);
    spr.drawRoundRect(boxX, y, boxW, itemH, radius, outline);

    const int th = spr.fontHeight();
    const int ty = y + (itemH - th) / 2;

    spr.setTextColor(textCol, fill);
    spr.drawString(labels[i], boxX + 10, ty);
  }
  if (UiSettingsPages::GameNewPetConfirmActive())

  {
    drawNewPetConfirmOverlay();
  }
}

static void drawAutoScreenPickerMenu()
{
  drawTopBar();

  const int contentY = TOP_BAR_H;
  const int footerH = 14; // reserve room for bottom help text
  const int contentH = SCREEN_H - TOP_BAR_H - footerH;
  const int contentBottom = contentY + contentH;

  spr.fillRect(0, contentY, SCREEN_W, SCREEN_H - contentY, TFT_BLACK);

  const char *choices[] = {"5 minutes", "30 minutes", "1 hour", "Off"};
  const int kCount = 4;

  g_app.autoScreenIndex = clampi(g_app.autoScreenIndex, 0, kCount - 1);

  constexpr int MAX_VISIBLE = 4;
  int start = 0, visCount = 0;
  listWindow(kCount, g_app.autoScreenIndex, MAX_VISIBLE, start, visCount);

  int itemH = 22;
  int gap = 6;

  int totalH = visCount * itemH + (visCount - 1) * gap;
  if (totalH > contentH)
  {
    itemH = 20;
    gap = 5;
    totalH = visCount * itemH + (visCount - 1) * gap;
  }

  const int bottomPad = 6; // small breathing room above screen bottom
  int startY = contentBottom - totalH - bottomPad;
  startY = clampi(startY, contentY + 18, contentBottom - totalH);

  const int boxW = (SCREEN_W * 3) / 4;
  const int boxX = (SCREEN_W - boxW) / 2;
  const int radius = 10;

  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextDatum(TL_DATUM);

  for (int row = 0; row < visCount; row++)
  {
    const int i = start + row;
    const int y = startY + row * (itemH + gap);
    const bool sel = (i == g_app.autoScreenIndex);

    const uint16_t outline = sel ? uiPillOutline(pet.type) : TFT_DARKGREY;
    const uint16_t fill = sel ? uiPillFillSelected(pet.type) : TFT_BLACK;
    const uint16_t textCol = sel ? TFT_WHITE : TFT_LIGHTGREY;

    spr.fillRoundRect(boxX, y, boxW, itemH, radius, fill);
    spr.drawRoundRect(boxX, y, boxW, itemH, radius, outline);

    const int th = spr.fontHeight();
    const int ty = y + (itemH - th) / 2;

    spr.setTextColor(textCol, fill);
    spr.drawString(choices[i], boxX + 10, ty);
  }
}

static void drawDecayModePickerMenu()
{
  drawTopBar();

  const int contentY = TOP_BAR_H;
  const int contentH = SCREEN_H - TOP_BAR_H;

  spr.fillRect(0, contentY, SCREEN_W, contentH, TFT_BLACK);

  spr.setTextDatum(TC_DATUM);
  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.drawString("Decay Mode", SCREEN_W / 2, contentY + 10);

  static const char *modes[] = {"SUPER SLOW", "SLOW", "NORMAL", "FAST", "SUPER FAST", "INSANE"};
  const int totalItems = 6;

  g_app.decayModeIndex = clampi(g_app.decayModeIndex, 0, totalItems - 1);
  constexpr int MAX_VISIBLE = 3;
  int start = 0, visCount = 0;
  listWindow(totalItems, g_app.decayModeIndex, MAX_VISIBLE, start, visCount);
  int itemH = 22;
  int gap = 6;

  int listTopY = contentY + 26;
  int listAreaH = contentH - 26;

  int totalH = visCount * itemH + (visCount - 1) * gap;
  int startY = listTopY + (listAreaH - totalH) / 2;
  startY = clampi(startY, listTopY, (contentY + contentH) - totalH - 16);

  const int boxW = (SCREEN_W * 3) / 4;
  const int boxX = (SCREEN_W - boxW) / 2;
  const int radius = 10;

  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextDatum(TL_DATUM);

  for (int row = 0; row < visCount; row++)
  {
    const int idx = start + row;
    const int y = startY + row * (itemH + gap);
    const bool sel = (idx == g_app.decayModeIndex);

    const uint16_t outline = sel ? uiPillOutline(pet.type) : TFT_DARKGREY;
    const uint16_t fill = sel ? uiPillFillSelected(pet.type) : TFT_BLACK;
    const uint16_t textCol = sel ? TFT_WHITE : TFT_LIGHTGREY;

    spr.fillRoundRect(boxX, y, boxW, itemH, radius, fill);
    spr.drawRoundRect(boxX, y, boxW, itemH, radius, outline);

    const int th = spr.fontHeight();
    const int ty = y + (itemH - th) / 2;

    spr.setTextColor(textCol, fill);
    spr.drawString(modes[idx], boxX + 10, ty);
  }
}

static void drawScreenSettingsMenu()
{
  drawNonPetTabBackground();
  drawTopBar();

  const int contentY = TOP_BAR_H;
  const int contentH = SCREEN_H - TOP_BAR_H;
  const int contentBottom = contentY + contentH;

  char bLine[28];
  snprintf(bLine, sizeof(bLine), "Brightness: %s", brightnessToText(brightnessLevel));

  char aLine[28];
  snprintf(aLine, sizeof(aLine), "Auto Screen: %s", autoScreenToText((uint8_t)autoScreenTimeoutSel));

  char sLine[32];
  snprintf(sLine, sizeof(sLine), "Shake to Wake: %s", motionShakeSensitivityToText(motionGetShakeSensitivity()));

  const char *labels[] = {bLine, aLine, sLine};
  const int totalItems = 3;

  g_app.screenSettingsIndex = clampi(g_app.screenSettingsIndex, 0, totalItems - 1);

  constexpr int MAX_VISIBLE = 3;
  int start = 0, visCount = 0;
  listWindow(totalItems, g_app.screenSettingsIndex, MAX_VISIBLE, start, visCount);

  int itemH = 22;
  int gap = 6;

  int totalH = visCount * itemH + (visCount - 1) * gap;
  int startY = contentY + (contentH - totalH) / 2;

  const int boxW = (SCREEN_W * 3) / 4;
  const int boxX = (SCREEN_W - boxW) / 2;
  const int radius = 10;

  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextDatum(TL_DATUM);

  for (int row = 0; row < visCount; row++)
  {
    const int i = start + row;
    const int y = startY + row * (itemH + gap);
    const bool sel = (i == g_app.screenSettingsIndex);

    const uint16_t outline = sel ? uiPillOutline(pet.type) : TFT_DARKGREY;
    const uint16_t fill = sel ? uiPillFillSelected(pet.type) : TFT_BLACK;
    const uint16_t textCol = sel ? TFT_WHITE : TFT_LIGHTGREY;

    spr.fillRoundRect(boxX, y, boxW, itemH, radius, fill);
    spr.drawRoundRect(boxX, y, boxW, itemH, radius, outline);

    const int th = spr.fontHeight();
    const int ty = y + (itemH - th) / 2;

    spr.setTextColor(textCol, fill);
    spr.drawString(labels[i], boxX + 10, ty);
  }
}

static void drawWifiSettingsMenu()
{
  drawNonPetTabBackground();
  drawTopBar();

  const int contentY = TOP_BAR_H;
  const int contentH = SCREEN_H - TOP_BAR_H;
  const int contentBottom = contentY + contentH;

  const int totalItems = UiSettingsMenu::WifiItemCount();
  g_wifi.wifiSettingsIndex = clampi(g_wifi.wifiSettingsIndex, 0, totalItems - 1);

  constexpr int MAX_VISIBLE = 4;
  int start = 0, visCount = 0;
  listWindow(totalItems, g_wifi.wifiSettingsIndex, MAX_VISIBLE, start, visCount);

  int itemH = 22;
  int gap = 6;

  int totalH = visCount * itemH + (visCount - 1) * gap;
  int startY = contentY + (contentH - totalH) / 2;

  const int boxW = (SCREEN_W * 3) / 4;
  const int boxX = (SCREEN_W - boxW) / 2;
  const int radius = 10;

  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextDatum(TL_DATUM);

  for (int row = 0; row < visCount; row++)
  {
    const int i = start + row;
    const int y = startY + row * (itemH + gap);
    const bool sel = (i == g_wifi.wifiSettingsIndex);

    const uint16_t outline = sel ? uiPillOutline(pet.type) : TFT_DARKGREY;
    const uint16_t fill = sel ? uiPillFillSelected(pet.type) : TFT_BLACK;
    const uint16_t textCol = sel ? TFT_WHITE : TFT_LIGHTGREY;

    spr.fillRoundRect(boxX, y, boxW, itemH, radius, fill);
    spr.drawRoundRect(boxX, y, boxW, itemH, radius, outline);

    const int th = spr.fontHeight();
    const int ty = y + (itemH - th) / 2;

    const char *label = UiSettingsMenu::WifiItemLabel(i);

    char valueBuf[40];
    valueBuf[0] = '\0';

    if (strcmp(label, "WiFi") == 0)
    {
      snprintf(valueBuf, sizeof(valueBuf), "WiFi: %s", wifiIsEnabled() ? "ON" : "OFF");
      label = valueBuf;
    }
    else if (strcmp(label, "Time Zone") == 0)
    {
      snprintf(valueBuf, sizeof(valueBuf), "Time Zone: %s", tzName(tzIndex));
      label = valueBuf;
    }
    else if (strcmp(label, "OTA Channel") == 0)
    {
      snprintf(valueBuf, sizeof(valueBuf), "OTA Channel: %s",
               ((AssetOtaChannel)assetOtaGetConfig().channel == AssetOtaChannel::DEV) ? "Dev" : "Public");
      label = valueBuf;
    }

    spr.setTextColor(textCol, fill);
    spr.drawString(label, boxX + 10, ty);
  }
}

static void drawFactoryResetConfirmOverlay()
{
  spr.fillRect(0, 0, screenW, screenH, TFT_BLACK);

  const uint16_t modalOutline = uiModalOutline(pet.type);

  const int pad = 10;
  const int boxW = screenW - (pad * 2);
  const int boxH = 74;
  const int x = pad;
  const int y = (screenH - boxH) / 2;

  spr.fillRoundRect(x, y, boxW, boxH, 8, TFT_BLACK);
  spr.drawRoundRect(x, y, boxW, boxH, 8, modalOutline);

  spr.setTextDatum(TC_DATUM);
  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.drawString("Factory Reset?", screenW / 2, y + 8);

  spr.setTextFont(1);
  spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  spr.drawString("This will wipe your save.", screenW / 2, y + 28);

  const int pillY = y + 38;
  const int pillH = 22;
  const int gap = 10;

  spr.setTextFont(2);
  spr.setTextSize(1);

  const char *noLabel = "NO";
  const char *yesLabel = "YES";

  const int padX = 14;
  const int noW = spr.textWidth(noLabel) + padX;
  const int yesW = spr.textWidth(yesLabel) + padX;
  const int totalW = noW + gap + yesW;

  const int startX = (screenW - totalW) / 2;

  const bool noSel = (g_factoryReset.confirmIndex == 0);
  const bool yesSel = (g_factoryReset.confirmIndex == 1);

  const uint16_t selFill = uiPillFillSelected(pet.type);
  const uint16_t selOut = uiPillOutline(pet.type);

  const uint16_t noFill = noSel ? selFill : TFT_BLACK;
  const uint16_t yesFill = yesSel ? selFill : TFT_BLACK;

  const uint16_t noOut = noSel ? selOut : TFT_DARKGREY;
  const uint16_t yesOut = yesSel ? selOut : TFT_DARKGREY;

  spr.fillRoundRect(startX, pillY, noW, pillH, 8, noFill);
  spr.drawRoundRect(startX, pillY, noW, pillH, 8, noOut);

  spr.fillRoundRect(startX + noW + gap, pillY, yesW, pillH, 8, yesFill);
  spr.drawRoundRect(startX + noW + gap, pillY, yesW, pillH, 8, yesOut);

  spr.setTextDatum(MC_DATUM);
  spr.setTextColor(TFT_WHITE, noFill);
  spr.drawString(noLabel, startX + (noW / 2), pillY + (pillH / 2));

  spr.setTextColor(TFT_WHITE, yesFill);
  spr.drawString(yesLabel, startX + noW + gap + (yesW / 2), pillY + (pillH / 2));

  spr.setTextFont(1);
  spr.setTextSize(1);
  spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  spr.setTextDatum(BC_DATUM);
  spr.drawString("ENTER: Continue   MENU/ESC: Cancel", screenW / 2, y + boxH - 6);
  spr.setTextDatum(TL_DATUM);
}

static void drawSystemSettingsMenu()
{
  drawNonPetTabBackground();
  drawTopBar();

  const int contentY = TOP_BAR_H;
  const int contentH = SCREEN_H - TOP_BAR_H;
  const int contentBottom = contentY + contentH;

  const char *labels[] = {"Set Time", "Factory Reset", "WiFi Settings >"};
  const int totalItems = 3;

  g_app.systemSettingsIndex = clampi(g_app.systemSettingsIndex, 0, totalItems - 1);

  constexpr int MAX_VISIBLE = 3;
  int start = 0, visCount = 0;
  listWindow(totalItems, g_app.systemSettingsIndex, MAX_VISIBLE, start, visCount);

  const int itemH = 22;
  const int gap = 6;

  const int totalH = visCount * itemH + (visCount - 1) * gap;
  const int startY = contentY + (contentH - totalH) / 2;

  const int boxW = (SCREEN_W * 3) / 4;
  const int boxX = (SCREEN_W - boxW) / 2;
  const int radius = 10;

  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextDatum(TL_DATUM);

  for (int row = 0; row < visCount; row++)
  {
    const int i = start + row;
    const int y = startY + row * (itemH + gap);
    const bool sel = (i == g_app.systemSettingsIndex);

    const uint16_t outline = sel ? uiPillOutline(pet.type) : TFT_DARKGREY;
    const uint16_t fill = sel ? uiPillFillSelected(pet.type) : TFT_BLACK;
    const uint16_t textCol = sel ? TFT_WHITE : TFT_LIGHTGREY;

    spr.fillRoundRect(boxX, y, boxW, itemH, radius, fill);
    spr.drawRoundRect(boxX, y, boxW, itemH, radius, outline);

    const int th = spr.fontHeight();
    const int ty = y + (itemH - th) / 2;

    spr.setTextColor(textCol, fill);
    spr.drawString(labels[i], boxX + 10, ty);
  }

  if (g_factoryReset.confirmActive)
  {
    drawFactoryResetConfirmOverlay();
  }
}

static void drawPlaceholderMenu(const char *title)
{
  drawTopBar();

  const int contentY = TOP_BAR_H;
  const int contentH = SCREEN_H - TOP_BAR_H;

  spr.fillRect(0, contentY, SCREEN_W, contentH, TFT_BLACK);

  spr.setTextDatum(MC_DATUM);
  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.drawString(title, SCREEN_W / 2, contentY + 30);

  spr.setTextFont(1);
  spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  spr.drawString("(Coming Soon)", SCREEN_W / 2, contentY + 52);

  spr.setTextDatum(BC_DATUM);
  spr.drawString("MENU: Back", SCREEN_W / 2, SCREEN_H - 6);

  spr.setTextDatum(TL_DATUM);
}

static void drawAssetOtaConfirmOverlay()
{
  const uint16_t modalOutline = uiModalOutline(pet.type);

  const int pad = 10;
  const int boxW = screenW - (pad * 2);
  const int boxH = 74;
  const int x = pad;
  const int y = (screenH - boxH) / 2;

  spr.fillRoundRect(x, y, boxW, boxH, 8, TFT_BLACK);
  spr.drawRoundRect(x, y, boxW, boxH, 8, modalOutline);

  spr.setTextDatum(TC_DATUM);
  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.drawString("Asset OTA", screenW / 2, y + 8);

  spr.setTextFont(1);
  spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  spr.drawString("The system will reboot and", screenW / 2, y + 28);
  spr.drawString("check for new/missing assets.", screenW / 2, y + 40);

  spr.setTextDatum(BC_DATUM);
  spr.drawString("ENTER: Continue   MENU/ESC: Cancel", screenW / 2, y + boxH - 6);

  spr.setTextDatum(TL_DATUM);
}

static void drawCreditsScreen()
{
  if (!isScreenOn())
    return;

  bool ok = false;
  if (g_sdReady)
    ok = sprDrawJpgFromSD(PATH_BG_SPLASH, 0, 0);
  if (!ok)
    spr.fillRect(0, 0, SCREEN_W, SCREEN_H, TFT_BLACK);

  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextDatum(TC_DATUM);

  const int LINE_H = 16;
  const int TIGHT_H = 14;

  const int blockTopY = (SCREEN_H / 2) + 8;

  const int yCreated = blockTopY;
  const int yAaron = yCreated + LINE_H;
  const int yVersion = yAaron + LINE_H;
  const int yAssets = yVersion + TIGHT_H;

  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.drawString("Created By:", SCREEN_W / 2, yCreated);
  spr.drawString("Aaron & Finley Ayers", SCREEN_W / 2, yAaron);

  uint16_t versionCol = TFT_DARKGREY;
#if defined(PUBLIC_BUILD) && PUBLIC_BUILD
  versionCol = TFT_GREEN;
#else
  versionCol = TFT_RED;
#endif

  spr.setTextColor(versionCol, TFT_BLACK);

  char verLine[48];
  snprintf(verLine, sizeof(verLine), "Version %s", RH_VERSION_STRING);
  spr.drawString(verLine, SCREEN_W / 2, yVersion);

  const char *assetVer = assetOtaInstalledVersion();
  char assetLine[48];
  if (assetVer && assetVer[0])
    snprintf(assetLine, sizeof(assetLine), "Asset OTA: %s", assetVer);
  else
    snprintf(assetLine, sizeof(assetLine), "Asset OTA: none installed");

  spr.drawString(assetLine, SCREEN_W / 2, yAssets);

  spr.setTextDatum(TL_DATUM);
}

static const char *basenameFromUrl(const char *url)
{
  if (!url || !url[0])
    return "(none)";
  const char *slash = strrchr(url, '/');
  return (slash && slash[1]) ? slash + 1 : url;
}

static void drawSystemStatusMenu()
{
  drawNonPetTabBackground();
  drawTopBar();

  const int contentY = TOP_BAR_H;
  const int contentH = SCREEN_H - TOP_BAR_H;
  const int contentBottom = contentY + contentH;

  const AssetOtaConfig &cfg = assetOtaGetConfig();
  const AssetOtaChannel ch = (AssetOtaChannel)cfg.channel;
  const char *manifestUrl = assetOtaManifestUrlForChannel(ch);
  const char *assetVer = assetOtaInstalledVersion();
  const char *ssid = wifiConsoleSsid();
  const char *ip = wifiConsoleIpString();

  char uptimeBuf[32];
  const uint32_t upMs = millis() - bootTime;
  const uint32_t upSec = upMs / 1000UL;
  const uint32_t upMin = upSec / 60UL;
  const uint32_t remSec = upSec % 60UL;
  snprintf(uptimeBuf, sizeof(uptimeBuf), "%lum %lus", (unsigned long)upMin, (unsigned long)remSec);

  char batteryBuf[32];
  snprintf(batteryBuf, sizeof(batteryBuf), "%d%% %s", batteryPercent, usbPowered ? "(USB)" : "");

  char buildBuf[16];
  char saveVerBuf[16];
  snprintf(saveVerBuf, sizeof(saveVerBuf), "%u", (unsigned)SAVE_VERSION);

#if defined(PUBLIC_BUILD) && PUBLIC_BUILD
  snprintf(buildBuf, sizeof(buildBuf), "PUBLIC");
#else
  snprintf(buildBuf, sizeof(buildBuf), "DEV");
#endif

  char otaChannelBuf[16];
  snprintf(otaChannelBuf, sizeof(otaChannelBuf), "%s", (ch == AssetOtaChannel::DEV) ? "DEV" : "PUBLIC");

  char wifiStateBuf[24];
  snprintf(wifiStateBuf, sizeof(wifiStateBuf), "%s/%s", wifiIsEnabled() ? "ON" : "OFF",
           wifiIsConnectedNow() ? "LINK" : "NO-LINK");

  char assetBuf[24];
  snprintf(assetBuf, sizeof(assetBuf), "%s", (assetVer && assetVer[0]) ? assetVer : "none");

  char heapFreeBuf[24];
  char heapLargestBuf[24];
  char psramSizeBuf[24];
  char psramFreeBuf[24];

  snprintf(heapFreeBuf, sizeof(heapFreeBuf), "%u", (unsigned)ESP.getFreeHeap());

  snprintf(heapLargestBuf, sizeof(heapLargestBuf), "%u", (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

  snprintf(psramSizeBuf, sizeof(psramSizeBuf), "%u", (unsigned)ESP.getPsramSize());

  snprintf(psramFreeBuf, sizeof(psramFreeBuf), "%u", (unsigned)ESP.getFreePsram());

  char localManifestBuf[8];
  snprintf(localManifestBuf, sizeof(localManifestBuf), "%s",
           SD.exists("/raising_hell/assets/manifest_local.json") ? "YES" : "NO");

  char sdBuf[16];
  snprintf(sdBuf, sizeof(sdBuf), "%s", g_sdReady ? "READY" : "MISSING");

  const char *lines[] = {
      "Build",
      buildBuf,
      "Firmware",
      RH_VERSION_STRING,
      "Save Ver",
      saveVerBuf,
      "Uptime",
      uptimeBuf,
      "Battery",
      batteryBuf,
      "Heap Free",
      heapFreeBuf,
      "Heap Largest",
      heapLargestBuf,
      "PSRAM Size",
      psramSizeBuf,
      "PSRAM Free",
      psramFreeBuf,
      "SD",
      sdBuf,
      "WiFi",
      wifiStateBuf,
      "SSID",
      (ssid && ssid[0]) ? ssid : "(none)",
      "IP",
      (ip && ip[0]) ? ip : "(none)",
      "OTA Channel",
      otaChannelBuf,
      "Manifest",
      basenameFromUrl(manifestUrl),
      "Assets",
      assetBuf,
      "Local Manifest",
      localManifestBuf,
      "OTA Status",
      assetOtaStatusString(),
      "OTA Error",
      assetOtaLastErrorString(),
  };

  const int totalLines = (int)(sizeof(lines) / sizeof(lines[0]));
  const int visibleLines = 10;
  g_app.statusScreenIndex = clampi(g_app.statusScreenIndex, 0, totalLines - visibleLines);

  const int start = g_app.statusScreenIndex;
  const int leftX = 8;
  const int valX = 110;
  const int lineH = 18;
  int y = contentY + 6;

  spr.setTextDatum(TL_DATUM);
  spr.setTextFont(2);
  spr.setTextSize(1);

  const int pairCount = totalLines / 2;

  for (int i = 0; i < visibleLines && (start + i) < pairCount; ++i)
  {
    const int pairIdx = start + i;

    const char *key = lines[pairIdx * 2];
    const char *val = lines[pairIdx * 2 + 1];

    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    spr.drawString(key, leftX, y);

    spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    spr.drawString(val, valX, y);

    y += lineH;
  }

  spr.setTextColor(TFT_DARKGREY, TFT_BLACK);
  if (start > 0)
    spr.drawString("^", SCREEN_W - 10, contentY + 2);
  if ((start + visibleLines) < totalLines)
    spr.drawString("v", SCREEN_W - 10, SCREEN_H - 14);
}

void drawSettingsMenu()
{
  switch (g_settingsFlow.settingsPage)
  {
  default:
  case SettingsPage::TOP:
    drawSettingsTopMenu();
    break;
  case SettingsPage::PET:
    drawPetSettingsMenu();
    break;
  case SettingsPage::SCREEN:
    drawScreenSettingsMenu();
    break;
  case SettingsPage::SYSTEM:
    drawSystemSettingsMenu();
    break;
  case SettingsPage::GAME:
    drawGameOptionsMenu();
    break;
  case SettingsPage::DECAY_MODE:
    drawDecayModePickerMenu();
    break;
  case SettingsPage::WIFI:
    drawWifiSettingsMenu();
    break;
  case SettingsPage::CONSOLE:
    drawPlaceholderMenu("Console");
    break;
  case SettingsPage::STATUS:
    drawSystemStatusMenu();
    break;
  case SettingsPage::CREDITS:
    drawCreditsScreen();
    break;
  case SettingsPage::AUTO_SCREEN:
    drawAutoScreenPickerMenu();
    break;
  }
  if (UiSettingsPages::GameNewPetConfirmActive())
  {
    drawNewPetConfirmOverlay();
  }
}

// ============================================================================
// Shop / Sleep / Inventory / Feed
// ============================================================================
// Shop list index -> item type (0..SHOP_ITEM_COUNT-1). SHOP_ITEM_COUNT is Exit.
static ItemType shopItemTypeForIndexLocal(int idx)
{
  switch (idx)
  {
  case 0:
    return ITEM_SOUL_FOOD;
  case 1:
    return ITEM_CURSED_RELIC;
  case 2:
    return ITEM_DEMON_BONE;
  case 3:
    return ITEM_RITUAL_CHALK;
  case 4:
    return ITEM_ELDRITCH_EYE;
  default:
    return ITEM_NONE;
  }
}

// ---- tiny bars (decls) ----
static void drawTinyBar(int x, int y, int w, int h, uint16_t fill, uint16_t outline, int value01_100,
                        const char *label);

static void drawTinyBar(int x, int y, int w, int h, uint16_t fill, uint16_t outline, int value01_100);

static void drawTinyBarV(int x, int y, int w, int h, uint16_t fill, uint16_t outline, int value01_100);

void drawShopScreen()
{
  drawNonPetTabBackground();
  drawTopBar();
  drawTabBar();

  const int contentY = TOP_BAR_H;
  const int contentH = SCREEN_H - TOP_BAR_H - TAB_BAR_H;
  const int contentBottom = contentY + contentH;

  const int totalItems = SHOP_ITEM_COUNT;

  if (g_app.shopIndex < 0)
    g_app.shopIndex = 0;
  if (g_app.shopIndex >= SHOP_ITEM_COUNT)
    g_app.shopIndex = SHOP_ITEM_COUNT - 1;

  // Windowing for visible rows
  constexpr int MAX_VISIBLE = 4; // shop list tends to look good with 4
  int start = 0, visCount = 0;
  listWindow(totalItems, g_app.shopIndex, MAX_VISIBLE, start, visCount);

  // Safety: never draw more rows than actually exist
  if (visCount < 0)
    visCount = 0;
  if (visCount > totalItems - start)
    visCount = totalItems - start;

  // ---------------------------------------------------------------------------
  // Layout: left list pills + right detail panel (image + price + effects)
  // ---------------------------------------------------------------------------
  const int margin = 6;
  const int gapLR = 8;

  const int listX = margin;
  const int listW = 118; // match Inventory pills width
  const int listRight = listX + listW;

  const int panelX = listRight + gapLR;
  const int panelW = SCREEN_W - panelX - margin;

  // List pill sizing
  int itemH = 22;
  int gapY = 6;

  int totalListH = visCount * itemH + (visCount - 1) * gapY;
  if (totalListH > contentH)
  {
    itemH = 20;
    gapY = 5;
    totalListH = visCount * itemH + (visCount - 1) * gapY;
  }

  int startY = contentY + (contentH - totalListH) / 2;
  startY = clampi(startY, contentY, contentBottom - totalListH);

  const int radius = 10;

  // Draw list pills (name only; no price text here)
  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextDatum(TL_DATUM);

  for (int row = 0; row < visCount; row++)
  {
    const int i = start + row;

    if (i < 0 || i >= totalItems)
      continue;

    const int y = startY + row * (itemH + gapY);
    const bool sel = (i == g_app.shopIndex);

    const uint16_t outline = sel ? uiPillOutline(pet.type) : TFT_DARKGREY;
    const uint16_t fill = sel ? uiPillFillSelected(pet.type) : TFT_BLACK;
    const uint16_t textCol = sel ? TFT_WHITE : TFT_LIGHTGREY;

    spr.fillRoundRect(listX, y, listW, itemH, radius, fill);
    spr.drawRoundRect(listX, y, listW, itemH, radius, outline);

    const ItemType t = availableItems[i].type;
    const char *itemName = g_app.inventory.getItemLabelForType(t);
    if (!itemName)
      itemName = "";

    spr.setTextFont(2);
    spr.setTextSize(1);
    spr.setTextDatum(TL_DATUM);

    const int th = spr.fontHeight();
    const int ty = y + (itemH - th) / 2;

    spr.setTextColor(textCol);
    spr.drawString(itemName, listX + 10, ty);
  }

  // Scroll arrows (to the right of the left list)
  spr.setTextFont(1);
  spr.setTextSize(1);
  spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  spr.setTextDatum(TL_DATUM);

  const int arrowX = listRight + 2;
  const int arrowUpY = startY - 2;
  const int arrowDownY = startY + totalListH - 10;

  if (start > 0)
    spr.drawString("^", arrowX, arrowUpY);
  if (start + visCount < totalItems)
    spr.drawString("v", arrowX, arrowDownY);

  // ---------------------------------------------------------------------------
  // Right detail panel for selected item
  // ---------------------------------------------------------------------------
  const int panelY = contentY + 6;
  const int panelH = contentH - 12;

  spr.fillRoundRect(panelX, panelY, panelW, panelH, 10, TFT_BLACK);
  spr.drawRoundRect(panelX, panelY, panelW, panelH, 10, TFT_DARKGREY);

  const int pad = 8;

  // Image pinned near the top of the panel
  const int imgW = 64;
  const int imgH = 64;
  const int imgX = panelX + pad;
  const int imgY = panelY + pad - 4;

  const ItemType selType = availableItems[g_app.shopIndex].type;

  const bool eldTheme = (pet.type == PET_ELDRITCH);

  const char *imgPath = nullptr;
  switch (selType)
  {
  case ITEM_SOUL_FOOD:
    imgPath = eldTheme ? PATH_SHOP_ELD_FOOD : PATH_SHOP_DEV_FOOD;
    break;
  case ITEM_CURSED_RELIC:
    imgPath = eldTheme ? PATH_SHOP_ELD_MOOD : PATH_SHOP_DEV_MOOD;
    break;
  case ITEM_DEMON_BONE:
    imgPath = eldTheme ? PATH_SHOP_ELD_REST : PATH_SHOP_DEV_REST;
    break;
  case ITEM_RITUAL_CHALK:
    imgPath = eldTheme ? PATH_SHOP_ELD_HEALTH : PATH_SHOP_DEV_HEALTH;
    break;
  case ITEM_ELDRITCH_EYE:
    imgPath = eldTheme ? PATH_SHOP_ELD_EVO : PATH_SHOP_DEV_EVO;
    break;
  default:
    imgPath = nullptr;
    break;
  }

  bool okImg = false;
  if (g_sdReady && imgPath)
    okImg = sprDrawPngFromSD(imgPath, imgX, imgY);
  if (!okImg)
  {
    spr.fillEllipse(imgX + imgW / 2, imgY + imgH / 2, imgW / 2, imgH / 2, TFT_WHITE);
    spr.drawEllipse(imgX + imgW / 2, imgY + imgH / 2, imgW / 2, imgH / 2, TFT_RED);
  }

  // Price (safe: shopIndex is guaranteed < SHOP_ITEM_COUNT here)
  const int cost = availableItems[g_app.shopIndex].price;
  char priceLine[16];
  snprintf(priceLine, sizeof(priceLine), "$%d", cost);

  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);

  spr.setTextDatum(TR_DATUM);
  const int priceX = panelX + panelW - 8;
  const int priceY = imgY + (imgH - spr.fontHeight()) / 2;
  spr.drawString(priceLine, priceX, priceY);
  spr.setTextDatum(TL_DATUM);

  // Effects at the bottom
  String eff;
  switch (selType)
  {
  case ITEM_SOUL_FOOD:
    eff = "-30 Hunger";
    break;
  case ITEM_CURSED_RELIC:
    eff = "+30 Mood";
    break;
  case ITEM_DEMON_BONE:
    eff = "+30 Energy";
    break;
  case ITEM_RITUAL_CHALK:
    eff = "+30 HP";
    break;
  case ITEM_ELDRITCH_EYE:
    eff = "Evolve Now";
    break;
  default:
    eff = "";
    break;
  }

  if (eff.length() > 0)
  {
    spr.setTextFont(2);
    spr.setTextSize(1);
    spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);

    spr.setTextDatum(BC_DATUM);
    const int effectsX = panelX + panelW / 2;
    const int effectsY = panelY + panelH - 2;
    spr.drawString(eff, effectsX, effectsY);
    spr.setTextDatum(TL_DATUM);
  }
}

void drawFeedMenu()
{
  drawNonPetTabBackground();
  drawTopBar();
  drawTabBar();

  const int contentY = TOP_BAR_H;
  const int contentH = SCREEN_H - TOP_BAR_H - TAB_BAR_H;
  const int contentBottom = contentY + contentH;

  const int totalItems = uiFeedMenuCount();

  g_app.feedMenuIndex = clampi(g_app.feedMenuIndex, 0, totalItems - 1);

  constexpr int MAX_VISIBLE = 3;
  int start = 0, visCount = 0;
  listWindow(totalItems, g_app.feedMenuIndex, MAX_VISIBLE, start, visCount);
  int itemH = 22;
  int gap = 6;

  int totalH = visCount * itemH + (visCount - 1) * gap;
  if (totalH > contentH)
  {
    itemH = 20;
    gap = 5;
    totalH = visCount * itemH + (visCount - 1) * gap;
  }

  const int boxW = (SCREEN_W * 3) / 4;
  const int boxX = (SCREEN_W - boxW) / 2;
  const int radius = 10;

  const int shiftDown = 14;
  const int meterH = 10;
  const int meterGap = 6;

  int startY = contentY + (contentH - totalH) / 2;
  startY = clampi(startY, contentY, contentBottom - totalH);
  startY += shiftDown;

  const int gapTop = startY - contentY;
  int meterY = contentY + (gapTop - meterH) / 2;
  if (meterY < contentY + 2)
    meterY = contentY + 2;

  if (meterY < contentY)
  {
    meterY = contentY;
    startY = meterY + meterH + meterGap;
  }

  if (startY + totalH > contentBottom)
  {
    startY = contentBottom - totalH;
    meterY = startY - meterGap - meterH;
    if (meterY < contentY)
      meterY = contentY;
  }

  const uint16_t colHunger = 0xF800;
  const int meterInset = 16;
  const int meterW = boxW - (meterInset * 2);
  const int meterX = boxX + (boxW - meterW) / 2;

  drawTinyBar(meterX, meterY, meterW, meterH, colHunger, colHunger, pet.hunger);

  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextDatum(TL_DATUM);

  for (int row = 0; row < visCount; row++)
  {
    int i = start + row;
    int y = startY + row * (itemH + gap);
    bool sel = (i == g_app.feedMenuIndex);

    uint16_t outline = sel ? uiPillOutline(pet.type) : TFT_DARKGREY;
    uint16_t fill = sel ? uiPillFillSelected(pet.type) : TFT_BLACK;
    uint16_t textCol = sel ? TFT_WHITE : TFT_LIGHTGREY;

    spr.fillRoundRect(boxX, y, boxW, itemH, radius, fill);
    spr.drawRoundRect(boxX, y, boxW, itemH, radius, outline);

    int cx = boxX + boxW / 2;
    int th = spr.fontHeight();
    int ty = y + (itemH - th) / 2;

    String line = uiFeedMenuLabel(i);
    if (i == 1)
    {
      const int SOUL_FOOD_HUNGER_GAIN = 20;
      int missing = 100 - pet.hunger;
      if (missing < 0)
        missing = 0;
      int needed = (missing + SOUL_FOOD_HUNGER_GAIN - 1) / SOUL_FOOD_HUNGER_GAIN;
      line += " (" + String(needed) + ")";
    }

    spr.setTextColor(textCol, fill);
    spr.drawCentreString(line.c_str(), cx, ty, 2);
  }

  spr.setTextDatum(TL_DATUM);
}

void drawSleepMenu()
{
  drawNonPetTabBackground();
  drawTopBar();
  drawTabBar();

  const int contentY = TOP_BAR_H;
  const int contentH = SCREEN_H - TOP_BAR_H - TAB_BAR_H;
  const int contentBottom = contentY + contentH;

  const int totalItems = uiSleepMenuCount();
  if (totalItems <= 0)
    return;

  sleepMenuIndex = clampi(sleepMenuIndex, 0, totalItems - 1);

  constexpr int MAX_VISIBLE = 3;
  int start = 0, visCount = 0;
  listWindow(totalItems, sleepMenuIndex, MAX_VISIBLE, start, visCount);

  int itemH = 22;
  int gap = 6;

  int totalH = visCount * itemH + (visCount - 1) * gap;
  if (totalH > contentH)
  {
    itemH = 20;
    gap = 5;
    totalH = visCount * itemH + (visCount - 1) * gap;
  }

  int startY = contentY + (contentH - totalH) / 2;
  startY = clampi(startY, contentY, contentBottom - totalH);

  const int boxW = (SCREEN_W * 3) / 4;
  const int boxX = (SCREEN_W - boxW) / 2;
  const int radius = 10;

  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextDatum(TL_DATUM);

  for (int row = 0; row < visCount; row++)
  {
    int i = start + row;
    int y = startY + row * (itemH + gap);
    bool sel = (i == sleepMenuIndex);

    uint16_t outline = sel ? uiPillOutline(pet.type) : TFT_DARKGREY;
    uint16_t fill = sel ? uiPillFillSelected(pet.type) : TFT_BLACK;
    uint16_t textCol = sel ? TFT_WHITE : TFT_LIGHTGREY;

    spr.fillRoundRect(boxX, y, boxW, itemH, radius, fill);
    spr.drawRoundRect(boxX, y, boxW, itemH, radius, outline);

    int cx = boxX + boxW / 2;
    int th = spr.fontHeight();
    int ty = y + (itemH - th) / 2;

    spr.setTextColor(textCol, fill);
    spr.drawCentreString(uiSleepMenuLabel(i), cx, ty, 2);
  }

  spr.setTextDatum(TL_DATUM);
}

static void drawInventoryLeftStatsPanel(int contentY, int contentH, int boxX)
{
  const int panelX = 2;
  const int panelW = boxX - panelX - 2;
  if (panelW < 12 || contentH < 30)
    return;

  const int gapY = 6;
  int barH = (contentH - 2 * gapY) / 3;
  barH = clampi(barH, 14, 20);

  const int totalBarsH = (3 * barH) + (2 * gapY);
  int y0 = contentY + (contentH - totalBarsH) / 2;
  if (y0 < contentY)
    y0 = contentY;

  const int barPadX = 7;
  const int barX = panelX + barPadX;
  const int barW = panelW - (barPadX * 2);
  if (barW < 8)
    return;

  const uint16_t colHunger = 0xF800;
  const uint16_t colMood = 0x001F;
  const uint16_t colEnergy = 0x07E0;

  const int rowGap = 10;
  const int rowH = barH + rowGap;

  const int yHunger = y0 + 0 * rowH;
  const int yMood = y0 + 1 * rowH;
  const int yRest = y0 + 2 * rowH;

  drawTinyBar(barX, yHunger, barW, barH, colHunger, colHunger, pet.hunger, "Hunger");
  drawTinyBar(barX, yMood, barW, barH, colMood, colMood, pet.happiness, "Mood");
  drawTinyBar(barX, yRest, barW, barH, colEnergy, colEnergy, pet.energy, "Rest");

  spr.setTextDatum(TL_DATUM);
}

void drawInventoryMenu()
{
  drawNonPetTabBackground();
  drawTopBar();
  drawTabBar();

  const int contentY = TOP_BAR_H;
  const int contentH = SCREEN_H - TOP_BAR_H - TAB_BAR_H;
  const int contentBottom = contentY + contentH;

  const int totalItems = g_app.inventory.countItems();
  const bool empty = (totalItems <= 0);

  if (g_app.inventory.selectedIndex < 0)
    g_app.inventory.selectedIndex = 0;
  if (g_app.inventory.selectedIndex >= totalItems && !empty)
    g_app.inventory.selectedIndex = totalItems - 1;

  constexpr int MAX_VISIBLE = 3;
  int start = 0, visCount = 0;
  if (empty)
  {
    start = 0;
    visCount = 1;
  }
  else
  {
    listWindow(totalItems, g_app.inventory.selectedIndex, MAX_VISIBLE, start, visCount);
  }

  // ---------------------------------------------------------------------------
  // Layout: left list pills + right stat readout panel
  // ---------------------------------------------------------------------------
  const int margin = 6;
  const int gapLR = 8;

  const int listX = margin;
  const int listW = 118; // narrower pills like shop
  const int listRight = listX + listW;

  const int panelX = listRight + gapLR;
  const int panelW = SCREEN_W - panelX - margin;

  // Pill sizing
  int itemH = 20;
  int gapY = 5;

  int totalListH = visCount * itemH + (visCount - 1) * gapY;
  if (totalListH > contentH)
  {
    itemH = 18;
    gapY = 4;
    totalListH = visCount * itemH + (visCount - 1) * gapY;
  }

  int startY = contentY + (contentH - totalListH) / 2;
  startY = clampi(startY, contentY, contentBottom - totalListH);

  const int radius = 10;

  // ---------------------------------------------------------------------------
  // Draw list pills (left)
  // ---------------------------------------------------------------------------
  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextDatum(TL_DATUM);

  for (int row = 0; row < visCount; row++)
  {
    int index = empty ? 0 : (start + row);
    int yy = startY + row * (itemH + gapY);
    bool sel = (!empty && index == g_app.inventory.selectedIndex);

    uint16_t outline = sel ? uiPillOutline(pet.type) : TFT_DARKGREY;
    uint16_t fill = sel ? uiPillFillSelected(pet.type) : TFT_BLACK;
    uint16_t textCol = sel ? TFT_WHITE : TFT_LIGHTGREY;

    spr.fillRoundRect(listX, yy, listW, itemH, radius, fill);
    spr.drawRoundRect(listX, yy, listW, itemH, radius, outline);

    String label;
    if (empty)
      label = "(Empty)";
    else
    {
      String name = g_app.inventory.getItemName(index);
      int qty = g_app.inventory.getItemQty(index);
      label = name + " x" + String(qty);
    }

    int th = spr.fontHeight();
    int ty = yy + (itemH - th) / 2;

    spr.setTextColor(textCol, fill);
    spr.drawString(label.c_str(), listX + 8, ty);
  }

  // Scroll arrows (to the right of the left list)
  if (!empty)
  {
    spr.setTextFont(1);
    spr.setTextSize(1);
    spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    spr.setTextDatum(TL_DATUM);

    const int arrowX = listRight + 2;
    const int arrowUpY = startY - 2;
    const int arrowDownY = startY + totalListH - 10;

    if (start > 0)
      spr.drawString("^", arrowX, arrowUpY);
    if (start + visCount < totalItems)
      spr.drawString("v", arrowX, arrowDownY);
  }

  // ---------------------------------------------------------------------------
  // Right stat readout panel
  // ---------------------------------------------------------------------------
  const int panelY = contentY + 6;
  const int panelH = contentH - 12;

  spr.fillRoundRect(panelX, panelY, panelW, panelH, 10, TFT_BLACK);
  spr.drawRoundRect(panelX, panelY, panelW, panelH, 10, TFT_DARKGREY);

  // Determine hovered item type (and compute stat deltas)
  ItemType hoveredType = ITEM_NONE;

  if (!empty)
  {
    int visible = 0;
    int realIndex = -1;
    for (int i = 0; i < Inventory::MAX_ITEMS; i++)
    {
      if (g_app.inventory.items[i].type != ITEM_NONE && g_app.inventory.items[i].quantity > 0)
      {
        if (visible == g_app.inventory.selectedIndex)
        {
          realIndex = i;
          break;
        }
        visible++;
      }
    }
    if (realIndex >= 0)
      hoveredType = g_app.inventory.items[realIndex].type;
  }

  const bool isEvoItem = (!empty && hoveredType == ITEM_ELDRITCH_EYE);
  const uint16_t evoLevel = pet.nextEvoMinLevel(); // 0 if no further evolution
  const bool evoReady = (evoLevel != 0) && pet.canEvolveNext();

  int dhunger = 0;
  int dmood = 0; // happiness
  int drest = 0; // energy
  int dhp = 0;

  // Match the real effects in applyItemMeta() / inventoryUseOne()
  switch (hoveredType)
  {
  case ITEM_SOUL_FOOD:
    dhunger = 30;
    dmood = 10;
    drest = 10;
    break;

  case ITEM_CURSED_RELIC:
    dmood = 30;
    break;

  case ITEM_DEMON_BONE:
    drest = 30;
    break;

  case ITEM_RITUAL_CHALK:
    dhp = 30;
    break;

  case ITEM_ELDRITCH_EYE:
  default:
    break;
  }

  // Draw stats as integers, optionally with "+X" appended
  const int pad = 8;
  int x = panelX + pad;
  int y = panelY + pad;

  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  spr.setTextDatum(TL_DATUM);

  // Eldritch Eye: show evolution message instead of stat lines
  if (isEvoItem)
  {
    // 3-line evolve readout: Title / Level / Availability (colored)
    const int lineGap = 1;

    spr.setTextFont(2);
    spr.setTextSize(1);
    spr.setTextDatum(TL_DATUM);

    // Line 1: Title
    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    spr.drawString("Evolve Now", x, y);
    y += spr.fontHeight() + lineGap;

    // Line 2: Level requirement
    spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    if (evoLevel == 0)
    {
      spr.drawString("Level: --", x, y);
    }
    else
    {
      char lvl[24];
      snprintf(lvl, sizeof(lvl), "Level: %u", (unsigned)evoLevel);
      spr.drawString(lvl, x, y);
    }
    y += spr.fontHeight() + lineGap;

    // Line 3: Availability (colored)
    if (evoLevel == 0)
    {
      spr.setTextColor(TFT_RED, TFT_BLACK);
      spr.drawString("Not Available", x, y);
    }
    else
    {
      spr.setTextColor(evoReady ? TFT_YELLOW : TFT_RED, TFT_BLACK);
      spr.drawString(evoReady ? "Available" : "Not Available", x, y);
    }

    spr.setTextDatum(TL_DATUM);
  }
  else
  {
    auto drawLine = [&](const char *label, int base, int delta)
    {
      // Draw base portion first: "Hunger 20"
      char baseBuf[32];
      snprintf(baseBuf, sizeof(baseBuf), "%s %d", label, base);

      spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
      spr.drawString(baseBuf, x, y);

      if (!empty && delta != 0)
      {

        // Clamp displayed delta so base+delta never exceeds 100
        int shownDelta = delta;
        if (base >= 100)
        {
          shownDelta = 0;
        }
        else if (base + shownDelta > 100)
        {
          shownDelta = 100 - base;
        }

        if (shownDelta != 0)
        {
          // Compute X position right after base text
          int deltaX = x + spr.textWidth(baseBuf);

          // Yellow if it will do something, red if maxed (base==100)
          uint16_t deltaColor = (base < 100) ? TFT_YELLOW : TFT_RED;

          spr.setTextColor(deltaColor, TFT_BLACK);

          char deltaBuf[16];
          snprintf(deltaBuf, sizeof(deltaBuf), "+%d", shownDelta);

          spr.drawString(deltaBuf, deltaX, y);
        }
        else
        {
          // Optional: if you still want to show "+0" in red when maxed, uncomment:
          int deltaX = x + spr.textWidth(baseBuf);
          spr.setTextColor(TFT_RED, TFT_BLACK);
          spr.drawString("+0", deltaX, y);
        }
      }

      y += spr.fontHeight() + 1; // tight spacing
    };

    drawLine("Hunger", pet.hunger, dhunger);
    drawLine("Mood", pet.happiness, dmood);
    drawLine("Rest", pet.energy, drest);
    drawLine("Health", pet.health, dhp);
  }

  spr.setTextDatum(TL_DATUM);
}

// ============================================================================
// NEW PET SCREEN + MINI STATS
// ============================================================================
static const char *g_petBgCachedPath = nullptr;
static PetType g_petBgCachedType = (PetType)255;
static uint8_t g_petBgCachedStage = 255;

// -----------------------------------------------------------------------------
// Memory Release helper for OTA Assets
// -----------------------------------------------------------------------------
void graphicsReleasePetLayerForOta()
{
  petLayer.deleteSprite();
  petLayerReady = false;

  g_petBgCachedPath = nullptr;
  g_petBgCachedType = (PetType)255;
  g_petBgCachedStage = 255;
  g_forcePetBgCache = true;
}

void graphicsRecoverAfterOta()
{
  petLayer.deleteSprite();
  petLayerReady = false;

  // Do NOT delete/recreate the main sprite here.
  // We already have a valid sprite; just force cached content to rebuild.
  g_petBgCachedPath = nullptr;
  g_petBgCachedType = (PetType)255;
  g_petBgCachedStage = 255;
  g_forcePetBgCache = true;

  bgDrawnForState = false;
  lastDrawnState = (UIState)255;

  invalidateBackgroundCache();
  requestUIRedraw();
}

static void cachePetAreaBackgroundIfNeeded(bool force)
{
  if (!g_sdReady)
  {
    spr.fillRect(0, PET_AREA_Y, SCREEN_W, PET_AREA_H, TFT_BLACK);
    invalidateBackgroundCache();
    requestUIRedraw();
    return;
  }

  const char *bgPath = bgPathForPetWithStage(pet.type, pet.evoStage);

  if (!ensurePetLayer())
  {
    spr.fillRect(0, PET_AREA_Y, SCREEN_W, PET_AREA_H, TFT_BLACK);
    if (bgPath)
    {
      (void)sprDrawJpgFromSD(bgPath, 0, PET_AREA_Y);
    }
    invalidateBackgroundCache();
    requestUIRedraw();
    return;
  }

  if (!petLayerReady)
    force = true;

  if (!force && (g_petBgCachedPath == bgPath) && (g_petBgCachedType == pet.type) &&
      (g_petBgCachedStage == pet.evoStage))
  {
    return;
  }

  petLayer.fillSprite(TFT_BLACK);

  bool ok = true;
  if (bgPath)
  {
    // Use sprite file storage + path-only overload.
    static bool s_petLayerFsInited = false;
    if (!s_petLayerFsInited)
    {
      petLayer.setFileStorage(SD);
      s_petLayerFsInited = true;
    }
    ok = petLayer.drawJpgFile(bgPath, 0, 0);
  }

  if (!ok)
  {
    petLayerReady = false;
    g_petBgCachedPath = nullptr;
    g_petBgCachedType = (PetType)255;
    g_petBgCachedStage = 255;

    spr.fillRect(0, PET_AREA_Y, SCREEN_W, PET_AREA_H, TFT_BLACK);

    invalidateBackgroundCache();
    requestUIRedraw();
    return;
  }

  g_petBgCachedPath = bgPath;
  g_petBgCachedType = pet.type;
  g_petBgCachedStage = pet.evoStage;
  petLayerReady = true;
}

static inline void restorePetAreaFromCache()
{
  if (!petLayerReady)
    return;
  spr.pushImage(0, PET_AREA_Y, SCREEN_W, PET_AREA_H, (uint16_t *)petLayer.getBuffer());
}

static bool ensurePetLayer()
{
  if (petLayerReady)
    return true;

  petLayer.setColorDepth(16);
  if (!petLayer.createSprite(SCREEN_W, PET_AREA_H))
  {
    petLayerReady = false;
    return false;
  }

  petLayerReady = true;
  return true;
}

// ============================================================================
// Pet Type Render Profiles (static sprites)
// ============================================================================
struct PetRenderProfile
{
  int w;
  int h;
  int xOff;
  int yOff;
};

static const PetRenderProfile kPetProfile[] = {
    /* PET_DEVIL    */ {PET_SPR_W, PET_SPR_H, PET_X_OFFSET, PET_Y_OFFSET},
    /* PET_KAIJU    */ {PET_SPR_W, PET_SPR_H, PET_X_OFFSET, PET_Y_OFFSET},
    /* PET_ELDRITCH */ {PET_SPR_W, PET_SPR_H, PET_X_OFFSET, PET_Y_OFFSET},
    /* PET_ALIEN    */ {PET_SPR_W, PET_SPR_H, PET_X_OFFSET, PET_Y_OFFSET},
    /* PET_ANUBIS   */ {PET_SPR_W, PET_SPR_H, PET_X_OFFSET, PET_Y_OFFSET},
    /* PET_AXOLOTL  */ {PET_SPR_W, PET_SPR_H, PET_X_OFFSET, PET_Y_OFFSET},
};

static inline const PetRenderProfile &getPetProfile(PetType t)
{
  int idx = (int)t;
  const int count = (int)(sizeof(kPetProfile) / sizeof(kPetProfile[0]));
  if (idx < 0 || idx >= count)
    idx = 0;
  return kPetProfile[idx];
}

static void getPetHomeScreenPosition(int &outX, int &outY)
{
  const int petAreaW = SCREEN_W - MINI_STAT_W - MINI_STAT_PAD;
  const int petAreaX = 0;

  const PetRenderProfile &prof = getPetProfile(pet.type);

  outX = petAreaX + (petAreaW / 2) + prof.xOff;
  outY = (PET_AREA_Y + PET_AREA_H) + prof.yOff;
}

void resetPetScreenPositionToHome()
{
  int homeX = 0;
  int homeY = 0;
  getPetHomeScreenPosition(homeX, homeY);

  s_petHomeX = homeX;
  s_petHomeY = homeY;

  s_petScreenX = homeX;
  s_petScreenY = homeY;
  s_petScreenPosInitialized = true;

  s_petIntroWalkActive = false;
  s_petIntroWalkLastStepMs = 0;
  s_petIntroArriveTurnActive = false;
  s_petIntroArriveTurnStartMs = 0;
  s_petIntroStandHoldActive = false;
  s_petIntroStandHoldStartMs = 0;
  s_petIntroHandoffActive = false;

  resetPetWanderToHome();
}

static void scheduleNextPetWander()
{
  const uint32_t now = millis();
  const uint32_t span = (kPetWanderMaxIdleMs - kPetWanderMinIdleMs);
  s_petWanderUntilMs = now + kPetWanderMinIdleMs + (span ? (uint32_t)random((long)span) : 0);
}

static void resetPetWanderToHome()
{
  s_petScreenX = s_petHomeX;
  s_petScreenY = s_petHomeY;
  s_petWanderTargetX = s_petHomeX;
  s_petWanderSideAX = s_petHomeX;
  s_petWanderSideBX = s_petHomeX;
  s_petWanderState = PetWanderState::HOME_IDLE;
  s_petWanderLastStepMs = 0;
  scheduleNextPetWander();
}

void startPetIntroWalkFromLeft()
{
  int homeX = 0;
  int homeY = 0;
  getPetHomeScreenPosition(homeX, homeY);
  s_petHomeX = homeX;
  s_petHomeY = homeY;

  // Start fully offscreen to the left using the pet sprite width as margin.
  s_petScreenX = -PET_SPR_W;
  s_petScreenY = homeY;
  s_petScreenPosInitialized = true;

  s_petIntroWalkActive = true;
  s_petIntroWalkLastStepMs = millis();
  s_petIntroArriveTurnActive = false;
  s_petIntroStandHoldActive = false;
  s_petIntroHandoffActive = false;
  s_petWanderState = PetWanderState::HOME_IDLE;
  s_petWanderTargetX = s_petHomeX;
  s_petWanderLastStepMs = 0;
  s_petWanderUntilMs = 0;
}

static void tickPetIntroWalk()
{
  if (!s_petIntroWalkActive)
  {
    if (s_petIntroStandHoldActive)
    {
      if ((millis() - s_petIntroStandHoldStartMs) >= kPetIntroStandHoldMs)
      {
        s_petIntroStandHoldActive = false;
        s_petIntroHandoffActive = true;
        requestUIRedraw();
      }
      return;
    }

    if (s_petIntroArriveTurnActive)
    {
      if ((millis() - s_petIntroArriveTurnStartMs) >= kPetIntroArriveTurnMs)
      {
        s_petIntroArriveTurnActive = false;
        s_petIntroStandHoldActive = true;
        s_petIntroStandHoldStartMs = millis();
        requestUIRedraw();
      }
      return;
    }

    return;
  }

  if (g_app.uiState != UIState::PET_SCREEN)
    return;

  int homeX = 0;
  int homeY = 0;
  getPetHomeScreenPosition(homeX, homeY);

  s_petScreenY = homeY;

  const uint32_t now = millis();
  if ((now - s_petIntroWalkLastStepMs) < kPetIntroWalkStepMs)
    return;

  s_petIntroWalkLastStepMs = now;

  if (s_petScreenX < homeX)
  {
    s_petScreenX += kPetIntroWalkStepPx;
    if (s_petScreenX > homeX)
      s_petScreenX = homeX;

    requestUIRedraw();
  }

  if (s_petScreenX >= homeX)
  {
    s_petScreenX = homeX;
    s_petScreenY = homeY;
    s_petIntroWalkActive = false;

    s_petIntroArriveTurnActive = true;
    s_petIntroArriveTurnStartMs = millis();

    requestUIRedraw();
  }
}

static void tickPetWander()
{
  // Never wander while the scripted intro is still owning the pet.
  if (s_petIntroWalkActive || s_petIntroArriveTurnActive || s_petIntroStandHoldActive)
    return;

  // Handoff is only meant to protect the first frame after the scripted intro.
  // Do not let it block wandering indefinitely.
  if (s_petIntroHandoffActive)
  {
    s_petIntroHandoffActive = false;
    requestUIRedraw();
  }

  const bool wanderActive = (s_petWanderState != PetWanderState::HOME_IDLE);

  // Only wander on the main PET tab.
  // IMPORTANT: do not hard-reset an active wander.
  if (g_app.uiState != UIState::PET_SCREEN || g_app.currentTab != Tab::TAB_PET)
  {
    if (!wanderActive)
      resetPetWanderToHome();
    return;
  }

  // Keep the pet grounded at home Y.
  s_petScreenY = s_petHomeY;

  // Don't start a new wander while sleeping, but don't interrupt one already in progress.
  if (pet.isSleeping)
  {
    if (!wanderActive)
      resetPetWanderToHome();
    return;
  }

  // Only happy or bored pets should START wandering.
  // If a wander is already active, let it finish naturally.
  const PetMood mood = petResolveMood(pet);
  const bool wanderAllowed = (mood == MOOD_HAPPY || mood == MOOD_BORED);
  if (!wanderAllowed && !wanderActive)
  {
    s_petScreenX = s_petHomeX;
    s_petScreenY = s_petHomeY;
    return;
  }

  const uint32_t now = millis();

  switch (s_petWanderState)
  {
  case PetWanderState::HOME_IDLE:
  {
    if (s_petWanderUntilMs == 0)
      scheduleNextPetWander();

    if ((int32_t)(now - s_petWanderUntilMs) < 0)
      return;

    int offsetA = 0;
    for (int tries = 0; tries < 8; ++tries)
    {
      offsetA = (int)random(-kPetWanderRangePx, kPetWanderRangePx + 1);
      if (abs(offsetA) >= kPetWanderMinMovePx)
        break;
    }

    if (abs(offsetA) < kPetWanderMinMovePx)
    {
      scheduleNextPetWander();
      return;
    }

    const int petAreaW = SCREEN_W - MINI_STAT_W - MINI_STAT_PAD;

    // Keep the full sprite visible, but reserve a little extra space away from
    // the mini-stat cluster on the right so the pet doesn't crowd it.
    const int minAnchorX = PET_SPR_W / 2;
    const int rightClearancePx = 12;
    const int maxAnchorX = petAreaW - (PET_SPR_W / 2) - rightClearancePx;

    const int originX = s_petScreenX;

    s_petWanderSideAX = clampi(originX + offsetA, minAnchorX, maxAnchorX);

    int offsetB = -offsetA;
    if (abs(offsetB) < kPetWanderMinMovePx)
      offsetB = (offsetB < 0) ? -kPetWanderMinMovePx : kPetWanderMinMovePx;

    s_petWanderSideBX = clampi(originX + offsetB, minAnchorX, maxAnchorX);

    // Reject tiny real moves after clamping.
    if (abs(s_petWanderSideAX - originX) < kPetWanderMinMovePx ||
        abs(s_petWanderSideBX - s_petWanderSideAX) < kPetWanderMinMovePx)
    {
      scheduleNextPetWander();
      return;
    }

    s_petWanderTargetX = s_petWanderSideAX;
    s_petWanderState = PetWanderState::MOVING_TO_SIDE_A;
    s_petWanderLastStepMs = now;
    requestUIRedraw();
    return;
  }

  case PetWanderState::MOVING_TO_SIDE_A:
  {
    if ((int32_t)(now - s_petWanderLastStepMs) < (int32_t)kPetWanderStepMs)
      return;

    s_petWanderLastStepMs = now;

    if (abs(s_petScreenX - s_petWanderTargetX) <= kPetWanderStepPx)
    {
      s_petScreenX = s_petWanderTargetX;
      s_petWanderState = PetWanderState::PAUSE_AWAY_1;
      s_petWanderUntilMs = now + kPetWanderPauseAwayMs;
      return;
    }

    s_petScreenX += (s_petScreenX < s_petWanderTargetX) ? kPetWanderStepPx : -kPetWanderStepPx;
    requestUIRedraw();
    return;
  }

  case PetWanderState::PAUSE_AWAY_1:
  {
    if ((int32_t)(now - s_petWanderUntilMs) < 0)
      return;

    s_petWanderTargetX = s_petWanderSideBX;
    s_petWanderState = PetWanderState::MOVING_TO_SIDE_B;
    s_petWanderLastStepMs = now;
    requestUIRedraw();
    return;
  }

  case PetWanderState::MOVING_TO_SIDE_B:
  {
    if ((int32_t)(now - s_petWanderLastStepMs) < (int32_t)kPetWanderStepMs)
      return;

    s_petWanderLastStepMs = now;

    if (abs(s_petScreenX - s_petWanderTargetX) <= kPetWanderStepPx)
    {
      s_petScreenX = s_petWanderTargetX;
      s_petWanderState = PetWanderState::PAUSE_AWAY_2;
      s_petWanderUntilMs = now + kPetWanderPauseAwayMs;
      return;
    }

    s_petScreenX += (s_petScreenX < s_petWanderTargetX) ? kPetWanderStepPx : -kPetWanderStepPx;
    requestUIRedraw();
    return;
  }

  case PetWanderState::PAUSE_AWAY_2:
  {
    if ((int32_t)(now - s_petWanderUntilMs) < 0)
      return;

    // Intentionally end the wander wherever the pet currently is.
    s_petWanderState = PetWanderState::HOME_IDLE;
    scheduleNextPetWander();
    requestUIRedraw();
    return;
  }

  case PetWanderState::RETURNING_HOME:
  {
    if (now - s_petWanderLastStepMs < kPetWanderStepMs)
      return;

    s_petWanderLastStepMs = now;

    int dx = s_petHomeX - s_petScreenX;

    // Close enough → snap ONLY position, do NOT reset state logic
    if (abs(dx) <= kPetWanderStepPx)
    {
      s_petScreenX = s_petHomeX;
      s_petScreenY = s_petHomeY;

      // Transition cleanly to idle WITHOUT teleport helper
      s_petWanderState = PetWanderState::HOME_IDLE;

      // Schedule next wander
      s_petWanderUntilMs = now + random(kPetWanderMinIdleMs, kPetWanderMaxIdleMs);

      return;
    }

    // Step toward home
    s_petScreenX += (dx > 0) ? kPetWanderStepPx : -kPetWanderStepPx;
    requestUIRedraw();
    return;
  }
  }
}

static bool drawIntroWalkingPetOverride()
{
  if (!g_sdReady)
    return false;

  const bool walking = s_petIntroWalkActive || s_petWanderState == PetWanderState::MOVING_TO_SIDE_A ||
                       s_petWanderState == PetWanderState::MOVING_TO_SIDE_B ||
                       s_petWanderState == PetWanderState::RETURNING_HOME;

  bool facingLeft = false;

  if (s_petIntroWalkActive)
  {
    facingLeft = false;
  }
  else if (s_petWanderState == PetWanderState::MOVING_TO_SIDE_A || s_petWanderState == PetWanderState::MOVING_TO_SIDE_B)
  {
    facingLeft = (s_petWanderTargetX < s_petScreenX);
  }
  else if (s_petWanderState == PetWanderState::RETURNING_HOME)
  {
    facingLeft = (s_petHomeX < s_petScreenX);
  }

  const char *path = nullptr;

  if (walking)
  {
    const uint32_t frame = (millis() / kPetIntroWalkFrameMs) & 1U;

    if (pet.type == PET_DEVIL)
    {
      if (pet.evoStage == 0)
      {
        if (facingLeft)
          path = frame ? PATH_DEV_BB_WALK2_L : PATH_DEV_BB_WALK1_L;
        else
          path = frame ? PATH_DEV_BB_WALK2 : PATH_DEV_BB_WALK1;
      }
      else if (pet.evoStage == 1)
      {
        if (facingLeft)
          path = frame ? PATH_DEV_TN_WALK2_L : PATH_DEV_TN_WALK1_L;
        else
          path = frame ? PATH_DEV_TN_WALK2 : PATH_DEV_TN_WALK1;
      }
      else if (pet.evoStage == 2)
      {
        if (facingLeft)
          path = frame ? PATH_DEV_AD_WALK2_L : PATH_DEV_AD_WALK1_L;
        else
          path = frame ? PATH_DEV_AD_WALK2 : PATH_DEV_AD_WALK1;
      }
      else
      {
        if (facingLeft)
          path = frame ? PATH_DEV_EL_WALK2_L : PATH_DEV_EL_WALK1_L;
        else
          path = frame ? PATH_DEV_EL_WALK2 : PATH_DEV_EL_WALK1;
      }
    }
    else if (pet.type == PET_ELDRITCH)
    {
      if (pet.evoStage == 0)
      {
        if (facingLeft)
          path = frame ? PATH_ELD_BB_WALK2_L : PATH_ELD_BB_WALK1_L;
        else
          path = frame ? PATH_ELD_BB_WALK2 : PATH_ELD_BB_WALK1;
      }
    }
  }

  if (!path || !path[0])
    return false;

  int w = PET_SPR_W;
  int h = PET_SPR_H;
  (void)getPngWH(path, w, h);

  const int drawX = s_petScreenX - (w / 2);
  const int drawY = s_petScreenY - h + kPetIntroYOffset;

  const bool ok = sprDrawPngFromSD(path, drawX, drawY);
  if (!ok)
  {
    Serial.printf("[PET WALK] draw failed path='%s' x=%d y=%d w=%d h=%d\n", path, drawX, drawY, w, h);
  }
  return ok;
}

static void drawPetScreenImpl(bool redrawBg)
{
  if (!isScreenOn())
    return;

  static PetType s_lastBgPetType = (PetType)255;
  static uint8_t s_lastBgEvoStage = 255;

  const bool petChanged = (s_lastBgPetType != pet.type) || (s_lastBgEvoStage != pet.evoStage);

  const bool cacheMissing = (g_petBgCachedPath == nullptr);

  const bool needPetBg = redrawBg || petChanged || cacheMissing || g_forcePetBgCache;

  s_lastBgPetType = pet.type;
  s_lastBgEvoStage = pet.evoStage;

  bool animChanged = false;
  if (g_app.currentTab == Tab::TAB_PET)
  {
    animChanged = animConsumeFrameChanged();
  }
  else
  {
    (void)animConsumeFrameChanged();
  }

  const bool needRestore = redrawBg || animChanged || needPetBg;

  if (s_petIntroHandoffActive && animChanged)
  {
    s_petIntroHandoffActive = false;
    requestUIRedraw();
  }

  cachePetAreaBackgroundIfNeeded(needPetBg);
  g_forcePetBgCache = false;

  if (needPetBg || needRestore)
  {
    restorePetAreaFromCache();
  }

  drawTopBar();

  int homeCenterX = 0;
  int homeBottomY = 0;
  getPetHomeScreenPosition(homeCenterX, homeBottomY);

  s_petHomeX = homeCenterX;
  s_petHomeY = homeBottomY;

  // Normal PET-screen entries should land at home unless a scripted intro
  // or wander movement is actively owning the position.
  if (!s_petScreenPosInitialized)
  {
    s_petScreenX = homeCenterX;
    s_petScreenY = homeBottomY;
    s_petScreenPosInitialized = true;
  }
  else if (!petWalkOverrideActive() && g_app.uiState == UIState::PET_SCREEN && g_app.currentTab == Tab::TAB_PET)
  {
    // Do not forcibly snap here.
    // Let the wander / intro state machine own the final position.
  }

  if (petWalkOverrideActive())
  {
    if (!drawIntroWalkingPetOverride())
    {
      animDrawPetFrameAnchoredBottom(s_petScreenX, s_petScreenY);
    }
  }
  else
  {
    animDrawPetFrameAnchoredBottom(s_petScreenX, s_petScreenY);
  }

  drawMiniStatPreview();
  drawTabBar();

  drawPetPerfHud();
}

// -----------------------------------------------------------------------------
// Draw Wrappers
// -----------------------------------------------------------------------------
void drawPetScreen() { drawPetScreenImpl(true); }

static void drawSettingsScreen() { drawSettingsMenu(); }

static void drawInventoryScreen() { drawInventoryMenu(); }

static void drawPetSleepingScreen() { drawSleepScreen(); }

static void drawMiniGameScreen()
{
  if (currentMiniGame == MiniGame::NONE)
    return;

  drawMiniGame();
}

static void drawNamePetScreen() { drawNamePetScreen(true); }

static void drawDeathScreen() { drawDeathScreen(true); }

// ----- Set Time UI helpers -----
static void drawButton(int x, int y, int w, int h, const char *label, bool selected)
{
  const uint16_t outline = selected ? uiPillOutline(pet.type) : TFT_DARKGREY;
  const uint16_t fill = selected ? uiPillFillSelected(pet.type) : TFT_BLACK;
  const uint16_t textCol = selected ? TFT_WHITE : TFT_LIGHTGREY;

  spr.fillRoundRect(x, y, w, h, 8, fill);
  spr.drawRoundRect(x, y, w, h, 8, outline);

  spr.setTextDatum(MC_DATUM);
  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextColor(textCol, fill);
  spr.drawString(label ? label : "", x + (w / 2), y + (h / 2));
  spr.setTextDatum(TL_DATUM);
}

static void drawSetTimePanel(int x, int y, int w, int h, const char *title, int selectedField, int fieldId)
{
  spr.drawRoundRect(x, y, w, h, 8, uiPillOutline(pet.type));
  spr.fillRoundRect(x + 1, y + 1, w - 2, h - 2, 8, TFT_BLACK);

  spr.setTextDatum(TL_DATUM);
  spr.setTextFont(1);
  spr.setTextSize(1);
  spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  spr.drawString(title ? title : "", x + 6, y + 4);

  const int year = g_setTimeTm.tm_year + 1900;
  const int mon = g_setTimeTm.tm_mon + 1;
  const int day = g_setTimeTm.tm_mday;
  const int hh = g_setTimeTm.tm_hour;
  const int mm = g_setTimeTm.tm_min;

  char a[6], b[6], c[6];
  a[0] = b[0] = c[0] = '\0';

  int nFields = 0;

  if (fieldId == 0)
  { // Date
    snprintf(a, sizeof(a), "%04d", year);
    snprintf(b, sizeof(b), "%02d", mon);
    snprintf(c, sizeof(c), "%02d", day);
    nFields = 3;
  }
  else
  { // Time
    snprintf(a, sizeof(a), "%02d", hh);
    snprintf(b, sizeof(b), "%02d", mm);
    nFields = 2;
  }

  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);

  const int baseY = y + 18;
  int cx = x + 6;

  auto drawField = [&](const char *s, int fid)
  {
    spr.drawString(s, cx, baseY);
    int tw = spr.textWidth(s);
    if (selectedField == fid)
    {
      spr.drawFastHLine(cx, baseY + 14, tw, TFT_YELLOW);
    }
    cx += tw + 6;
  };

  if (nFields == 3)
  {
    drawField(a, 0);
    spr.drawString("-", cx, baseY);
    cx += spr.textWidth("-") + 6;
    drawField(b, 1);
    spr.drawString("-", cx, baseY);
    cx += spr.textWidth("-") + 6;
    drawField(c, 2);
  }
  else
  {
    drawField(a, 3);
    spr.drawString(":", cx, baseY);
    cx += spr.textWidth(":") + 6;
    drawField(b, 4);
  }
}

static void drawSetDateTimePanel(int x, int y, int w, int h, int selectedField)
{
  spr.drawRoundRect(x, y, w, h, 8, uiPillOutline(pet.type));
  spr.fillRoundRect(x + 1, y + 1, w - 2, h - 2, 8, TFT_BLACK);

  spr.setTextDatum(TL_DATUM);
  spr.setTextFont(1);
  spr.setTextSize(1);
  spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  spr.drawString("Date & Time", x + 6, y + 4);

  const int year = g_setTimeTm.tm_year + 1900;
  const int mon = g_setTimeTm.tm_mon + 1;
  const int day = g_setTimeTm.tm_mday;
  const int hh = g_setTimeTm.tm_hour;
  const int mm = g_setTimeTm.tm_min;

  char yy[6], mo[4], dd[4], th[4], tm[4];
  snprintf(yy, sizeof(yy), "%04d", year);
  snprintf(mo, sizeof(mo), "%02d", mon);
  snprintf(dd, sizeof(dd), "%02d", day);
  snprintf(th, sizeof(th), "%02d", hh);
  snprintf(tm, sizeof(tm), "%02d", mm);

  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);

  const int baseY = y + 18;
  int cx = x + 8;

  auto drawField = [&](const char *s, int fid)
  {
    spr.drawString(s, cx, baseY);
    const int tw = spr.textWidth(s);
    if (selectedField == fid)
    {
      spr.drawFastHLine(cx, baseY + 14, tw, TFT_YELLOW);
    }
    cx += tw + 4; // tighter spacing than old panel
  };

  // Date: YYYY-MM-DD (fields 0,1,2)
  drawField(yy, 0);
  spr.drawString("-", cx, baseY);
  cx += spr.textWidth("-") + 4;
  drawField(mo, 1);
  spr.drawString("-", cx, baseY);
  cx += spr.textWidth("-") + 4;
  drawField(dd, 2);

  // Spacer between date and time
  cx += 10;

  // Time: HH:MM (fields 3,4)
  drawField(th, 3);
  spr.drawString(":", cx, baseY);
  cx += spr.textWidth(":") + 4;
  drawField(tm, 4);
}

// ============================================================================
// STATS TAB
// ============================================================================
static void drawStatsTab(bool redrawBg)
{
  (void)redrawBg;
  if (!isScreenOn())
    return;

  drawTopBar();
  drawTabBar();

  const int contentY = TOP_BAR_H;
  const int contentH = SCREEN_H - TOP_BAR_H - TAB_BAR_H;
  drawNonPetTabBackground();
  drawTopBar();
  drawTabBar();

  const int pad = 6;
  const int cardX = pad;
  const int cardY = contentY + 2;
  const int cardW = SCREEN_W - pad * 2;
  const int cardH = contentH - 4;

  spr.fillRoundRect(cardX, cardY, cardW, cardH, 8, TFT_BLACK);
  spr.drawRoundRect(cardX, cardY, cardW, cardH, 8, TFT_DARKGREY);

  const int nameX = cardX + 10;
  const int nameY = cardY + 8;

  char nmBuf[64];
  pet.buildDisplayName(nmBuf, sizeof(nmBuf));

  String titleLine = String(nmBuf);
  titleLine.trim();
  if (titleLine.length() == 0)
    titleLine = "(NO NAME)";

  spr.setTextDatum(TL_DATUM);
  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextColor(TFT_YELLOW, TFT_BLACK);
  spr.drawString(titleLine, nameX, nameY);

  const int dividerY = nameY + spr.fontHeight() + 4;
  spr.drawFastHLine(cardX + 10, dividerY, cardW - 20, TFT_DARKGREY);

  // -----------------------
  // Bio-card layout
  // -----------------------
  spr.setTextFont(1);
  spr.setTextSize(1);
  spr.setTextDatum(TL_DATUM);

  const int bodyPad = 8;
  const int bodyX = cardX + bodyPad;
  const int bodyY = dividerY + bodyPad;
  const int bodyW = cardW - (bodyPad * 2);
  const int bodyH = (cardY + cardH) - bodyY - bodyPad;

  // Left: status image square
  const int desiredBio = 48;
  const int bioSize = (bodyH < desiredBio) ? bodyH : desiredBio;
  const int bioX = bodyX;
  const int bioY = bodyY + (bodyH - bioSize) / 2;

  // Frame
  spr.drawRoundRect(bioX - 1, bioY - 1, bioSize + 2, bioSize + 2, 6, TFT_DARKGREY);

  // First milestone asset (baby devil happy bio)
  // We'll expand this lookup later.
  const char *bioPath = getBioStatusImagePath();

  // Draw image if SD is ready (uses your existing PNG pipeline).
  // If your project guards SD differently, swap g_sdReady for whatever you use.
  if (g_sdReady)
  {
    sprDrawPngFromSD(bioPath, bioX, bioY);
  }
  else
  {
    spr.setTextColor(TFT_DARKGREY, TFT_BLACK);
    spr.drawString("NO IMG", bioX + 8, bioY + (bioSize / 2) - 4);
  }

  // Right: key/value stats
  const int textX = bioX + bioSize + 12;
  const int textY = bodyY;
  const int rowH = 13;

  auto drawKV = [&](int px, int py, const char *key, const char *val)
  {
    spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);

    char kbuf[24];
    snprintf(kbuf, sizeof(kbuf), "%s:", key);
    spr.drawString(kbuf, px, py, 1);

    int vx = px + spr.textWidth(kbuf) + 4;
    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    spr.drawString(val ? val : "", vx, py, 1);
  };

  char buf[40];

  // Row 0: Age
  {
    uint32_t birth = saveManagerGetBirthEpoch();
    int64_t now = (int64_t)time(nullptr);
    AgeParts a = calcAgeParts((int64_t)birth, now);
    formatAgeString(buf, sizeof(buf), a, false);
  }
  drawKV(textX, textY + 0 * rowH, "Age", buf);

  // Row 1: Level (with evolve target)
  {
    const int curLevel = pet.level;
    const uint16_t evolveLevel = pet.nextEvoMinLevel();  // 0 if no further evolution
    const bool evolutionAvailable = pet.canEvolveNext(); // level >= evolveLevel and evoStage < 3

    const int y = textY + 1 * rowH;

    spr.setTextDatum(TL_DATUM);
    spr.setTextFont(1);
    spr.setTextSize(1);

    // Key (match drawKV look)
    spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    char kbuf[24];
    snprintf(kbuf, sizeof(kbuf), "%s:", "Level");
    spr.drawString(kbuf, textX, y, 1);

    const int vx = textX + spr.textWidth(kbuf) + 4;

    // Value
    if (evolveLevel == 0)
    {
      char vbuf[32];
      snprintf(vbuf, sizeof(vbuf), "%d", curLevel);
      spr.setTextColor(TFT_WHITE, TFT_BLACK);
      spr.drawString(vbuf, vx, y, 1);
    }
    else if (evolutionAvailable)
    {
      char left[16];
      snprintf(left, sizeof(left), "%d (", curLevel);

      spr.setTextColor(TFT_WHITE, TFT_BLACK);
      spr.drawString(left, vx, y, 1);

      int w = spr.textWidth(left);

      spr.setTextColor(TFT_YELLOW, TFT_BLACK);
      spr.drawString("Evo Ready!", vx + w, y, 1);

      w += spr.textWidth("Evo Ready!");

      spr.setTextColor(TFT_WHITE, TFT_BLACK);
      spr.drawString(")", vx + w, y, 1);
    }
    else
    {
      char vbuf[64];
      snprintf(vbuf, sizeof(vbuf), "%d (%u to evolve)", curLevel, (unsigned)evolveLevel);

      spr.setTextColor(TFT_WHITE, TFT_BLACK);
      spr.drawString(vbuf, vx, y, 1);
    }
  }

  // Row 2: XP
  {
    const uint32_t need = pet.xpForNextLevel();
    if (need > 0)
    {
      snprintf(buf, sizeof(buf), "%lu/%lu", (unsigned long)pet.xp, (unsigned long)need);
    }
    else
    {
      snprintf(buf, sizeof(buf), "%lu", (unsigned long)pet.xp);
    }
  }
  drawKV(textX, textY + 2 * rowH, "XP", buf);

  // Row 3: Condition (derived from stats)
  {
    const char *cond = "Happy";
    uint16_t condColor = TFT_GREEN;

    const int SICK_HP = 60;
    const int HUNGRY_LEVEL = 30;
    const int TIRED_EN = 30;
    const int ANGRY_HAPPY = 30;
    const int BORED_HAPPY = 60;

    if (pet.health < SICK_HP)
    {
      cond = "Sick";
      condColor = TFT_RED;
    }
    else if (pet.hunger <= HUNGRY_LEVEL)
    {
      cond = "Hungry";
      condColor = TFT_YELLOW;
    }
    else if (pet.energy <= TIRED_EN)
    {
      cond = "Tired";
      condColor = TFT_YELLOW;
    }
    else if (pet.happiness <= ANGRY_HAPPY)
    {
      cond = "Angry";
      condColor = TFT_YELLOW;
    }
    else if (pet.happiness < BORED_HAPPY)
    {
      cond = "Bored";
      condColor = TFT_GREEN;
    }

    // If you want this to look like the other rows, use drawKV:
    // (value colored, key grey)
    spr.setTextDatum(TL_DATUM);
    spr.setTextFont(1);
    spr.setTextSize(1);

    char kbuf[24];
    snprintf(kbuf, sizeof(kbuf), "%s:", "Condition");
    spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    spr.drawString(kbuf, textX, textY + 3 * rowH, 1);

    const int vx = textX + spr.textWidth(kbuf) + 4;
    spr.setTextColor(condColor, TFT_BLACK);
    spr.drawString(cond, vx, textY + 3 * rowH, 1);
  }
}

// ============================================================================
// PLAY TAB (mini-games list)
// ============================================================================
static void drawPlayTab(bool redrawBg)
{
  if (!isScreenOn())
    return;

  (void)redrawBg;

  drawNonPetTabBackground();
  drawTopBar();
  drawTabBar();

  const int contentY = TOP_BAR_H;
  const int contentH = SCREEN_H - TOP_BAR_H - TAB_BAR_H;
  const int contentBottom = contentY + contentH;

  const int totalItems = uiPlayMenuCount();

  playMenuIndex = clampi(playMenuIndex, 0, totalItems - 1);

  constexpr int MAX_VISIBLE = 3;
  int start = 0, visCount = 0;
  listWindow(totalItems, playMenuIndex, MAX_VISIBLE, start, visCount);

  int itemH = 22;
  int gap = 6;

  int totalH = visCount * itemH + (visCount - 1) * gap;
  if (totalH > contentH)
  {
    itemH = 20;
    gap = 5;
    totalH = visCount * itemH + (visCount - 1) * gap;
  }

  int startY = contentY + (contentH - totalH) / 2;
  startY = clampi(startY, contentY, contentBottom - totalH);

  const int boxW = (SCREEN_W * 3) / 4;
  const int boxX = (SCREEN_W - boxW) / 2;
  const int radius = 10;

  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextDatum(TL_DATUM);

  for (int row = 0; row < visCount; row++)
  {
    int index = start + row;
    int y = startY + row * (itemH + gap);
    bool sel = (index == playMenuIndex);

    uint16_t outline = sel ? uiPillOutline(pet.type) : TFT_DARKGREY;
    uint16_t fill = sel ? uiPillFillSelected(pet.type) : TFT_BLACK;
    uint16_t textCol = sel ? TFT_WHITE : TFT_LIGHTGREY;

    spr.fillRoundRect(boxX, y, boxW, itemH, radius, fill);
    spr.drawRoundRect(boxX, y, boxW, itemH, radius, outline);

    int cx = boxX + boxW / 2;
    int th = spr.fontHeight();
    int ty = y + (itemH - th) / 2;

    spr.setTextColor(textCol, fill);
    spr.drawCentreString(uiPlayMenuLabel(index), cx, ty, 2);
  }

  if (start > 0 || (start + visCount < totalItems))
  {
    spr.setTextFont(1);
    spr.setTextSize(1);
    spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    spr.setTextDatum(TL_DATUM);

    const int arrowX = boxX + boxW + 6;
    const int arrowUpY = startY - 2;
    const int arrowDownY = startY + totalH - 10;

    if (start > 0)
      spr.drawString("^", arrowX, arrowUpY);
    if (start + visCount < totalItems)
      spr.drawString("v", arrowX, arrowDownY);
  }

  spr.setTextDatum(TL_DATUM);
}

// ============================================================================
// Sleep screen (sleep_bg.jpg background + bottom sleep meter)
// ============================================================================
static void drawSleepMeterBar()
{
  const int y = SCREEN_H - TAB_BAR_H;
  const int h = TAB_BAR_H;

  const PetUIColorScheme ui = uiSchemeForPet(pet.type);
  const uint16_t bg = ui.topBg;
  const uint16_t outline = ui.topOutline;
  const uint16_t textCol = ui.topText;

  // Footer hint (replaces the old energy meter)
  spr.fillRect(0, y, SCREEN_W, h, bg);
  spr.drawFastHLine(0, y, SCREEN_W, outline);

  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextColor(textCol, bg);
  spr.setTextDatum(MC_DATUM);
  spr.drawString("Press OK or G to Wake Up", SCREEN_W / 2, y + h / 2);

  spr.setTextDatum(TL_DATUM);
}

// -----------------------------------------------------------------------------
// DEVIL BABY sleep background animation (4 JPG frames)
// -----------------------------------------------------------------------------
static constexpr uint32_t DEV_BABY_SLEEP_FRAME_MS = 200; // tweak speed (ms)

static const char *DEV_BABY_SLEEP_FRAMES[4] = {
    "/raising_hell/graphics/pet/anim/dev/bb/sleep/dev_baby_sleepbk1.jpg",
    "/raising_hell/graphics/pet/anim/dev/bb/sleep/dev_baby_sleepbk2.jpg",
    "/raising_hell/graphics/pet/anim/dev/bb/sleep/dev_baby_sleepbk3.jpg",
    "/raising_hell/graphics/pet/anim/dev/bb/sleep/dev_baby_sleepbk4.jpg",
};

static inline bool useDevBabySleepAnim() { return (pet.type == PET_DEVIL) && (pet.evoStage == 0); }

// -----------------------------------------------------------------------------
// DEVIL TEEN sleep background animation (4 JPG frames)
// -----------------------------------------------------------------------------
static constexpr uint32_t DEV_TEEN_SLEEP_FRAME_MS = 180; // tweak speed (ms)

static const char *DEV_TEEN_SLEEP_FRAMES[4] = {
    "/raising_hell/graphics/pet/anim/dev/tn/sleep/dev_teen_sleepbk1.jpg",
    "/raising_hell/graphics/pet/anim/dev/tn/sleep/dev_teen_sleepbk2.jpg",
    "/raising_hell/graphics/pet/anim/dev/tn/sleep/dev_teen_sleepbk3.jpg",
    "/raising_hell/graphics/pet/anim/dev/tn/sleep/dev_teen_sleepbk4.jpg",
};

static inline bool useDevTeenSleepAnim() { return (pet.type == PET_DEVIL) && (pet.evoStage == 1); }

// -----------------------------------------------------------------------------
// DEVIL ADULT sleep background animation (4 PNG frames)
// -----------------------------------------------------------------------------
static constexpr uint32_t DEV_ADULT_SLEEP_FRAME_MS = 160; // slightly smoother

static const char *DEV_ADULT_SLEEP_FRAMES[4] = {
    "/raising_hell/graphics/pet/anim/dev/ad/sleep/dev_adult_sleepbk1.jpg",
    "/raising_hell/graphics/pet/anim/dev/ad/sleep/dev_adult_sleepbk2.jpg",
    "/raising_hell/graphics/pet/anim/dev/ad/sleep/dev_adult_sleepbk3.jpg",
    "/raising_hell/graphics/pet/anim/dev/ad/sleep/dev_adult_sleepbk4.jpg",
};

static inline bool useDevAdultSleepAnim() { return (pet.type == PET_DEVIL) && (pet.evoStage == 2); }

// -----------------------------------------------------------------------------
// DEVIL ELDER sleep background animation (4 PNG frames)
// -----------------------------------------------------------------------------
static constexpr uint32_t DEV_ELDER_SLEEP_FRAME_MS = 200; // slightly slower feel

static constexpr uint8_t DEV_ELDER_SLEEP_FRAME_COUNT = 4;

static const char *DEV_ELDER_SLEEP_FRAMES[DEV_ELDER_SLEEP_FRAME_COUNT] = {
    "/raising_hell/graphics/pet/anim/dev/ed/sleep/dev_el_sleepbk1.jpg",
    "/raising_hell/graphics/pet/anim/dev/ed/sleep/dev_el_sleepbk2.jpg",
    "/raising_hell/graphics/pet/anim/dev/ed/sleep/dev_el_sleepbk3.jpg",
    "/raising_hell/graphics/pet/anim/dev/ed/sleep/dev_el_sleepbk4.jpg",
};

static inline bool useDevElderSleepAnim()
{
  return (pet.type == PET_DEVIL) && (pet.evoStage == 3); // adjust if needed
}

// -----------------------------------------------------------------------------
// ELDRITCH BABY sleep background animation (4 PNG frames)
// -----------------------------------------------------------------------------
static constexpr uint32_t ELD_BABY_SLEEP_FRAME_MS = 200; // tweak speed (ms)

static const char *ELD_BABY_SLEEP_FRAMES[4] = {
    "/raising_hell/graphics/pet/anim/eld/bb/sleep/eld_bb_sleepbk1.png",
    "/raising_hell/graphics/pet/anim/eld/bb/sleep/eld_bb_sleepbk2.png",
    "/raising_hell/graphics/pet/anim/eld/bb/sleep/eld_bb_sleepbk3.png",
    "/raising_hell/graphics/pet/anim/eld/bb/sleep/eld_bb_sleepbk4.png",
};

static inline bool useEldBabySleepAnim() { return (pet.type == PET_ELDRITCH) && (pet.evoStage == 0); }

// -----------------------------------------------------------------------------
// ELDRITCH TEEN sleep background animation (3 PNG frames)
// -----------------------------------------------------------------------------
static constexpr uint32_t ELD_TEEN_SLEEP_FRAME_MS = 200;

static constexpr uint8_t ELD_TEEN_SLEEP_FRAME_COUNT = 3;

static const char *ELD_TEEN_SLEEP_FRAMES[ELD_TEEN_SLEEP_FRAME_COUNT] = {
    "/raising_hell/graphics/pet/anim/eld/tn/sleep/eld_tn_sleepbk1.png",
    "/raising_hell/graphics/pet/anim/eld/tn/sleep/eld_tn_sleepbk2.png",
    "/raising_hell/graphics/pet/anim/eld/tn/sleep/eld_tn_sleepbk3.png",
};

static inline bool useEldTeenSleepAnim() { return (pet.type == PET_ELDRITCH) && (pet.evoStage == 1); }

// -----------------------------------------------------------------------------
// ELDRITCH ADULT sleep background animation (4 PNG frames)
// -----------------------------------------------------------------------------
static constexpr uint32_t ELD_ADULT_SLEEP_FRAME_MS = 180;

static const char *ELD_ADULT_SLEEP_FRAMES[4] = {
    "/raising_hell/graphics/pet/anim/eld/ad/sleep/eld_ad_sleepbk1.png",
    "/raising_hell/graphics/pet/anim/eld/ad/sleep/eld_ad_sleepbk2.png",
    "/raising_hell/graphics/pet/anim/eld/ad/sleep/eld_ad_sleepbk3.png",
    "/raising_hell/graphics/pet/anim/eld/ad/sleep/eld_ad_sleepbk4.png",
};

static inline bool useEldAdultSleepAnim() { return (pet.type == PET_ELDRITCH) && (pet.evoStage == 2); }

// -----------------------------------------------------------------------------
// ELDRITCH ELDER sleep background animation (4 PNG frames)
// -----------------------------------------------------------------------------
static constexpr uint32_t ELD_ELDER_SLEEP_FRAME_MS = 180;

static const char *ELD_ELDER_SLEEP_FRAMES[4] = {
    "/raising_hell/graphics/pet/anim/eld/ed/sleep/eld_ed_sleepbk1.png",
    "/raising_hell/graphics/pet/anim/eld/ed/sleep/eld_ed_sleepbk2.png",
    "/raising_hell/graphics/pet/anim/eld/ed/sleep/eld_ed_sleepbk3.png",
    "/raising_hell/graphics/pet/anim/eld/ed/sleep/eld_ed_sleepbk4.png",
};

static inline bool useEldElderSleepAnim() { return (pet.type == PET_ELDRITCH) && (pet.evoStage == 3); }

// -----------------------------------------------------------------------------
// Sleep animation frame cache (RGB565 full-screen sprite buffer snapshots)
// (Renamed to avoid colliding with existing ensureSleepFrameCache in this file.)
// -----------------------------------------------------------------------------
static uint16_t **s_sleepAnimFrameCache = nullptr;
static uint8_t s_sleepAnimFrameCacheCnt = 0;
static uint8_t s_sleepAnimFrameCacheMode = 0; // 1=baby,2=teen,3=adult,4=elder
static bool s_sleepAnimFrameCacheReady = false;

static void freeSleepAnimFrameCache()
{
  if (s_sleepAnimFrameCache)
  {
    for (uint8_t i = 0; i < s_sleepAnimFrameCacheCnt; ++i)
    {
      if (s_sleepAnimFrameCache[i])
      {
        free(s_sleepAnimFrameCache[i]);
        s_sleepAnimFrameCache[i] = nullptr;
      }
    }
    free(s_sleepAnimFrameCache);
    s_sleepAnimFrameCache = nullptr;
  }
  s_sleepAnimFrameCacheCnt = 0;
  s_sleepAnimFrameCacheMode = 0;
  s_sleepAnimFrameCacheReady = false;
}

void graphicsReleaseUiCachesForMiniGame()
{
  // Release pet-area cached sprite.
  petLayer.deleteSprite();
  petLayerReady = false;

  g_petBgCachedPath = nullptr;
  g_petBgCachedType = (PetType)255;
  g_petBgCachedStage = 255;
  g_forcePetBgCache = true;

  s_nonPetTile.deleteSprite();
  s_nonPetTileReady = false;
  s_nonPetTileW = 0;
  s_nonPetTileH = 0;
  s_nonPetTileCachedType = (PetType)255;

  // Release cached sleep animation full-screen frame buffers.
  freeSleepAnimFrameCache();

  // Force the UI to rebuild cleanly when we return.
  bgDrawnForState = false;
  lastDrawnState = (UIState)255;
  invalidateBackgroundCache();
}

static bool ensureSleepAnimFrameCache(uint8_t mode, const char *const *frames, uint8_t frameCount, int drawX, int drawY)
{
  if (mode == 0 || !frames || frameCount == 0)
    return false;

  // No PSRAM on this hardware. Full-screen cached sleep frames are too large
  // and can starve later graphics/WiFi allocations.
  // Fall back to drawing sleep frames live instead of caching snapshots.
  Serial.println("[SLEEP CACHE] disabled on no-PSRAM build");
  return false;

  if (s_sleepAnimFrameCacheReady && s_sleepAnimFrameCache && s_sleepAnimFrameCacheMode == mode &&
      s_sleepAnimFrameCacheCnt == frameCount)
  {
    return true;
  }

  const size_t pxCount = (size_t)SCREEN_W * (size_t)SCREEN_H;
  const size_t bufBytes = pxCount * sizeof(uint16_t);
  const size_t totalNeeded = bufBytes * frameCount;

  if (ESP.getPsramSize() == 0 || heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) < (totalNeeded + 4096))
  {
    return false;
  }

  freeSleepAnimFrameCache();

  uint16_t *sprBuf = (uint16_t *)spr.getBuffer();
  if (!sprBuf)
    return false;

  s_sleepAnimFrameCache = (uint16_t **)calloc(frameCount, sizeof(uint16_t *));
  if (!s_sleepAnimFrameCache)
    return false;

  for (uint8_t i = 0; i < frameCount; ++i)
  {
    s_sleepAnimFrameCache[i] = (uint16_t *)malloc(bufBytes);
    if (!s_sleepAnimFrameCache[i])
    {
      freeSleepAnimFrameCache();
      return false;
    }

    spr.fillRect(0, 0, SCREEN_W, SCREEN_H, TFT_BLACK);

    bool ok = false;
    if (g_sdReady && frames[i])
    {
      const char *ext = strrchr(frames[i], '.');
      const bool isPng = (ext && (strcasecmp(ext, ".png") == 0));
      if (isPng)
        ok = sprDrawPngFromSD(frames[i], drawX, drawY);
      else
        ok = sprDrawJpgFromSD(frames[i], drawX, drawY);
    }

    if (!ok)
    {
      freeSleepAnimFrameCache();
      return false;
    }

    memcpy(s_sleepAnimFrameCache[i], sprBuf, bufBytes);
  }

  s_sleepAnimFrameCacheCnt = frameCount;
  s_sleepAnimFrameCacheMode = mode;
  s_sleepAnimFrameCacheReady = true;
  return true;
}

static void drawSleepScreenImpl(bool redrawBg)
{
  if (!isScreenOn())
    return;

  static uint8_t s_frame = 0;
  static uint32_t s_nextFrameMs = 0;
  static bool s_hasBg = false;

  static uint8_t s_mode = 0;

  const uint32_t now = millis();

  const bool kick = g_sleepBgKick;
  if (kick)
    g_sleepBgKick = false;

  const bool wakeKick = g_sleepBgWakeKick;
  if (wakeKick)
    g_sleepBgWakeKick = false;

  const bool babyAnim = useDevBabySleepAnim();
  const bool teenAnim = useDevTeenSleepAnim();
  const bool adultAnim = useDevAdultSleepAnim();
  const bool elderAnim = useDevElderSleepAnim();
  const bool eldBabyAnim = useEldBabySleepAnim();
  const bool eldTeenAnim = useEldTeenSleepAnim();
  const bool eldAdultAnim = useEldAdultSleepAnim();
  const bool eldElderAnim = useEldElderSleepAnim();

  uint8_t newMode = 0;

  if (babyAnim)
    newMode = 1;
  else if (teenAnim)
    newMode = 2;
  else if (adultAnim)
    newMode = 3;
  else if (elderAnim)
    newMode = 4;
  else if (eldBabyAnim)
    newMode = 5;
  else if (eldTeenAnim)
    newMode = 6;
  else if (eldAdultAnim)
    newMode = 7;
  else if (eldElderAnim)
    newMode = 8;

  if (newMode != s_mode)
  {
    s_mode = newMode;
    s_frame = 0;
    s_nextFrameMs = 0;
    s_hasBg = false;
    redrawBg = true;
    freeSleepAnimFrameCache();
  }

  bool frameChanged = false;

  const char *bgPath = nullptr;

  static uint8_t s_lastMode = 0;
  static bool s_animInited = false;

  const bool modeChanged = (s_mode != s_lastMode);

  const char *const *frames = nullptr;
  uint8_t frameCount = 0;
  uint32_t frameMs = 0;

  switch (s_mode)
  {
  case 1:
    frames = DEV_BABY_SLEEP_FRAMES;
    frameCount = sizeof(DEV_BABY_SLEEP_FRAMES) / sizeof(DEV_BABY_SLEEP_FRAMES[0]);
    frameMs = DEV_BABY_SLEEP_FRAME_MS;
    break;
  case 2:
    frames = DEV_TEEN_SLEEP_FRAMES;
    frameCount = sizeof(DEV_TEEN_SLEEP_FRAMES) / sizeof(DEV_TEEN_SLEEP_FRAMES[0]);
    frameMs = DEV_TEEN_SLEEP_FRAME_MS;
    break;
  case 3:
    frames = DEV_ADULT_SLEEP_FRAMES;
    frameCount = sizeof(DEV_ADULT_SLEEP_FRAMES) / sizeof(DEV_ADULT_SLEEP_FRAMES[0]);
    frameMs = DEV_ADULT_SLEEP_FRAME_MS;
    break;
  case 4:
    frames = DEV_ELDER_SLEEP_FRAMES;
    frameCount = sizeof(DEV_ELDER_SLEEP_FRAMES) / sizeof(DEV_ELDER_SLEEP_FRAMES[0]);
    frameMs = DEV_ELDER_SLEEP_FRAME_MS;
    break;
  case 5:
    frames = ELD_BABY_SLEEP_FRAMES;
    frameCount = sizeof(ELD_BABY_SLEEP_FRAMES) / sizeof(ELD_BABY_SLEEP_FRAMES[0]);
    frameMs = ELD_BABY_SLEEP_FRAME_MS;
    break;
  case 6:
    frames = ELD_TEEN_SLEEP_FRAMES;
    frameCount = sizeof(ELD_TEEN_SLEEP_FRAMES) / sizeof(ELD_TEEN_SLEEP_FRAMES[0]);
    frameMs = ELD_TEEN_SLEEP_FRAME_MS;
    break;
  case 7:
    frames = ELD_ADULT_SLEEP_FRAMES;
    frameCount = sizeof(ELD_ADULT_SLEEP_FRAMES) / sizeof(ELD_ADULT_SLEEP_FRAMES[0]);
    frameMs = ELD_ADULT_SLEEP_FRAME_MS;
    break;
  case 8:
    frames = ELD_ELDER_SLEEP_FRAMES;
    frameCount = sizeof(ELD_ELDER_SLEEP_FRAMES) / sizeof(ELD_ELDER_SLEEP_FRAMES[0]);
    frameMs = ELD_ELDER_SLEEP_FRAME_MS;
    break;
  default:
    bgPath = sleepBgForPet(pet.type);
    s_lastMode = s_mode;
    break;
  }

  const bool anyKick = (kick || wakeKick);

  if (anyKick && frames && frameCount > 0 && frameMs > 0)
  {
    s_animInited = true;
    s_nextFrameMs = now;

    if (frameCount > 1)
    {
      s_frame = (uint8_t)((s_frame + 1) % frameCount);
      frameChanged = true;
    }

    s_hasBg = false;
  }

  if (frames && frameCount > 0 && frameMs > 0)
  {
    if (!s_animInited || modeChanged)
    {
      s_animInited = true;

      if (s_nextFrameMs == 0)
        s_frame = 0;

      s_nextFrameMs = now;
      frameChanged = true;
      s_hasBg = false;

      freeSleepAnimFrameCache();
    }
    else
    {
      const int32_t late = (int32_t)(now - s_nextFrameMs);
      if (late >= 0)
      {
        uint32_t steps = 1u + (uint32_t)late / (uint32_t)frameMs;
        if (steps > frameCount)
          steps = frameCount;

        s_frame = (uint8_t)((s_frame + steps) % frameCount);
        s_nextFrameMs += steps * frameMs;
        frameChanged = true;
      }
    }

    bgPath = frames[s_frame];
  }

  s_lastMode = s_mode;

  g_sleepAnimActive = (frames && frameCount > 0 && frameMs > 0);
  g_sleepAnimNextFrameMs = (g_sleepAnimActive ? s_nextFrameMs : 0);

  const bool needBgDraw = redrawBg || frameChanged || !s_hasBg;

  if (needBgDraw)
  {
    bool ok = false;
    Serial.printf("[HEAPCHK] sleep-draw pre-bg free=%u largest=%u\n",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));

    if (s_mode != 0 && frames && frameCount > 0)
    {
      if (ensureSleepAnimFrameCache(s_mode, frames, frameCount, 0, 18))
      {
        uint16_t *sprBuf = (uint16_t *)spr.getBuffer();
        if (sprBuf && s_sleepAnimFrameCache && s_sleepAnimFrameCache[s_frame])
        {
          const size_t pxCount = (size_t)SCREEN_W * (size_t)SCREEN_H;
          memcpy(sprBuf, s_sleepAnimFrameCache[s_frame], pxCount * sizeof(uint16_t));
          ok = true;
        }
      }
    }

    if (!ok)
    {
      if (g_sdReady && bgPath)
      {
        const char *ext = strrchr(bgPath, '.');
        const bool isPng = (ext && (strcasecmp(ext, ".png") == 0));
        if (isPng)
          ok = sprDrawPngFromSD(bgPath, 0, 18);
        else
          ok = sprDrawJpgFromSD(bgPath, 0, 18);
      }
    }

    if (!ok)
    {
      spr.fillRect(0, 0, SCREEN_W, SCREEN_H, TFT_BLACK);
      s_hasBg = false;
    }
    else
    {
      s_hasBg = true;
    }
  }

  drawTopBar();
  drawMiniStatPreviewSleepLeft();
  drawSleepMeterBar();
}

// ============================================================================
// Tiny stat preview panel
// ============================================================================
static void drawTinyBar(int x, int y, int w, int h, uint16_t fill, uint16_t outline, int value01_100, const char *label)
{
  value01_100 = clampi(value01_100, 0, 100);

  const int r = h / 2;
  const int innerX = x + 1;
  const int innerY = y + 1;
  const int innerW = w - 2;
  const int innerH = h - 2;
  const int fillW = (innerW * value01_100) / 100;

  // Outer pill
  spr.fillRoundRect(x, y, w, h, r, outline);

  // Inner dark track
  spr.fillRoundRect(innerX, innerY, innerW, innerH, (innerH / 2), TFT_BLACK);

  // Fill with flat right edge
  if (fillW > 0)
  {
    int fw = fillW;
    if (fw < innerH)
      fw = innerH; // keep tiny values visible as a nub
    if (fw > innerW)
      fw = innerW;

    spr.fillRect(innerX, innerY, fw, innerH, fill);
  }

  // Centered label
  if (label && label[0])
  {
    spr.setTextFont(1);
    spr.setTextSize(1);
    spr.setTextColor(TFT_WHITE);
    spr.setTextDatum(MC_DATUM);
    spr.drawString(label, x + w / 2, y + h / 2);
    spr.setTextDatum(TL_DATUM);
  }
}

static void drawTinyBar(int x, int y, int w, int h, uint16_t fill, uint16_t outline, int value01_100)
{
  drawTinyBar(x, y, w, h, fill, outline, value01_100, nullptr);
}

static void drawTinyBarV(int x, int y, int w, int h, uint16_t fill, uint16_t outline, int value01_100)
{
  value01_100 = clampi(value01_100, 0, 100);

  spr.drawRect(x, y, w, h, outline);

  const int innerW = w - 2;
  const int innerH = h - 2;
  const int fillH = (innerH * value01_100) / 100;

  spr.fillRect(x + 1, y + 1, innerW, innerH, TFT_BLACK);

  const int fy = y + 1 + (innerH - fillH);
  spr.fillRect(x + 1, fy, innerW, fillH, fill);
}

static void drawMiniStatPreviewAt(int x0, bool showCoin, bool alignRight)
{
  const int panelW = 72;

  // Layout
  const int headerY = PET_AREA_Y + 2;

  // Stat block
  const int barH = 14;
  const int rowGap = 4;
  const int rowH = barH + rowGap;

  const uint16_t colHunger = 0xF800;
  const uint16_t colMood = 0x001F;
  const uint16_t colEnergy = 0x03E0;

  // Bars first
  const int y0 = headerY + 4;
  const int barX = x0 + 2;
  const int barW = panelW - 4;

  const int yHunger = y0 + 0 * rowH;
  const int yMood = y0 + 1 * rowH;
  const int yRest = y0 + 2 * rowH;

  drawTinyBar(barX, yHunger, barW, barH, colHunger, colHunger, pet.hunger, "Hunger");
  drawTinyBar(barX, yMood, barW, barH, colMood, colMood, pet.happiness, "Mood");
  drawTinyBar(barX, yRest, barW, barH, colEnergy, colEnergy, pet.energy, "Rest");

  // Bottom header: coin/count on left, heart/HP on right
  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);

  const int headerY2 = headerY + 74;
  const int headerIconY = headerY2 + 0;
  const int topTextY = headerY2 + 1;

  // Define the right-side heart anchor first so coin text can avoid it
  const int heartIconX = x0 + panelW - 2 - 16 - 28;

  // Left side: coin + count (count grows left, icon follows it)
  if (showCoin)
  {
    char infBuf[20];
    snprintf(infBuf, sizeof(infBuf), "%d", pet.inf);

    // Fixed right edge for coin text, safely left of the heart block
    const int coinRightX = heartIconX - 6;

    spr.setTextDatum(TR_DATUM);

    // Measure count width using current font/settings
    const int coinTextW = spr.textWidth(infBuf);

    // Keep a small gap between icon and number
    const int coinGap = 6;

    // Place icon so it sits just left of the text block
    const int coinIconX = coinRightX - coinTextW - coinGap - 16;

    drawMiniStatIconCached(PATH_INF_COIN, coinIconX, headerIconY);

    // fake-bold / slightly larger-looking text
    spr.drawString(infBuf, coinRightX, topTextY);
    spr.drawString(infBuf, coinRightX - 1, topTextY);

    spr.setTextDatum(TL_DATUM);
  }

  // Right side: heart + HP
  {
    char hpBuf[16];
    snprintf(hpBuf, sizeof(hpBuf), "%d", pet.health);

    drawMiniStatIconCached(PATH_LIFE_ICON, heartIconX, headerIconY);

    spr.setTextDatum(TL_DATUM);

    const int hpTextX = x0 + panelW - 2 - spr.textWidth(hpBuf);

    // fake-bold / slightly larger-looking text
    spr.drawString(hpBuf, hpTextX, topTextY);
    spr.drawString(hpBuf, hpTextX + 1, topTextY);
  }

  spr.setTextDatum(TL_DATUM);
}

static void drawMiniStatPreview()
{
  const int panelW = 72;
  const int x0 = SCREEN_W - panelW - 4;
  drawMiniStatPreviewAt(x0, /*showCoin=*/true, /*alignRight=*/true);
}

static void drawMiniStatPreviewSleepLeft()
{
  const int x0 = 4;
  const int panelW = 72;

  // Layout
  const int headerY = PET_AREA_Y + 2;

  // Stat block
  const int barH = 14;
  const int rowGap = 4;
  const int rowH = barH + rowGap;

  const uint16_t colHunger = 0xF800;
  const uint16_t colMood = 0x001F;
  const uint16_t colEnergy = 0x03E0;

  // Bars near the top
  const int y0 = headerY + 4;
  const int barX = x0 + 2;
  const int barW = panelW - 4;

  const int yHunger = y0 + 0 * rowH;
  const int yMood = y0 + 1 * rowH;
  const int yRest = y0 + 2 * rowH;

  drawTinyBar(barX, yHunger, barW, barH, colHunger, colHunger, pet.hunger, "Hunger");
  drawTinyBar(barX, yMood, barW, barH, colMood, colMood, pet.happiness, "Mood");
  drawTinyBar(barX, yRest, barW, barH, colEnergy, colEnergy, pet.energy, "Rest");

  // Bottom footer: coin/count on left, heart/HP on right
  // On the left-side cluster, both counts should expand to the right.
  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);

  const int headerY2 = headerY + 64;
  const int headerIconY = headerY2 + 0;
  const int topTextY = headerY2 + 1;

  {
    const int heartIconX = x0 + 2;

    drawMiniStatIconCached(PATH_LIFE_ICON, heartIconX, headerIconY);

    char hpBuf[16];
    snprintf(hpBuf, sizeof(hpBuf), "%d", pet.health);

    const int hpTextX = heartIconX + 18;

    spr.setTextDatum(TL_DATUM);

    // fake-bold
    spr.drawString(hpBuf, hpTextX, topTextY);
    spr.drawString(hpBuf, hpTextX + 1, topTextY);
  }

  spr.setTextDatum(TL_DATUM);
}

// ============================================================================
// Console
// ============================================================================
static constexpr int CONSOLE_INPUT_H = TAB_BAR_H;
static constexpr int CONSOLE_PAD_X = 4;
static constexpr int CONSOLE_PAD_Y = 2;
static constexpr int CONSOLE_INPUT_FONT = 2;

void drawConsoleMenu()
{
  drawTopBar();
  spr.fillRect(0, PET_AREA_Y, SCREEN_W, PET_AREA_H, TFT_BLACK);

  spr.setTextDatum(TL_DATUM);
  spr.setTextFont(1);
  spr.setTextSize(1);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);

  spr.drawString("CONSOLE", 6, PET_AREA_Y + 6);
  spr.drawString("Type in Serial Monitor", 6, PET_AREA_Y + 18);
  spr.drawString("ESC: Back", 6, PET_AREA_Y + 30);

  drawTabBar();
}

void drawConsoleScreen()
{
  drawTopBar();

  const int outY = TOP_BAR_H;
  const int outH = SCREEN_H - TOP_BAR_H - CONSOLE_INPUT_H;
  const int inY = TOP_BAR_H + outH;

  const PetUIColorScheme ui = uiSchemeForPet(pet.type);
  const uint16_t inputBg = ui.topBg;
  const uint16_t inputLine = ui.topOutline;

  spr.fillRect(0, outY, SCREEN_W, outH, TFT_BLACK);
  spr.fillRect(0, inY, SCREEN_W, CONSOLE_INPUT_H, inputBg);
  spr.drawFastHLine(0, inY, SCREEN_W, inputLine);

  spr.setTextFont(1);
  spr.setTextSize(1);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.setTextDatum(TL_DATUM);

  const int lineH = 10;
  const int maxLinesVisible = outH / lineH;

  const int total = consoleGetLineCount();
  int first = total - maxLinesVisible;
  if (first < 0)
    first = 0;

  int y = outY + 2;
  for (int i = first; i < total; i++)
  {
    const char *s = consoleGetLine(i);
    if (s && *s)
      spr.drawString(s, CONSOLE_PAD_X, y);
    y += lineH;
  }

  spr.setTextFont(CONSOLE_INPUT_FONT);
  spr.setTextSize(1);
  spr.setTextColor(TFT_WHITE, inputBg);
  spr.setTextDatum(TL_DATUM);

  const char *in = consoleGetInputLine();
  if (!in)
    in = "";

  char full[256];
  snprintf(full, sizeof(full), "> %s", in);

  const int x0 = CONSOLE_PAD_X;
  const int y0 = inY + CONSOLE_PAD_Y;

  const int maxPx = SCREEN_W - (CONSOLE_PAD_X * 2);

  const char *shown = full;
  while (*shown && spr.textWidth(shown) > maxPx)
  {
    shown++;
  }

  spr.drawString(shown, x0, y0);

  spr.setTextFont(1);
  spr.setTextSize(1);
}

// ============================================================================
// WiFi setup screen
// ============================================================================
static void drawWifiSetupScreen()
{

  if (g_wifi.setupStage == WIFI_SETUP_STAGE_SCAN)
  {
    drawTopBar();

    const int contentY = TOP_BAR_H;
    const int contentH = SCREEN_H - TOP_BAR_H;
    spr.fillRect(0, contentY, SCREEN_W, contentH, TFT_BLACK);

    spr.setTextFont(2);
    spr.setTextSize(1);

    if (g_wifi.scanInProgress)
    {
      spr.setTextDatum(CC_DATUM);
      spr.setTextColor(TFT_WHITE, TFT_BLACK);
      spr.drawString("Scanning WiFi...", SCREEN_W / 2, contentY + 16);
      spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
      spr.drawString("Please wait", SCREEN_W / 2, contentY + 36);
      spr.setTextDatum(TL_DATUM);
      return;
    }

    const bool hasResults = (g_wifi.scanCount > 0);
    const int totalItems = hasResults ? (g_wifi.scanCount + 1) : 2;
    const int itemH = 20;
    const int gap = 5;
    const int maxVisible = 5;

    int start = 0;
    int visCount = totalItems;
    if (visCount > maxVisible)
      visCount = maxVisible;

    if (g_wifi.scanIndex < start)
      start = g_wifi.scanIndex;
    if (g_wifi.scanIndex >= start + visCount)
      start = g_wifi.scanIndex - visCount + 1;

    const int totalH = visCount * itemH + (visCount - 1) * gap;
    const int startY = contentY + (contentH - totalH) / 2;

    const int boxW = (SCREEN_W * 3) / 4;
    const int boxX = (SCREEN_W - boxW) / 2;
    const int radius = 8;

    for (int row = 0; row < visCount; ++row)
    {
      const int i = start + row;
      int y = startY + row * (itemH + gap);

      const bool sel = (i == g_wifi.scanIndex);

      const uint16_t outline = sel ? uiPillOutline(pet.type) : TFT_DARKGREY;
      const uint16_t fill = sel ? uiPillFillSelected(pet.type) : TFT_BLACK;
      const uint16_t textCol = sel ? TFT_WHITE : TFT_LIGHTGREY;

      spr.fillRoundRect(boxX, y, boxW, itemH, radius, fill);
      spr.drawRoundRect(boxX, y, boxW, itemH, radius, outline);

      char line[48];

      if (!hasResults)
      {
        if (i == 0)
        {
          snprintf(line, sizeof(line), "Scan for networks");
        }
        else
        {
          snprintf(line, sizeof(line), "Manual entry");
        }
      }
      else
      {
        if (i < g_wifi.scanCount)
        {
          snprintf(line, sizeof(line), "%s (%d)", g_wifi.scanSsids[i], (int)g_wifi.scanRssi[i]);
        }
        else
        {
          snprintf(line, sizeof(line), "Manual entry");
        }
      }

      spr.setTextDatum(TL_DATUM);
      spr.setTextColor(textCol, fill);
      const int th = spr.fontHeight();
      const int ty = y + (itemH - th) / 2;
      spr.drawString(line, boxX + 8, ty);
    }

    return;
  }

  const bool isPass = (g_wifi.setupStage == WIFI_SETUP_STAGE_PASS);
  ui_drawMessageWindow("WiFi Setup", isPass ? "Password:" : "SSID:", wifiSetupBuf,
                       /*maskLine2=*/isPass,
                       /*showCursor=*/true);
}

static void drawWifiConnectWaitScreen()
{
  drawTopBar();

  const int contentY = TOP_BAR_H;
  const int contentH = SCREEN_H - TOP_BAR_H;
  spr.fillRect(0, contentY, SCREEN_W, contentH, TFT_BLACK);

  spr.setTextDatum(TL_DATUM);
  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);

  const char *ssid = wifiConsoleSsid();
  const uint32_t ageMs = wifiConsoleConnectAgeMs();
  const uint32_t ageS = ageMs / 1000;

  const int wl = WiFi.status();
  const bool reallyConnected = (wl == WL_CONNECTED);
  const int liveRssi = reallyConnected ? WiFi.RSSI() : 0;

  const char *st = nullptr;
  switch (wl)
  {
  case WL_CONNECTED:
    st = "Connected";
    break;
  case WL_IDLE_STATUS:
    st = "Idle";
    break;
  case WL_NO_SSID_AVAIL:
    st = "SSID not found";
    break;
  case WL_CONNECT_FAILED:
    st = "Connect failed";
    break;
  case WL_CONNECTION_LOST:
    st = "Connection lost";
    break;
  case WL_DISCONNECTED:
    st = "Disconnected";
    break;
  default:
    st = "Connecting...";
    break;
  }

  spr.drawString("Connecting WiFi...", 10, contentY + 10);

  if (ssid && ssid[0])
    spr.drawString((String("SSID: ") + ssid).c_str(), 10, contentY + 28);
  else if (wifiSetupSsid[0])
    spr.drawString((String("SSID: ") + String(wifiSetupSsid)).c_str(), 10, contentY + 28);
  else
    spr.drawString("SSID: (none)", 10, contentY + 28);

  spr.drawString((String("Status: ") + st).c_str(), 10, contentY + 46);
  spr.drawString((String("Elapsed: ") + String(ageS) + "s").c_str(), 10, contentY + 64);

  if (reallyConnected)
  {
    spr.drawString("WiFi connected", 10, contentY + 86);
    spr.drawString((String("RSSI: ") + String(liveRssi)).c_str(), 10, contentY + 104);
  }
  else
  {
    if (ageS >= 15)
    {
      spr.drawString("Still not connected.", 10, contentY + 92);
      spr.drawString("Check password/signal.", 10, contentY + 110);
    }
    else
    {
      spr.drawString("Not connected yet", 10, contentY + 92);
    }
  }

  spr.setTextDatum(CC_DATUM);
}

// ============================================================================
// MAIN RENDER DISPATCHER HELPER
// ============================================================================
void startPetScreenIntroFadeNow()
{
  s_petScreenIntroFadeActive = true;
  s_petScreenIntroFadeStartMs = millis();
}

static void tickPetScreenIntroFade()
{
  if (g_app.uiState != UIState::PET_SCREEN)
  {
    s_petScreenIntroFadeActive = false;
    g_app.petScreenIntroFadePending = false;
    return;
  }

  if (g_app.petScreenIntroFadePending && !s_petScreenIntroFadeActive)
  {
    g_app.petScreenIntroFadePending = false;
    s_petScreenIntroFadeActive = true;
    s_petScreenIntroFadeStartMs = millis();

    // Ensure we start from black
    forceBacklightDuringFade(0);
    requestUIRedraw();
    return;
  }

  if (!s_petScreenIntroFadeActive)
    return;

  const uint32_t elapsed = millis() - s_petScreenIntroFadeStartMs;
  const uint8_t targetBrightness = (uint8_t)brightnessValues[brightnessLevel];

  if (elapsed >= kPetScreenIntroFadeMs)
  {
    s_petScreenIntroFadeActive = false;
    forceBacklightDuringFade(targetBrightness);
    return;
  }

  const uint8_t fadeBrightness = (uint8_t)(((uint32_t)targetBrightness * elapsed) / kPetScreenIntroFadeMs);

  forceBacklightDuringFade(fadeBrightness);
  requestUIRedraw();
}

bool isPetScreenIntroFadeActive() { return s_petScreenIntroFadeActive; }

void forceRenderUIOnce()
{
  g_app.lastRenderTimeMs = 0;
  requestUIRedraw();
  renderUI();
}

static bool uiIsBootWifiOnboardingState(UIState s)
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

static bool uiStateBlocksOverlays(UIState s)
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
    return true;
  default:
    return false;
  }
}

// ============================================================================
// Current Screen Driver
// ============================================================================
static void drawTabDrivenScreen(bool redrawBg)
{
  switch (g_app.currentTab)
  {
  case Tab::TAB_PET:
    drawPetScreenImpl(redrawBg);
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
    drawPetScreenImpl(redrawBg);
    break;
  }
}

static void drawCurrentScreen(bool redrawBg)
{
  switch (g_app.uiState)
  {
  case UIState::DEATH:
    drawDeathScreen(redrawBg);
    return;

  case UIState::BURIAL_SCREEN:
    drawBurialScreen();
    return;

  case UIState::DEATH_TRANSITION:
    drawDeathTransitionScreen(redrawBg);
    return;

  case UIState::PET_SLEEPING:
    drawSleepScreenImpl(redrawBg);
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
    // Draw the mini-game frame underneath, then overlay the pause menu UI.
    drawMiniGameScreen();
    mgDrawPauseOverlay();
    return;

  default:
    break;
  }

  // Non-exclusive “normal” states
  switch (g_app.uiState)
  {
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
    // Overlay only; do NOT redraw anything behind it here.
    // renderUI() will call drawPowerMenu() after this function returns.
    break;

  case UIState::BOOT:
    drawBootSplash();
    return;

  case UIState::HOME:
    drawTabDrivenScreen(redrawBg);
    break;

  case UIState::PET_SCREEN:
  default:
    drawTabDrivenScreen(redrawBg);
    break;
  }

  // If console is open and this state allows overlays, draw it on top.
  if (!uiStateBlocksOverlays(g_app.uiState) && consoleIsOpen())
  {
    drawConsoleScreen();
  }
}

// ============================================================================
// MAIN RENDER DISPATCHER
// ============================================================================
void renderUI()
{
  if (!isScreenOn())
    return;

  if (g_bootSplashActive)
  {
    drawSplashScreen(true);
    spr.pushSprite(0, 0);
    return;
  }

  if ((g_bootUiBlockedForAssetProvision || g_bootAssetProvisionActive) && g_app.uiState != UIState::CONSOLE &&
      !uiIsBootWifiOnboardingState(g_app.uiState))
  {
    drawBootAssetProvisionScreen("Preparing asset check.", "Please wait...");
    spr.pushSprite(0, 0);
    return;
  }

  static int lastTab = -1;

  const int tabNow = (int)g_app.currentTab;
  const bool tabChanged = (tabNow != lastTab);
  const bool stateChanged = (g_app.uiState != lastDrawnState);

  const bool bgInvalid = backgroundCacheInvalidated();
  consumeBackgroundInvalidation();

  if (tabChanged)
  {
    bgDrawnForState = false;
  }

  if (stateChanged)
  {
    bgDrawnForState = false;
  }

  if (tabChanged || stateChanged || bgInvalid)
  {
    requestUIRedraw();
  }

  // Always advance pet-side timers/state before deciding whether to throttle.
  tickPetScreenIntroFade();
  tickPetIntroWalk();
  tickPetWander();
  
  const bool redrawRequested = consumeUIRedrawRequest();
  const bool petAnimating =
      (g_app.uiState == UIState::PET_SCREEN) &&
      (g_app.petScreenIntroFadePending || isPetScreenIntroFadeActive() || s_petIntroWalkActive ||
       s_petIntroArriveTurnActive || s_petIntroStandHoldActive || s_petIntroHandoffActive ||
       s_petWanderState == PetWanderState::MOVING_TO_SIDE_A || s_petWanderState == PetWanderState::MOVING_TO_SIDE_B ||
       s_petWanderState == PetWanderState::RETURNING_HOME);
  
  if (!tabChanged && !stateChanged && !bgInvalid && !redrawRequested && !petAnimating)
  {
    const uint32_t now = millis();
    const uint32_t gateMs = consoleIsOpen() ? 16 : 50;
    if (now - lastRenderTimeMs < gateMs)
      return;
    lastRenderTimeMs = now;
  }
  else
  {
    lastRenderTimeMs = millis();
  }
  
  lastTab = tabNow;

  const bool petScreenNow = (g_app.uiState == UIState::PET_SCREEN);
  const bool petScreenJustEntered = petScreenNow && !s_petScreenWasActiveLastFrame;
  s_petScreenWasActiveLastFrame = petScreenNow;

  // Existing pets should start at home in their normal mood animation.
  // Only preserve offscreen/intro positioning when the scripted hatch intro
  // is actually active.
  if (petScreenJustEntered)
  {
    const bool scriptedIntroActive =
        s_petIntroWalkActive || s_petIntroArriveTurnActive || s_petIntroStandHoldActive || s_petIntroHandoffActive;

    if (!scriptedIntroActive)
    {
      resetPetScreenPositionToHome();
      requestUIRedraw();
    }
  }

  const bool petMotionActive = s_petIntroWalkActive || s_petIntroArriveTurnActive || s_petIntroStandHoldActive ||
                               s_petIntroHandoffActive || s_petWanderState == PetWanderState::MOVING_TO_SIDE_A ||
                               s_petWanderState == PetWanderState::MOVING_TO_SIDE_B ||
                               s_petWanderState == PetWanderState::RETURNING_HOME;

  const bool redrawBg = (!bgDrawnForState) || bgInvalid || petMotionActive;

  // Update pet intro fade state before drawing/presenting this frame.
  tickPetScreenIntroFade();
  tickPetIntroWalk();
  tickPetWander();

  drawCurrentScreen(redrawBg);

  if (g_app.uiState == UIState::POWER_MENU)
  {
    drawPowerMenu();
  }

  if (uiIsLevelUpPopupActive())
  {
    uiDrawLevelUpPopup();
  }

  uiDrawToastOverlay();

  // draw overlays
  if (assetOtaConfirmActive())
  {
    drawAssetOtaConfirmOverlay();
  }

  spr.pushSprite(0, 0);

  bgDrawnForState = true;
  lastDrawnState = g_app.uiState;
}

// ============================================================================
// UI: message window (modal)
// ============================================================================
void ui_drawMessageWindow(const char *title, const char *line1, const char *line2, bool maskLine2, bool showCursor)
{
  if (!isScreenOn())
    return;

  spr.fillRect(0, 0, screenW, screenH, TFT_BLACK);

  const uint16_t modalOutline = uiModalOutline(pet.type);

  const int pad = 10;
  const int boxW = screenW - (pad * 2);
  const int boxH = 74;
  const int x = pad;
  const int y = (screenH - boxH) / 2;

  spr.fillRoundRect(x, y, boxW, boxH, 8, TFT_BLACK);
  spr.drawRoundRect(x, y, boxW, boxH, 8, modalOutline);

  spr.setTextDatum(TC_DATUM);
  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.drawString(title ? title : "", screenW / 2, y + 8);

  spr.setTextDatum(TC_DATUM);
  spr.setTextFont(1);
  spr.setTextSize(1);
  spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  spr.drawString(line1 ? line1 : "", screenW / 2, y + 28);

  char shown[40];
  shown[0] = '\0';

  if (line2)
  {
    if (maskLine2)
    {
      size_t n = strnlen(line2, 32);
      if (n > 32)
        n = 32;
      for (size_t i = 0; i < n; i++)
        shown[i] = '*';
      shown[n] = '\0';
    }
    else
    {
      strncpy(shown, line2, sizeof(shown) - 1);
      shown[sizeof(shown) - 1] = '\0';
    }
  }

  if (showCursor)
  {
    const int inX = x + 12;
    const int inY = y + 40;
    const int inW = boxW - 24;
    const int inH = 20;

    const uint16_t inputOutline = (pet.type == PET_ELDRITCH) ? uiModalOutline(pet.type) : TFT_DARKGREY;
    spr.drawRoundRect(inX, inY, inW, inH, 6, inputOutline);

    spr.setTextFont(2);
    spr.setTextSize(1);
    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    spr.setTextDatum(TL_DATUM);
    spr.drawString(shown, inX + 6, inY + 4);

    int cx = inX + 6 + spr.textWidth(shown);
    spr.fillRect(cx, inY + 4, 2, 12, TFT_WHITE);

    spr.setTextFont(1);
    spr.setTextSize(1);
    spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    spr.setTextDatum(BC_DATUM);
    spr.drawString("ENTER: Next   MENU/ESC: Cancel", screenW / 2, y + boxH - 6);
  }
  else
  {
    spr.setTextFont(2);
    spr.setTextSize(1);
    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    spr.setTextDatum(TC_DATUM);
    spr.drawString(shown, screenW / 2, y + 46);

    spr.setTextFont(1);
    spr.setTextSize(1);
    spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    spr.setTextDatum(BC_DATUM);
    spr.drawString("ENTER: Next   MENU/ESC: Cancel", screenW / 2, y + boxH - 6);
  }

  spr.setTextDatum(TL_DATUM);
}

// ============================================================================
// Level Up Pop Up Window Modal
// ============================================================================
void uiShowLevelUpPopup(uint16_t newLevel)
{
  g_levelUpPopupActive = true;
  g_levelUpPopupLevel = newLevel;

  // ensure a clean redraw
  invalidateBackgroundCache();
  requestUIRedraw();
}

bool uiIsLevelUpPopupActive() { return g_levelUpPopupActive; }

void uiDismissLevelUpPopup()
{
  g_levelUpPopupActive = false;
  invalidateBackgroundCache();
  requestUIRedraw();
}

void uiDrawLevelUpPopup()
{
  if (!g_levelUpPopupActive)
    return;

  const uint16_t outline = uiModalOutline(pet.type);

  // Slim window
  const int boxW = 168;
  const int boxH = 56;
  const int x = (screenW - boxW) / 2;
  const int y = (screenH - boxH) / 2;

  // Draw modal on top of whatever was rendered this frame
  spr.fillRoundRect(x, y, boxW, boxH, 8, TFT_BLACK);
  spr.drawRoundRect(x, y, boxW, boxH, 8, outline);

  // Title
  spr.setTextDatum(TC_DATUM);
  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.drawString("LEVEL UP!", screenW / 2, y + 6);

  // Line
  char line1[32];
  snprintf(line1, sizeof(line1), "Reached Level %u", (unsigned)g_levelUpPopupLevel);

  spr.setTextFont(1);
  spr.setTextSize(1);
  spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  spr.drawString(line1, screenW / 2, y + 28);

  // No help text (intentionally omitted)

  spr.setTextDatum(TL_DATUM);
}

// ============================================================================
// Utility: message overlay
// ============================================================================
static void uiShowToastInternal(const char *msg, uint32_t durationMs)
{
  if (!msg)
    return;

  strncpy(g_toastMsg, msg, sizeof(g_toastMsg) - 1);
  g_toastMsg[sizeof(g_toastMsg) - 1] = '\0';

  g_toastActive = true;
  g_toastUntilMs = durationMs ? (millis() + durationMs) : 0;

  requestUIRedraw();
}

void ui_showMessage(const char *msg) { uiShowToastInternal(msg, 900); }

void ui_showTimedMessage(const char *msg, uint32_t durationMs) { uiShowToastInternal(msg, durationMs); }

void ui_showSuccessMessage(const char *msg) { uiShowToastInternal(msg, 1200); }

static void uiDrawToastOverlay()
{
  if (!g_toastActive)
    return;

  const uint32_t now = millis();
  if (g_toastUntilMs != 0 && (int32_t)(now - g_toastUntilMs) >= 0)
  {
    g_toastActive = false;
    g_toastUntilMs = 0;
    g_toastMsg[0] = '\0';

    // Force one clean repaint so the old toast pixels do not linger.
    requestFullUIRedraw();
    return;
  }

  if (!isScreenOn())
    return;

  const uint16_t modalOutline = uiModalOutline(pet.type);

  const int pad = 10;
  const int boxW = screenW - (pad * 2);
  const int boxH = 42;
  const int x = pad;
  const int y = (screenH - boxH) / 2;

  spr.fillRoundRect(x, y, boxW, boxH, 8, TFT_BLACK);
  spr.drawRoundRect(x, y, boxW, boxH, 8, modalOutline);

  spr.setTextDatum(MC_DATUM);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.setTextFont(2);
  spr.setTextSize(1);

  spr.drawString(g_toastMsg, screenW / 2, y + (boxH / 2));

  spr.setTextDatum(TL_DATUM);

  // Keep rendering while toast is active (prevents it from getting "stuck" behind throttle)
  requestUIRedraw();
}

// ============================================================================
// Power menu (overlay; MUST NOT call drawCurrentScreen)
// ============================================================================
static void drawPowerMenuOverlay()
{
  const uint16_t modalOutline = uiModalOutline(pet.type);
  const uint16_t selFill = uiPillOutline(pet.type);
  const uint16_t selText = TFT_BLACK;

  const int boxW = 200;
  const int boxH = 92;
  const int x = (screenW - boxW) / 2;
  const int y = (screenH - boxH) / 2;

  spr.fillRoundRect(x, y, boxW, boxH, 10, TFT_BLACK);
  spr.drawRoundRect(x, y, boxW, boxH, 10, modalOutline);

  spr.setTextDatum(TC_DATUM);
  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.drawString("POWER MENU", screenW / 2, y + 8);

  const int itemCount = uiPowerMenuCount();

  const int listX = x + 16;
  int yy = y + 26;

  for (int i = 0; i < itemCount; i++)
  {
    const bool sel = (i == g_app.powerMenuIndex);

    if (sel)
    {
      spr.fillRoundRect(listX - 6, yy - 2, boxW - 32, 18, 6, selFill);
      spr.setTextColor(selText, selFill);
    }
    else
    {
      spr.setTextColor(TFT_WHITE, TFT_BLACK);
    }

    spr.setTextDatum(TL_DATUM);
    spr.drawString(uiPowerMenuLabel(i), listX, yy);
    yy += 20;
  }

  spr.setTextDatum(TL_DATUM);
}

void drawPowerMenu() { drawPowerMenuOverlay(); }

// ============================================================================
// New pet flow screens
// ============================================================================
// Read PNG width/height from IHDR (so we can center without guessing)
static bool getPngWH(const char *path, int &outW, int &outH)
{
  outW = 0;
  outH = 0;
  if (!path || !*path)
    return false;
  if (!g_sdReady)
    return false;

  File f = SD.open(path, FILE_READ);
  if (!f)
    return false;

  uint8_t hdr[24];
  int n = f.read(hdr, sizeof(hdr));
  f.close();
  if (n != (int)sizeof(hdr))
    return false;

  const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  for (int i = 0; i < 8; i++)
  {
    if (hdr[i] != sig[i])
      return false;
  }

  if (hdr[12] != 'I' || hdr[13] != 'H' || hdr[14] != 'D' || hdr[15] != 'R')
    return false;

  auto be32 = [&](int off) -> int
  {
    return (int)((uint32_t)hdr[off] << 24 | (uint32_t)hdr[off + 1] << 16 | (uint32_t)hdr[off + 2] << 8 |
                 (uint32_t)hdr[off + 3]);
  };

  outW = be32(16);
  outH = be32(20);
  return (outW > 0 && outH > 0);
}

static void drawCenteredImageSpr(const char *path, int cx, int cy)
{
  if (!path || !*path)
    return;

  int w = 0, h = 0;
  const bool gotWH = getPngWH(path, w, h);

  int x = gotWH ? (cx - (w / 2)) : cx;
  int y = gotWH ? (cy - (h / 2)) : cy;

  bool ok = false;
  if (g_sdReady)
  {
    ok = sprDrawPngFromSD(path, x, y);
  }

  if (!ok)
  {
    const int boxW = gotWH ? w : 140;
    const int boxH = gotWH ? h : 40;
    const int boxX = gotWH ? x : (cx - boxW / 2);
    const int boxY = gotWH ? y : (cy - boxH / 2);

    spr.drawRect(boxX, boxY, boxW, boxH, TFT_DARKGREY);
    spr.setTextDatum(MC_DATUM);
    spr.setTextColor(TFT_DARKGREY, TFT_BLACK);
    spr.setTextFont(1);
    spr.setTextSize(1);
    spr.drawString("IMG FAIL", cx, cy);
  }
}

static void drawCrackedEggBig(int cx, int topY, const char *path)
{
  if (!path || !path[0] || !g_sdReady)
    return;

  int w = 0;
  int h = 0;
  const bool gotWH = getPngWH(path, w, h);

  const int x = gotWH ? (cx - (w / 2)) : cx;
  const int y = topY;

  sprDrawPngFromSD(path, x, y);
}

static void drawCenteredLine(const char *s, int y, int font = 2, int size = 1)
{
  spr.setTextDatum(TC_DATUM);
  spr.setTextFont(font);
  spr.setTextSize(size);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.drawString(s ? s : "", screenW / 2, y);
}

void drawImportPetListScreen(bool redrawBg)
{
  if (!isScreenOn())
    return;

  if (redrawBg)
    spr.fillSprite(TFT_BLACK);

  if (uiImportPetListConfirmDeleteActive())
  {
    const int idx = uiImportPetListConfirmDeleteIndex();
    spr.setTextDatum(TC_DATUM);
    spr.setTextColor(TFT_WHITE);
    spr.drawString("Delete this stored pet?", SCREEN_W / 2, SCREEN_H / 2 - 10, 2);

    spr.setTextColor(idx == 0 ? TFT_YELLOW : TFT_WHITE);
    spr.drawString("YES", SCREEN_W / 2 - 30, SCREEN_H / 2 + 10, 2);

    spr.setTextColor(idx == 1 ? TFT_YELLOW : TFT_WHITE);
    spr.drawString("NO", SCREEN_W / 2 + 30, SCREEN_H / 2 + 10, 2);
    return;
  }

  if (uiImportPetListActionMenuActive())
  {
    const int idx = uiImportPetListActionIndex();
    const char *items[3] = {"Retrieve", "Delete", "Cancel"};

    spr.setTextDatum(TC_DATUM);
    spr.setTextColor(TFT_WHITE);
    spr.drawString("Stored Pet Options", SCREEN_W / 2, 24, 2);

    for (int i = 0; i < 3; ++i)
    {
      spr.setTextColor(i == idx ? TFT_YELLOW : TFT_WHITE);
      spr.drawString(items[i], SCREEN_W / 2, 52 + (i * 18), 2);
    }
    return;
  }

  const int rowH = 18;
  const int startY = 20;

  const int count = uiImportPetListCount();
  const int visibleCount = uiImportPetListVisibleCount();
  const int selectedIdx = uiImportPetListSelected();
  const int windowStart = uiImportPetListWindowStart();

  if (count <= 0)
  {
    spr.setTextDatum(TC_DATUM);
    spr.setTextColor(TFT_WHITE);
    spr.drawString("No stored pets found", SCREEN_W / 2, SCREEN_H / 2, 2);
    return;
  }

  for (int i = 0; i < visibleCount; ++i)
  {
    const int y = startY + (i * rowH);
    const bool selected = ((windowStart + i) == selectedIdx);
    const PetExportEntry &e = uiImportPetListGetVisible(i);

    // Detect if this entry is the currently active pet
    bool isCurrent = false;
    if (pet.getName()[0] && strcmp(e.name, pet.getName()) == 0)
    {
      isCurrent = true;
    }

    spr.setTextDatum(TL_DATUM);

    // Priority:
    // 1. Selected = yellow
    // 2. Current pet = green
    // 3. Default = white
    uint16_t nameColor = TFT_WHITE;
    if (selected)
      nameColor = TFT_YELLOW;
    else if (isCurrent)
      nameColor = TFT_GREEN;

    char typePretty[16];
    snprintf(typePretty, sizeof(typePretty), "%s", e.petType);
    typePretty[0] = (char)toupper((unsigned char)typePretty[0]);
    for (int j = 1; typePretty[j]; ++j)
      typePretty[j] = (char)tolower((unsigned char)typePretty[j]);

    char nameWithSep[48];
    snprintf(nameWithSep, sizeof(nameWithSep), "%s - ", e.name);

    spr.setTextColor(nameColor);
    int nameWidth = spr.drawString(nameWithSep, 6, y, 2);

    char meta[48];
    time_t t = (time_t)e.createdAtEpoch;
    struct tm tmBuf{};
    localtime_r(&t, &tmBuf);
    snprintf(meta, sizeof(meta), "%s  %02d/%02d %02d:%02d", typePretty, tmBuf.tm_mon + 1, tmBuf.tm_mday, tmBuf.tm_hour,
             tmBuf.tm_min);

    uint16_t metaColor = TFT_LIGHTGREY;
    if (selected)
      metaColor = TFT_YELLOW;
    else if (isCurrent)
      metaColor = TFT_GREEN;

    spr.setTextColor(metaColor);
    spr.drawString(meta, 6 + nameWidth, y, 2);
  }
}

static void drawBackupPetListScreen(bool redrawBg)
{
  if (!isScreenOn())
    return;

  if (redrawBg)
    spr.fillSprite(TFT_BLACK);

  if (uiBackupPetListConfirmRestoreActive())
  {
    const int idx = uiBackupPetListConfirmRestoreIndex();

    const int selectedIdx = uiBackupPetListSelected();
    const int windowStart = uiBackupPetListWindowStart();
    const int visibleIdx = selectedIdx - windowStart;

    char titleBuf[64];
    titleBuf[0] = '\0';

    if (visibleIdx >= 0 && visibleIdx < uiBackupPetListVisibleCount())
    {
      const PetExportEntry &e = uiBackupPetListGetVisible(visibleIdx);

      time_t t = (time_t)e.createdAtEpoch;
      struct tm tmBuf{};
      localtime_r(&t, &tmBuf);

      static const char *kMonths[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

      const char *mon = "???";
      if (tmBuf.tm_mon >= 0 && tmBuf.tm_mon < 12)
        mon = kMonths[tmBuf.tm_mon];

      snprintf(titleBuf, sizeof(titleBuf), "Restore %s (%s %d, %02d:%02d)?", e.name[0] ? e.name : "backup", mon,
               tmBuf.tm_mday, tmBuf.tm_hour, tmBuf.tm_min);
    }
    else
    {
      snprintf(titleBuf, sizeof(titleBuf), "Restore backup?");
    }

    spr.setTextDatum(TC_DATUM);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(titleBuf, SCREEN_W / 2, SCREEN_H / 2 - 30, 2);
    spr.drawString("Store Current Pet First?", SCREEN_W / 2, SCREEN_H / 2 - 12, 2);

    spr.setTextColor(idx == 0 ? TFT_YELLOW : TFT_WHITE);
    spr.drawString("YES", SCREEN_W / 2 - 40, SCREEN_H / 2 + 12, 2);

    spr.setTextColor(idx == 1 ? TFT_YELLOW : TFT_WHITE);
    spr.drawString("CANCEL", SCREEN_W / 2 + 40, SCREEN_H / 2 + 12, 2);
    return;
  }

  if (uiBackupPetListActionMenuActive())
  {
    const int idx = uiBackupPetListActionIndex();
    const char *items[3] = {"Restore", "Delete Backup", "Cancel"};

    spr.setTextDatum(TC_DATUM);
    spr.setTextColor(TFT_WHITE);
    spr.drawString("Backup Options", SCREEN_W / 2, 24, 2);

    for (int i = 0; i < 3; ++i)
    {
      spr.setTextColor(i == idx ? TFT_YELLOW : TFT_WHITE);
      spr.drawString(items[i], SCREEN_W / 2, 52 + (i * 18), 2);
    }
    return;
  }

  const int count = uiBackupPetListCount();
  if (count <= 0)
  {
    spr.setTextDatum(TC_DATUM);
    spr.setTextColor(TFT_DARKGREY);
    spr.drawString("No backups found", SCREEN_W / 2, SCREEN_H / 2, 2);
    return;
  }

  const int rowH = 18;
  const int startY = 20;
  const int visibleCount = uiBackupPetListVisibleCount();

  for (int i = 0; i < visibleCount; ++i)
  {
    const int y = startY + (i * rowH);
    const bool selected = ((uiBackupPetListWindowStart() + i) == uiBackupPetListSelected());
    const PetExportEntry &e = uiBackupPetListGetVisible(i);

    spr.setTextDatum(TL_DATUM);
    spr.setTextColor(selected ? TFT_YELLOW : TFT_WHITE);
    int nameWidth = spr.drawString(e.name, 6, y, 2);

    char meta[48];
    time_t t = (time_t)e.createdAtEpoch;
    struct tm tmBuf{};
    localtime_r(&t, &tmBuf);
    snprintf(meta, sizeof(meta), "%s  %02d/%02d %02d:%02d", e.petType, tmBuf.tm_mon + 1, tmBuf.tm_mday, tmBuf.tm_hour,
             tmBuf.tm_min);

    spr.setTextColor(selected ? TFT_YELLOW : TFT_LIGHTGREY);
    spr.drawString(meta, 6 + nameWidth + 6, y + 5, 1);
  }
}

void drawTitleMenuScreen(bool redrawBg)
{
  if (!isScreenOn())
    return;

  if (redrawBg)
  {
    bool ok = false;
    if (g_sdReady)
      ok = sprDrawJpgFromSD(PATH_BG_SPLASH, 0, 0);

    if (!ok)
      spr.fillSprite(TFT_BLACK);
  }

  const bool hasSave = uiTitleMenuHasSave();
  const bool hasImport = uiTitleMenuHasImport();
  const char *petName = (hasSave && pet.getName()[0]) ? pet.getName() : "";

  char row0Buf[80];
  if (hasSave)
  {
    const char *typePretty = "Devil";
    switch (pet.type)
    {
    case PET_ELDRITCH:
      typePretty = "Eldritch";
      break;
    case PET_DEVIL:
    default:
      typePretty = "Devil";
      break;
    }

    snprintf(row0Buf, sizeof(row0Buf), "%s - lvl %u %s", petName, (unsigned)pet.level, typePretty);
  }
  else
  {
    snprintf(row0Buf, sizeof(row0Buf), "New Pet");
  }

  const char *storageLabel = hasImport ? "Pet Storage" : "Pet Storage Empty";
  const char *labels[3] = {row0Buf, storageLabel, "Settings"};
  const bool enabled[3] = {true, true, true};

  // Menu panel
  // Menu panel
  const int rowH = 18;
  const int itemCount = 3;
  const int menuTopY = (SCREEN_H / 2) + 12;
  const int menuPadX = 12;
  const int menuPadY = 8;
  const int menuBoxY = menuTopY - menuPadY;
  const int menuBoxH = (itemCount * rowH) + (menuPadY * 2);

  const int panelX = menuPadX;
  const int panelY = menuBoxY;
  const int panelW = SCREEN_W - (menuPadX * 2);
  const int panelH = menuBoxH;

  // Checkerboard dither overlay behind menu text
  for (int yy = panelY; yy < panelY + panelH; ++yy)
  {
    const int xStart = panelX + ((yy & 1) ? 1 : 0);
    for (int xx = xStart; xx < panelX + panelW; xx += 2)
    {
      spr.drawPixel(xx, yy, TFT_BLACK);
    }
  }

  for (int i = 0; i < itemCount; ++i)
  {
    const int rowY = menuTopY + (i * rowH);
    const bool selected = (i == g_titleMenuIndex);

    uint16_t fg = TFT_WHITE;
    if (!enabled[i])
      fg = TFT_DARKGREY;
    else if (selected)
      fg = TFT_YELLOW;

    // Measure this item's text width using the same font the title menu helper uses
    spr.setTextFont(2);
    spr.setTextSize(1);
    const int textW = spr.textWidth(labels[i]);

    // Draw selection arrows sized to the actual item width
    if (selected)
    {
      const int arrowGap = 6;
      const int leftArrowX = (SCREEN_W / 2) - (textW / 2) - arrowGap;
      const int rightArrowX = (SCREEN_W / 2) + (textW / 2) + arrowGap;

      drawTitleMenuText(spr, "<", leftArrowX, rowY, 2, TFT_YELLOW, textdatum_t::top_right);
      drawTitleMenuText(spr, ">", rightArrowX, rowY, 2, TFT_YELLOW, textdatum_t::top_left);
    }

    drawTitleMenuText(spr, labels[i], SCREEN_W / 2, rowY, 2, fg, textdatum_t::top_center);
  }

  // Status block
  const char *assetVer = assetOtaInstalledVersion();
  const AssetOtaChannel ch = (AssetOtaChannel)assetOtaGetConfig().channel;

  char assetBuf[32];
  char buildBuf[32];

  snprintf(assetBuf, sizeof(assetBuf), "%s", (assetVer && assetVer[0]) ? assetVer : "none");
  snprintf(buildBuf, sizeof(buildBuf), "%s %s", (ch == AssetOtaChannel::DEV) ? "DEV" : "PUB", RH_VERSION_STRING);

  // Build version: top-left
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(TFT_BLACK, TFT_TRANSPARENT);
  spr.drawString(buildBuf, 5, 3, 1);
  spr.setTextColor(TFT_LIGHTGREY, TFT_TRANSPARENT);
  spr.drawString(buildBuf, 4, 2, 1);

  // Asset version: top-right
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(TFT_BLACK, TFT_TRANSPARENT);
  spr.drawString(assetBuf, SCREEN_W - 3, 3, 1);
  spr.setTextColor(TFT_LIGHTGREY, TFT_TRANSPARENT);
  spr.drawString(assetBuf, SCREEN_W - 4, 2, 1);
}

void drawChoosePetScreen(bool redrawBg)
{
  if (!isScreenOn())
    return;
  if (redrawBg)
    spr.fillSprite(TFT_BLACK);

  drawCenteredLine("Choose Your Egg", 18, 2, 1);

  const int eggW = 64;
  const int eggH = 64;
  const int eggX = (SCREEN_W - eggW) / 2;
  const int eggY = 38;

  const char *eggPath = nullptr;
  switch (pet.type)
  {
  case PET_DEVIL:
    eggPath = DEV_EGG_PNG;
    break;
  case PET_ELDRITCH:
    eggPath = ELD_EGG_PNG;
    break;
  case PET_KAIJU:
    eggPath = KAI_EGG_PNG;
    break;
  case PET_ANUBIS:
    eggPath = ANU_EGG_PNG;
    break;
  case PET_AXOLOTL:
    eggPath = AXO_EGG_PNG;
    break;
  case PET_ALIEN:
    eggPath = AL_EGG_PNG;
    break;
  default:
    break;
  }

  bool ok = false;
  if (g_sdReady && eggPath)
  {
    ok = sprDrawPngFromSD(eggPath, eggX, eggY);
  }

  if (!ok)
  {
    spr.fillEllipse(eggX + eggW / 2, eggY + eggH / 2, eggW / 2, eggH / 2, TFT_WHITE);
    spr.drawEllipse(eggX + eggW / 2, eggY + eggH / 2, eggW / 2, eggH / 2, TFT_RED);
  }

  const int arrowOffsetX = 14;
  const int arrowY = eggY + (eggH / 2) - 4;

  spr.setTextDatum(TL_DATUM);
  spr.drawString("<", eggX - arrowOffsetX, arrowY);
  spr.drawString(">", eggX + eggW + arrowOffsetX - 6, arrowY);

  const char *label = "Unknown Egg";
  switch (pet.type)
  {
  case PET_DEVIL:
    label = "Devil Egg";
    break;
  case PET_KAIJU:
    label = "Kaiju Egg";
    break;
  case PET_ELDRITCH:
    label = "Eldritch Egg";
    break;
  case PET_ALIEN:
    label = "Alien Egg";
    break;
  case PET_ANUBIS:
    label = "Anubis Egg";
    break;
  case PET_AXOLOTL:
    label = "Axolotl Egg";
    break;
  default:
    break;
  }

  const int eggBottomY = eggY + eggH;
  const int EGG_LABEL_Y = eggBottomY + 2;
  int EGG_PROMPT_Y = screenH - 10; // push down

  // Bigger, more prominent label
  drawCenteredLine(label, EGG_LABEL_Y, 2, 1);

  // Keep prompt smaller
  drawCenteredLine("Press ENTER to hatch", EGG_PROMPT_Y, 1, 1);

#if SAVE_DIAG_ENABLED
  {
    const uint8_t e = saveManagerLastLoadErr();
    const uint32_t sz = saveManagerLastLoadSize();

    char buf[96];
    snprintf(buf, sizeof(buf), "ERR=%u FS=%lu SP=%u SV2=%u", (unsigned)e, (unsigned long)sz,
             (unsigned)sizeof(SavePayload), (unsigned)sizeof(SavePayloadV2));

    spr.setTextSize(1);
    spr.setTextColor(TFT_YELLOW, TFT_BLACK);
    spr.drawString(buf, 2, SCREEN_H - 10);
  }
#endif
}

static void drawNamePetScreen(bool redrawBg)
{
  if (!isScreenOn())
    return;
  if (redrawBg)
    spr.fillSprite(TFT_BLACK);

  drawCenteredLine(g_namePetRenameMode ? "Rename Pet" : "Name Your Pet", 18, 2, 1);

  const char *name = (g_pendingPetName[0] != '\0') ? g_pendingPetName : "_";

  const int boxW = 200;
  const int boxH = 26;
  const int boxX = (screenW - boxW) / 2;
  const int boxY = 54;

  spr.fillRoundRect(boxX, boxY, boxW, boxH, 6, TFT_BLACK);
  spr.drawRoundRect(boxX, boxY, boxW, boxH, 6, uiModalOutline(pet.type));

  spr.setTextDatum(TL_DATUM);
  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.drawString(name, boxX + 8, boxY + 6);

  drawCenteredLine(g_namePetRenameMode ? "Edit name, press ENTER" : "Type name, press ENTER", screenH - 22, 1, 1);
}

static void drawPetPerfHud()
{
  if (!g_petPerfHudEnabled)
    return;

  if (g_app.currentTab != Tab::TAB_PET)
    return;

  if (g_app.uiState != UIState::HOME && g_app.uiState != UIState::PET_SCREEN)
    return;

  const PetPerfStats &ps = petPerfStats();

  const int boxW = 86;
  const int boxH = 34;
  const int x = SCREEN_W - boxW - 4;
  const int y = TOP_BAR_H + 4;

  spr.fillRoundRect(x, y, boxW, boxH, 4, TFT_BLACK);
  spr.drawRoundRect(x, y, boxW, boxH, 4, TFT_DARKGREY);

  spr.setTextDatum(TL_DATUM);
  spr.setTextFont(1);
  spr.setTextSize(1);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);

  char line[32];

  snprintf(line, sizeof(line), "Pet:%ums", (unsigned)ps.petFrameMs);
  spr.drawString(line, x + 4, y + 4);

  snprintf(line, sizeof(line), "SD :%ums", (unsigned)ps.petSpriteDrawMs);
  spr.drawString(line, x + 4, y + 14);

  snprintf(line, sizeof(line), "Ani:%ums", (unsigned)ps.animStepMs);
  spr.drawString(line, x + 4, y + 24);
}

static void drawEvolutionScreen()
{
  // Always redraw cleanly
  spr.fillSprite(TFT_BLACK);

  if (g_app.flow.evo.flashWhite)
  {
    spr.fillSprite(TFT_WHITE);
    return;
  }

  // Decide which stage we’re showing right now
  const uint8_t stageShown = (g_app.flow.evo.phase >= 2) ? g_app.flow.evo.toStage : g_app.flow.evo.fromStage;

  const AnimId id = evoHappyClipFor(pet.type, stageShown);
  const AnimClip *clip = animGetClip(id);
  if (!clip || !clip->frames || clip->frameCount == 0)
  {
    return;
  }

  // Frame select
  const uint32_t now = millis();
  const uint32_t t = (g_app.flow.evo.phaseStartMs == 0) ? 0 : (now - g_app.flow.evo.phaseStartMs);
  uint32_t idx = 0;

  if (clip->frameMs > 0)
    idx = t / clip->frameMs;
  if (clip->loop && clip->frameCount > 0)
    idx %= clip->frameCount;
  if (!clip->loop && idx >= clip->frameCount)
    idx = clip->frameCount - 1;

  const char *path = clip->frames[idx];
  if (!path || !*path || !g_sdReady)
    return;

  // Center draw
  int w = 0, h = 0;
  const bool gotWH = getPngWH(path, w, h);

  const int cx = screenW / 2;
  const int cy = screenH / 2 + 10; // small down bias like your egg positioning

  const int x = gotWH ? (cx - (w / 2)) : cx;
  const int y = gotWH ? (cy - (h / 2)) : cy;

  sprDrawPngFromSD(path, x, y);
}

void drawHatchingScreen(bool redrawBg)
{
  (void)redrawBg;

  spr.fillSprite(TFT_BLACK);

  if (g_app.flow.hatch.flashWhite && !g_app.flow.hatch.showingMsg)
  {
    spr.fillSprite(TFT_WHITE);
    return;
  }

  const int centerX = screenW / 2;
  const int animEggY = screenH / 2;
  const int crackedEggTopY = 4;

  const char *const *crackFrames = pendingEggCrackFrames();

  if (!g_app.flow.hatch.showingMsg)
  {
    if (g_app.flow.hatch.frame < 4)
      drawCenteredImageSpr(crackFrames[g_app.flow.hatch.frame], centerX, animEggY);
    else
      drawCrackedEggBig(centerX, crackedEggTopY, pendingEggCrackedPng());
    return;
  }

  drawCrackedEggBig(centerX, crackedEggTopY, pendingEggCrackedPng());

  spr.setTextDatum(MC_DATUM);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.drawString(pendingHatchMessage(), centerX, 122);
}

// ============================================================================
// Death screen
// ============================================================================
static void drawDeathScreen(bool redrawBg)
{
  static bool s_deathScreenFadeInActive = false;
  static uint32_t s_deathScreenFadeInStartMs = 0;
  static constexpr uint32_t kDeathScreenFadeInMs = 900;

  if (consumeDeathScreenFadeInStart())
  {
    s_deathScreenFadeInActive = true;
    s_deathScreenFadeInStartMs = millis();
    forceBacklightDuringFade(0);
  }

  if (s_deathScreenFadeInActive)
  {
    const uint32_t now = millis();
    const uint32_t elapsed = now - s_deathScreenFadeInStartMs;
    const uint8_t targetBrightness = (uint8_t)brightnessValues[brightnessLevel];

    if (elapsed >= kDeathScreenFadeInMs)
    {
      s_deathScreenFadeInActive = false;

      applyBrightnessLevel(brightnessLevel);

      const uint8_t targetBrightness = (uint8_t)brightnessValues[brightnessLevel];
      forceBacklightDuringFade(targetBrightness);
    }
    else
    {
      const uint8_t fadeBrightness = (uint8_t)(((uint32_t)targetBrightness * elapsed) / kDeathScreenFadeInMs);

      Serial.printf("[DEATHX] death fade-in done targetBrightness=%u level=%d\n",
                    (unsigned)brightnessValues[brightnessLevel], (int)brightnessLevel);

      forceBacklightDuringFade(fadeBrightness);
      requestUIRedraw();
    }
  }

  spr.fillSprite(TFT_BLACK);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);

  drawCenteredLine("YOUR PET", 26, 2, 1);
  drawCenteredLine("HAS DIED", 46, 2, 1);

  const int y0 = 78;
  const int gap = 18;

  spr.setTextDatum(TC_DATUM);
  spr.setTextFont(2);
  spr.setTextSize(1);

  spr.setTextColor(TFT_WHITE, TFT_BLACK);

  const int itemCount = uiDeathMenuCount();
  for (int i = 0; i < itemCount; ++i)
  {
    String line = (deathMenuIndex == i) ? "> " : "  ";
    line += uiDeathMenuLabel(i);
    spr.drawString(line.c_str(), screenW / 2, y0 + gap * i);
  }

  spr.setTextFont(1);
  spr.setTextDatum(TC_DATUM);
  spr.drawString("UP/DOWN + ENTER", screenW / 2, screenH - 16);
}

// ============================================================================
// Set Time Screen (patched: removed CONTENT_* dependency)
// ============================================================================
void drawSetTimeScreen()
{
  if (!isScreenOn())
    return;

  drawTopBar();

  const int cx = 0;
  const int cw = screenW;
  const int contentY = TOP_BAR_H;
  const int ch = screenH - TOP_BAR_H;

  spr.fillRect(0, contentY, cw, ch, TFT_BLACK);

  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.drawString("Set Date & Time", cx + 8, contentY + 6);

  const int panelX = cx + 10;
  const int panelY = contentY + 28;
  const int panelW = cw - 20;
  const int panelH = 42;

  drawSetDateTimePanel(panelX, panelY, panelW, panelH, g_setTimeField);

  const int okW = 84;
  const int okH = 22;
  const int okX = cx + (cw - okW) / 2;

  // Put OK under the combined panel, not down in the footer area
  const int okY = panelY + panelH + 12;

  const bool okSel = (g_setTimeField == 5);
  drawButton(okX, okY, okW, okH, "OK", okSel);

  spr.setTextDatum(BC_DATUM);
  spr.setTextFont(1);
  spr.setTextSize(1);
  spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  spr.drawString("Enter: next | Arrows: +/-", cx + cw / 2, contentY + ch - 2);
  spr.setTextDatum(TL_DATUM);
}

static AnimId deathTransitionStaticClipForPet()
{
  switch (pet.type)
  {
  case PET_DEVIL:
    switch (pet.evoStage)
    {
    case 0:
      return ANIM_DEV_BABY_SICK_CRAWL;
    case 1:
      return ANIM_DEV_TEEN_SICK_BOB;
    case 2:
      return ANIM_DEV_ADULT_SICK_LAY;
    default:
      return ANIM_DEV_ELDER_SICK_COUGH;
    }

  case PET_ELDRITCH:
    switch (pet.evoStage)
    {
    case 0:
      return ANIM_ELD_BABY_SICK_BOB;
    case 1:
      return ANIM_ELD_TEEN_SICK_SNEEZE;
    case 2:
      return ANIM_ELD_ADULT_SICK_HUNCH;
    default:
      return ANIM_ELD_ELDER_SICK_SNEEZE;
    }

  case PET_KAIJU:
    return ANIM_KAI_IDLE_1F;
  case PET_ALIEN:
    return ANIM_AL_IDLE_1F;
  case PET_ANUBIS:
    return ANIM_ANU_IDLE_1F;
  case PET_AXOLOTL:
    return ANIM_AXO_IDLE_1F;

  default:
    return ANIM_NONE;
  }
}

static uint8_t deathTransitionStaticFrameIndex(const AnimClip *clip)
{
  if (!clip || clip->frameCount == 0)
    return 0;

  // Cheap, deterministic default: use the last frame of the sick clip.
  // If any pet looks weird, we can special-case per clip later.
  return (uint8_t)(clip->frameCount - 1);
}

static void drawDeathTransitionStaticPet()
{
  if (!g_sdReady)
    return;

  const AnimId id = deathTransitionStaticClipForPet();
  const AnimClip *clip = animGetClip(id);
  if (!clip || !clip->frames || clip->frameCount == 0)
    return;

  const uint8_t idx = deathTransitionStaticFrameIndex(clip);
  const char *path = clip->frames[idx];
  if (!path || !*path)
    return;

  const int petAreaW = SCREEN_W - MINI_STAT_W - MINI_STAT_PAD;
  const int petAreaX = 0;

  const PetRenderProfile &prof = getPetProfile(pet.type);

  const int centerX = petAreaX + (petAreaW / 2) + prof.xOff;
  const int bottomY = (PET_AREA_Y + PET_AREA_H) + prof.yOff;

  int w = 0;
  int h = 0;

  if (getPngWH(path, w, h) && w > 0 && h > 0)
  {
    const int drawX = centerX - (w / 2);
    const int drawY = bottomY - h + deathTransitionYNudgeForPet();
    sprDrawPngFromSD(path, drawX, drawY);
  }
  else
  {
    // Fallback if WH lookup fails.
    sprDrawPngFromSD(path, centerX, bottomY);
  }
}

static void drawDeathTransitionScreen(bool redrawBg)
{
  if (!isScreenOn())
    return;

  static PetType s_lastBgPetType = (PetType)255;
  static uint8_t s_lastBgEvoStage = 255;

  const bool petChanged = (s_lastBgPetType != pet.type) || (s_lastBgEvoStage != pet.evoStage);

  const bool cacheMissing = (g_petBgCachedPath == nullptr);

  // redrawBg should restore from cache, not force a fresh SD/JPEG rebuild.
  const bool needPetBg = petChanged || cacheMissing || g_forcePetBgCache;

  s_lastBgPetType = pet.type;
  s_lastBgEvoStage = pet.evoStage;

  cachePetAreaBackgroundIfNeeded(needPetBg);
  g_forcePetBgCache = false;

  if (needPetBg)
  {
    restorePetAreaFromCache();
  }

  drawTopBar();
  drawDeathTransitionStaticPet();
  drawMiniStatPreview();
  drawTabBar();
  drawPetPerfHud();
}

// ============================================================================
// BURIAL SCREEN
//  - patched: removed pet.birth_epoch direct field access (compile-safe)
// ============================================================================
static void drawBurialScreen()
{
  static const char *kBurialBg = "/raising_hell/graphics/background/flow/grave.jpg";

  spr.fillSprite(TFT_BLACK);

  // Use wrapper-based draw to avoid drawJpgFile(SD, ...) template instantiation.
  sprDrawJpgFromSD(kBurialBg, 0, 0);

  const int cx = 120;
  int y = 44;
  const int lineH = 14;

  spr.setTextColor(TFT_WHITE);
  spr.setFont(nullptr);
  spr.setTextDatum(MC_DATUM);

  spr.drawString(pet.name, cx, y);
  y += lineH + 6;

  char birthBuf[24] = {0};
  char deathBuf[24] = {0};

  uint32_t be = saveManagerGetBirthEpoch();
  if (be == 0)
    be = (uint32_t)getPetBirthEpoch();

  if (be > 100000)
  {
    time_t bt = (time_t)be;
    tm tmb;
    localtime_r(&bt, &tmb);
    snprintf(birthBuf, sizeof(birthBuf), "%04d-%02d-%02d", tmb.tm_year + 1900, tmb.tm_mon + 1, tmb.tm_mday);
  }
  else
  {
    strncpy(birthBuf,
            "????"
            "-"
            "??"
            "-"
            "??",
            sizeof(birthBuf) - 1);
  }

  time_t now = time(nullptr);
  if (now > 100000)
  {
    tm tmd;
    localtime_r(&now, &tmd);
    snprintf(deathBuf, sizeof(deathBuf), "%04d-%02d-%02d", tmd.tm_year + 1900, tmd.tm_mon + 1, tmd.tm_mday);
  }
  else
  {
    strncpy(deathBuf,
            "????"
            "-"
            "??"
            "-"
            "??",
            sizeof(deathBuf) - 1);
  }

  spr.drawString(String("Born: ") + birthBuf, cx, y);
  y += lineH;

  spr.drawString(String("Died: ") + deathBuf, cx, y);
  y += lineH;

  char ageBuf[32] = {0};
  getPetAgeString(ageBuf, sizeof(ageBuf), be);
  spr.drawString(String("Age: ") + ageBuf, cx, y);

  spr.pushSprite(0, 0);
}