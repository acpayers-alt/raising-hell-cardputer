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
#include "graphics_hud_icons.h"
#include "graphics_menu_screens.h"
#include "graphics_mini_stats.h"
#include "graphics_name_pet_screens.h"
#include "graphics_nonpet_bg.h"
#include "graphics_overlays.h"
#include "graphics_pet_bg_paths.h"
#include "graphics_pet_presentation.h"
#include "graphics_render_utils.h"
#include "graphics_sd_draw.h"
#include "graphics_set_time_screens.h"
#include "graphics_settings_screens.h"
#include "graphics_shared_utils.h"
#include "graphics_sleep_frame_cache.h"
#include "graphics_special_screens.h"
#include "graphics_screen_dispatch.h"
#include "graphics_tab_screens.h"
#include "graphics_tab_menus.h"
#include "graphics_ui_common.h"
#include "graphics_wifi_screens.h"

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

// Sleep Graphics Kick
volatile bool g_sleepBgKick = false;

// Misc Helpers
void sleepBgKickNow()
{
  g_sleepBgKick = true;
  requestUIRedraw();
}

// -----------------------------------------------------------------------------
// Paths (SD)
// -----------------------------------------------------------------------------
const char *PATH_BG_SLEEP = "/raising_hell/graphics/background/sleep_bg.jpg";
static const char *PATH_BG_SPLASH = "/raising_hell/graphics/background/flow/rh_splash.jpg";

static constexpr int MINI_STAT_ICON_W = 18;
static constexpr int MINI_STAT_ICON_H = 18;
static constexpr uint16_t MINI_STAT_ICON_TRANSPARENT = 0xF81F;

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

#define DEV_EGG_PNG "/raising_hell/graphics/pet/egg/dev_egg.png"
#define ELD_EGG_PNG "/raising_hell/graphics/pet/egg/eld_egg.png"

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

void drawBootLowBatteryChargingScreen(int mv, int pct, bool usb, bool readyToBoot);

static bool ensurePetLayer();
void cachePetAreaBackgroundIfNeeded(bool needPetBg);
void restorePetAreaFromCache();

// -----------------------------------------------------------------------------
// Background caching per UI state
// -----------------------------------------------------------------------------
static UIState lastDrawnState = UIState::PET_SCREEN;
static bool bgDrawnForState = false;

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

void graphicsReleaseUiCachesForMiniGame()
{
  // Release pet presentation caches through the owning module.
  graphicsReleasePetLayerForOta();

  // Release mini stat icon caches through the owning module.
  graphicsReleaseMiniStatCaches();

  // Release non-pet tiled background cache through the owning module.
  graphicsReleaseNonPetTileCache();

  // Release cached sleep animation full-screen frame buffers.
  freeSleepAnimFrameCache();

  // Force the UI to rebuild cleanly when we return.
  bgDrawnForState = false;
  lastDrawnState = (UIState)255;
  invalidateBackgroundCache();
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