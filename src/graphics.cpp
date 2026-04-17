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
#include "flow_boot_wifi.h"

#include "graphics_boot_screens.h"
#include "graphics_chrome.h"
#include "graphics_clock_mode_screens.h"
#include "graphics_death_screens.h"
#include "graphics_egg_select_screens.h"
#include "graphics_evolution_screens.h"
#include "graphics_hatching_screens.h"
#include "graphics_menu_screens.h"
#include "graphics_name_pet_screens.h"
#include "graphics_overlays.h"
#include "graphics_pet_presentation.h"
#include "graphics_render_utils.h"
#include "graphics_set_time_screens.h"
#include "graphics_settings_screens.h"
#include "graphics_shared_utils.h"
#include "graphics_ui_common.h"
#include "graphics_tab_menus.h"
#include "graphics_pet_presentation.h"
#include "graphics_mini_stats.h"
#include "graphics_sd_draw.h"

#include "feed_menu_state.h"
#include "inventory_state.h"
#include "shop_screen_state.h"

#include "mg_pause_menu.h"
#include "mini_game_pause_menu.h"
#include "mini_games.h"
#include "name_entry_state.h"
#include "new_pet_flow_state.h"
#include "settings_nav_state.h"
#include "ui_death_menu.h"
#include "ui_feed_menu.h"
#include "ui_menu_state.h"
#include "ui_play_menu.h"
#include "ui_power_menu.h"
#include "ui_sleep_menu.h"
#include "ui_state_backup_pet_list.h"
#include "ui_state_import_pet_list.h"
#include "ui_state_title_menu.h"
#include "user_toggles_state.h"
#include "wifi_setup_state.h"

// -----------------------------------------------------------------------------
// OTA / Build / Config
// -----------------------------------------------------------------------------
#include "build_flags.h"

// -----------------------------------------------------------------------------
// Misc / Tools
// -----------------------------------------------------------------------------
#include "console.h"

// END of includes

// --- Cache/Draw Helpers
bool g_forcePetBgCache = false;
void drawPetPerfHud();

// --- Graphics and Runtime
void resetClockModePetPresentation();

// --- Compatibility wrappers / missing helpers (compile fix) ---
static void drawMiniGameScreen();
static void drawDeathScreen(bool redrawBg);
static void drawBurialScreen();

// Sleep Graphics Kick
volatile bool g_sleepBgKick = false;

// Misc Helpers
void drawNonPetTabBackground();

void sleepBgKickNow()
{
  g_sleepBgKick = true;
  requestUIRedraw();
}

// -----------------------------------------------------------------------------
// Paths (SD)
// -----------------------------------------------------------------------------
static const char *PATH_BG_PET = "/raising_hell/graphics/bg/hell_bg.jpg";
const char *PATH_BG_SLEEP = "/raising_hell/graphics/background/sleep_bg.jpg";
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

int deathTransitionYNudgeForPet()
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

#define DEV_EGG_PNG "/raising_hell/graphics/pet/egg/dev_egg.png"
#define ELD_EGG_PNG "/raising_hell/graphics/pet/egg/eld_egg.png"

// Pet-area background (drawn at PET_AREA_Y)
static const char *PATH_BG_DEVIL_BABY = "/raising_hell/graphics/background/dev/hell_bg.jpg";
static const char *PATH_BG_DEVIL_TEEN = "/raising_hell/graphics/background/dev/dev_teen_bg.jpg";
static const char *PATH_BG_DEVIL_ADULT = "/raising_hell/graphics/background/dev/dev_ad_bg.jpg";
static const char *PATH_BG_DEVIL_ELDER = "/raising_hell/graphics/background/dev/dev_el_bg.jpg";

static const char *PATH_BG_ELDRITCH = "/raising_hell/graphics/background/eld/eld_bg.jpg";
static const char *PATH_BG_ELDRITCH_TEEN = "/raising_hell/graphics/background/eld/eld_teen_bg.jpg";
static const char *PATH_BG_ELDRITCH_ADULT = "/raising_hell/graphics/background/eld/eld_ad_bg.jpg";
static const char *PATH_BG_ELDRITCH_ELDER = "/raising_hell/graphics/background/eld/eld_el_bg.jpg";

static inline const char *bgPathForPet(PetType t)
{
  switch (t)
  {
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

// // -----------------------------------------------------------------------------
// EGG Cracker - Cracks your eggs
// -----------------------------------------------------------------------------

static const char *pendingEggClosedPng()
{
  if (g_pendingPetType == PET_ELDRITCH)
    return "/raising_hell/graphics/pet/egg/eld_egg.png";

  return "/raising_hell/graphics/pet/egg/dev_egg.png";
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
void drawMiniStatPreview();
static void drawCurrentScreen(bool redrawBg);
static void drawWifiSetupScreen();

void drawBootLowBatteryChargingScreen(int mv, int pct, bool usb, bool readyToBoot);

static bool ensurePetLayer();
void cachePetAreaBackgroundIfNeeded(bool needPetBg);
void restorePetAreaFromCache();

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

  return s_nonPetTileReady;
}

void drawNonPetTabBackground()
{
  spr.fillScreen(TFT_BLACK);

  if (!ensureNonPetTileReady())
  {
    spr.fillScreen(TFT_BLACK);
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
// Pet Type Render Profiles (static sprites)
// ============================================================================
static int getClockModeBaselineDeltaForPet()
{
  switch (pet.type)
  {
  case PET_DEVIL:
    switch (pet.evoStage)
    {
    case 0:
      return TAB_BAR_H; // baby
    case 1:
      return TAB_BAR_H; // teen
    case 2:
      return TAB_BAR_H; // adult
    case 3:
      return TAB_BAR_H; // elder
    default:
      return TAB_BAR_H;
    }

  case PET_ELDRITCH:
    switch (pet.evoStage)
    {
    case 0:
      return TAB_BAR_H; // baby
    case 1:
      return TAB_BAR_H; // teen
    case 2:
      return TAB_BAR_H + 1; // adult tentacles hang a hair lower
    case 3:
      return TAB_BAR_H + 1; // elder too
    default:
      return TAB_BAR_H;
    }

  default:
    return TAB_BAR_H;
  }
}

// Death screen
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
// BURIAL SCREEN
//  - patched: removed pet.birth_epoch direct field access (compile-safe)
// ============================================================================
void drawBurialScreen()
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

static void drawMiniGameScreen()
{
  if (currentMiniGame == MiniGame::NONE)
    return;

  drawMiniGame();
}

void drawDeathScreen() { drawDeathScreen(true); }

// STATS TAB
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
void drawSleepMeterBar()
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
// Sleep animation frame cache (RGB565 full-screen sprite buffer snapshots)
// (Renamed to avoid colliding with existing ensureSleepFrameCache in this file.)
// -----------------------------------------------------------------------------
uint16_t **s_sleepAnimFrameCache = nullptr;
static uint8_t s_sleepAnimFrameCacheCnt = 0;
static uint8_t s_sleepAnimFrameCacheMode = 0; // 1=baby,2=teen,3=adult,4=elder
static bool s_sleepAnimFrameCacheReady = false;

void freeSleepAnimFrameCache()
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
  // Release pet presentation caches through the owning module.
  graphicsReleasePetLayerForOta();

  // Release mini stat icon caches through the owning module.
  graphicsReleaseMiniStatCaches();

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

bool ensureSleepAnimFrameCache(uint8_t mode, const char *const *frames, uint8_t frameCount, int drawX, int drawY)
{
  if (mode == 0 || !frames || frameCount == 0)
    return false;

  // No PSRAM on this hardware. Full-screen cached sleep frames are too large
  // and can starve later graphics/WiFi allocations.
  // Fall back to drawing sleep frames live instead of caching snapshots.
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
  case UIState::CLOCK_MODE:
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
  const bool petFreeRoamScreen = ((g_app.uiState == UIState::PET_SCREEN && g_app.currentTab == Tab::TAB_PET) ||
                                  (g_app.uiState == UIState::CLOCK_MODE));

  const bool petAnimating = petFreeRoamScreen && (g_app.petScreenIntroFadePending || isPetScreenIntroFadeActive() ||
                                                  petPresentationAnimating());

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

  const bool petScreenNow = ((g_app.uiState == UIState::PET_SCREEN && g_app.currentTab == Tab::TAB_PET) ||
                             (g_app.uiState == UIState::CLOCK_MODE));

  const bool petScreenJustEntered = petScreenNow && !s_petScreenWasActiveLastFrame;
  s_petScreenWasActiveLastFrame = petScreenNow;

  // Existing pets should start at home in their normal mood animation.
  // Only preserve offscreen/intro positioning when the scripted hatch intro
  // is actually active.
  if (petScreenJustEntered)
  {
    const bool scriptedIntroActive = petPresentationScriptedIntroActive();

    if (!scriptedIntroActive)
    {
      resetPetScreenPositionToHome();
      requestUIRedraw();
    }
  }

  const bool petMotionActive = petPresentationAnimating();

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

  uiDrawAlertScreenFlashOverlay();
  uiDrawToastOverlay();

  spr.pushSprite(0, 0);

  bgDrawnForState = true;
  lastDrawnState = g_app.uiState;
}

// ============================================================================
// New pet flow screens
// ============================================================================
void drawPetPerfHud()
{
  if (!g_petPerfHudEnabled)
    return;

  if (g_app.currentTab != Tab::TAB_PET)
    return;

  if (g_app.uiState != UIState::PET_SCREEN)
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

AnimId deathTransitionStaticClipForPet()
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

  default:
    return ANIM_NONE;
  }
}