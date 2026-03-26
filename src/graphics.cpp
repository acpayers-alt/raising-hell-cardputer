#include "graphics.h"

#include "app_state.h"
#include "boot_pipeline.h"
#include "display.h"
#include "display_dims_state.h"
#include "tft_compat.h"
#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <cstring>
#include <time.h>

#include "console.h"
#include "death_state.h"
#include "game_options_state.h"
#include "input.h"
#include "sound.h"

#include "save_manager.h"
#include "savegame.h"
#include "sdcard.h"

#include "settings_state.h"
#include "timezone.h"
#include "wifi_time.h"

#include "anim_engine.h"
#include "inventory.h"
#include "pet.h"
#include "pet_age.h"

#include "auto_screen.h"
#include "graphics_assets.h"
#include "ui_runtime.h"

#include "anim_clips.h"
#include "asset_ota.h"
#include "boot_state.h"
#include "brightness_state.h"
#include "build_flags.h"
#include "death_state.h"
#include "factory_reset_state.h"
#include "flow_boot_wifi.h"
#include "inventory_state.h"
#include "mg_pause_menu.h"
#include "mini_game_pause_menu.h"
#include "mini_games.h"
#include "name_entry_state.h"
#include "new_pet_flow_state.h"
#include "settings_flow_state.h"
#include "settings_nav_state.h"
#include "shop_items.h"
#include "system_status_state.h"
#include "time_state.h"
#include "ui_death_menu.h"
#include "ui_feed_menu.h"
#include "ui_menu_state.h"
#include "ui_play_menu.h"
#include "ui_power_menu.h"
#include "ui_sleep_menu.h"
#include "user_toggles_state.h"
#include "version.h"
#include "wifi_setup_state.h"
#include <lgfx/v1/misc/DataWrapper.hpp>

// --- Cache/Draw Helpers
bool g_forcePetBgCache = false;
static void drawBurialScreen();

void drawHatchingScreen(bool redrawBg);
static void drawEvolutionScreen();

static void drawMiniStatPreview();
static void drawMiniStatPreviewSleepLeft();

static void drawPetPerfHud();

// --- Compatibility wrappers / missing helpers (compile fix) ---
static void drawSettingsScreen();
static void drawInventoryScreen();
static void drawPetSleepingScreen();
static void drawMiniGameScreen();
static bool getPngWH(const char *path, int &outW, int &outH);
static void drawCenteredImageSpr(const char *path, int cx, int cy);
static void drawCrackedEggBig(int cx, int cy);
static bool getPngWH(const char *path, int &outW, int &outH);
static void drawCenteredImageSpr(const char *path, int cx, int cy);
static void drawCrackedEggBig(int cx, int cy);

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

// Utility: toast message overlay (timed)
static bool g_toastActive = false;
static uint32_t g_toastUntilMs = 0;
static char g_toastMsg[64] = {0};

static void uiDrawToastOverlay();
// Sleep Graphics Kick
static volatile bool g_sleepBgKick = false;

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
static constexpr int PET_X_OFFSET = -20;
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

static const char *PATH_STAT_KAIJU = "/raising_hell/graphics/pet/kai_stat.png";
static const char *PATH_STAT_AXOLOTL = "/raising_hell/graphics/pet/axo_stat.png";
static const char *PATH_STAT_ANUBIS = "/raising_hell/graphics/pet/anu_stat.png";
static const char *PATH_STAT_ALIEN = "/raising_hell/graphics/pet/al_stat.png";

static const char *PATH_STAT_ELDRITCH = "/raising_hell/graphics/pet/eld_stat.png";
static const char *PATH_STAT_ELDRITCH_BABY = "/raising_hell/graphics/pet/eld_stat.png";
static const char *PATH_STAT_ELDRITCH_TEEN = "/raising_hell/graphics/pet/eld_teen_stat.png";
static const char *PATH_STAT_ELDRITCH_ADULT = "/raising_hell/graphics/pet/eld_ad_stat.png";
static const char *PATH_STAT_ELDRITCH_ELDER = "/raising_hell/graphics/pet/eld_el_stat.png";

static const char *PATH_STAT_DEVIL_BABY = "/raising_hell/graphics/pet/dev_stat.png";
static const char *PATH_STAT_DEVIL_TEEN = "/raising_hell/graphics/pet/dev_teen_stat.png";
static const char *PATH_STAT_DEVIL_ADULT = "/raising_hell/graphics/pet/dev_ad_stat.png";
static const char *PATH_STAT_DEVIL_ELDER = "/raising_hell/graphics/pet/dev_el_stat.png";
static const char *PATH_STAT_DEFAULT = "/raising_hell/graphics/pet/dev_stat.png";

static const char *DEV_FACE_HAPPY_BABY = "/raising_hell/graphics/pet/face/dev_face_hpy.png";
static const char *DEV_FACE_HAPPY_TEEN = "/raising_hell/graphics/pet/face/dev_teen_face_hpy.png";
static const char *DEV_FACE_HAPPY_ADULT = "/raising_hell/graphics/pet/face/dev_ad_face_hpy.png";
static const char *DEV_FACE_HAPPY_ELDER = "/raising_hell/graphics/pet/face/dev_el_face_hpy.png";

static const char *PATH_INF_COIN = "/raising_hell/graphics/ui/icons/inf_coin.png";
static const char *PATH_LIFE_ICON = "/raising_hell/graphics/ui/icons/life_icon.png";

static constexpr int MINI_STAT_ICON_W = 22;
static constexpr int MINI_STAT_ICON_H = 22;
static constexpr uint16_t MINI_STAT_ICON_TRANSPARENT = 0x0001;

static M5Canvas s_miniStatLifeIcon(&spr);
static bool s_miniStatLifeIconReady = false;
static M5Canvas s_miniStatCoinIcon(&spr);
static bool s_miniStatCoinIconReady = false;

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

  if (!canvasDrawPngFromSD(canvas, path, 0, 0))
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

static const char *devilHappyFaceForStage(uint8_t evoStage)
{
  if (evoStage >= 3)
    return DEV_FACE_HAPPY_ELDER;
  if (evoStage == 2)
    return DEV_FACE_HAPPY_ADULT;
  if (evoStage == 1)
    return DEV_FACE_HAPPY_TEEN;
  return DEV_FACE_HAPPY_BABY;
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

// -----------------------------------------------------------------------------
// Controls Helper
// -----------------------------------------------------------------------------
static void drawControlsHelpScreen()
{
  spr.fillScreen(TFT_BLACK);

  spr.setTextDatum(textdatum_t::top_left);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);

  int x = 10;
  int y = 10;

  // Title
  spr.setTextColor(TFT_CYAN, TFT_BLACK);
  spr.drawString("Controls", x, y);
  y += 15; // tighter than before

  spr.setTextColor(TFT_WHITE, TFT_BLACK);

  const int lineGap = 12; // tighter vertical rhythm

  spr.drawString("LEFT/RIGHT : switch tabs", x, y);
  y += lineGap;
  spr.drawString("UP/DOWN    : move selection", x, y);
  y += lineGap;
  spr.drawString("ENTER/G    : confirm", x, y);
  y += lineGap;
  spr.drawString("Del/Q      : back / home", x, y);
  y += lineGap;
  spr.drawString("ESC        : open Settings / cancel", x, y);
  y += lineGap;
  spr.drawString("Z-M        : jump to tab", x, y);
  y += lineGap;
  spr.drawString("GO         : toggle screen on/off", x, y);
  y += lineGap;
  spr.drawString("           : shake console to wake", x, y);
  y += lineGap;
  spr.drawString("\\          : console", x, y);
  y += lineGap;

  // Alt Navigation Cluster section
  y += 2; // small separation
  spr.setTextColor(TFT_DARKGREY, TFT_BLACK);
  spr.drawString("Alt Navigation Clusters", x, y);
  y += 14;

  spr.drawString("(E A S D) (O J K L)", x, y);

  spr.pushSprite(0, 0);
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
  spr.drawString("ENTER: Set up Wi-Fi", 10, 122);

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
  spr.fillScreen(TFT_BLACK);
  spr.setTextDatum(TL_DATUM);
  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);

  const char *ssid = wifiConsoleSsid();
  const uint32_t ageMs = wifiConsoleConnectAgeMs();
  const uint32_t ageS = ageMs / 1000;
  const char *st = wifiConsoleStatusString();

  spr.drawString("Connecting WiFi...", 10, 10);

  if (ssid && ssid[0])
    spr.drawString((String("SSID: ") + ssid).c_str(), 10, 28);
  else
    spr.drawString("SSID: (none)", 10, 28);

  spr.drawString((String("Status: ") + st).c_str(), 10, 46);
  spr.drawString((String("Elapsed: ") + String(ageS) + "s").c_str(), 10, 64);

  if (connected)
  {
    spr.drawString("Connected", 10, 86);
    spr.drawString(("RSSI: " + String(rssi)).c_str(), 10, 104);
    spr.drawString("Advancing to Timezone...", 10, 126);
  }
  else
  {
    // Make failure state obvious after a reasonable wait.
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
static void drawWifiSetupScreen();
static void drawNamePetScreen(bool redrawBg);
static void drawDecayModePickerMenu();
static void drawScreenSettingsMenu();
static void drawSystemSettingsMenu();
static void drawWifiSettingsMenu();
static void drawPlaceholderMenu(const char *title);
static void drawCreditsScreen();

void drawConsoleMenu();
void drawChoosePetScreen(bool redrawBg);
void drawPowerMenu(); // non-static (renderUI calls it)
void ui_drawMessageWindow(const char *title, const char *line1, const char *line2, bool maskLine2, bool showCursor);
void ui_showMessage(const char *msg);
static void uiDrawToastOverlay();

static bool ensurePetLayer();
static void cachePetAreaBackgroundIfNeeded(bool needPetBg);
static void restorePetAreaFromCache();

// -----------------------------------------------------------------------------
// Background caching per UI state
// -----------------------------------------------------------------------------
static UIState lastDrawnState = UIState::PET_SCREEN;
static bool bgDrawnForState = false;

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
  if (on)
  {
    bgDrawnForState = false;
    invalidateBackgroundCache();
  }
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
  drawTopBar();

  const int contentY = TOP_BAR_H;
  const int contentH = SCREEN_H - TOP_BAR_H;
  const int contentBottom = contentY + contentH;

  spr.fillRect(0, contentY, SCREEN_W, contentH, TFT_BLACK);

  char volumeLine[24];
  snprintf(volumeLine, sizeof(volumeLine), "Volume: %s", soundVolumeToText(soundGetVolumeLevel()));

  static const char *labelsStatic[] = {nullptr, // 0 => volumeLine
                                       "Controls",       "Screen Settings >", "System Settings >",
                                       "Game Options >", "Console >",         "Credits"};

  const int totalItems = 7;

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
    const int y = startY + row * (itemH + gap);
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
    if (i == 0)
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

static void drawGameOptionsMenu()
{
  drawTopBar();

  const int contentY = TOP_BAR_H;
  const int contentH = SCREEN_H - TOP_BAR_H;

  spr.fillRect(0, contentY, SCREEN_W, contentH, TFT_BLACK);

  const char *renameLine = "Rename Pet";

  char decayLine[32];
  snprintf(decayLine, sizeof(decayLine), "Decay Mode: %s", decayModeToText(saveManagerGetDecayMode()));

  char deathLine[32];
  snprintf(deathLine, sizeof(deathLine), "Pet Death: %s", petDeathEnabled ? "ON" : "OFF");

  char ledLine[32];
  snprintf(ledLine, sizeof(ledLine), "LED Alerts: %s", ledAlertsEnabled ? "ON" : "OFF");

  char perfLine[32];
  snprintf(perfLine, sizeof(perfLine), "Pet Perf HUD: %s", g_petPerfHudEnabled ? "ON" : "OFF");

  const char *labels[] = {renameLine, decayLine, deathLine, ledLine, perfLine};
  const int totalItems = 5;

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
  drawTopBar();

  const int contentY = TOP_BAR_H;
  const int contentH = SCREEN_H - TOP_BAR_H;

  spr.fillRect(0, contentY, SCREEN_W, contentH, TFT_BLACK);

  char bLine[28];
  snprintf(bLine, sizeof(bLine), "Brightness: %s", brightnessToText(brightnessLevel));

  char aLine[28];
  snprintf(aLine, sizeof(aLine), "Auto Screen: %s", autoScreenToText((uint8_t)autoScreenTimeoutSel));

  const char *labels[] = {bLine, aLine};
  const int totalItems = 2;

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
  drawTopBar();

  const int contentY = TOP_BAR_H;
  const int contentH = SCREEN_H - TOP_BAR_H;

  spr.fillRect(0, contentY, SCREEN_W, contentH, TFT_BLACK);

  char wLine[28];
  snprintf(wLine, sizeof(wLine), "WiFi: %s", wifiIsEnabled() ? "ON" : "OFF");

  char tzLine[36];
  snprintf(tzLine, sizeof(tzLine), "Time Zone: %s", tzName(tzIndex));

  char otaCheckLine[40];
  snprintf(otaCheckLine, sizeof(otaCheckLine), "Check/Fix Assets");

#if PUBLIC_BUILD
  const char *labels[] = {wLine, "Set WiFi Network", "Reset WiFi Settings", tzLine, otaCheckLine};
  const int totalItems = 5;
#else
  char otaChannelLine[32];
  snprintf(otaChannelLine, sizeof(otaChannelLine), "OTA Channel: %s",
           ((AssetOtaChannel)assetOtaGetConfig().channel == AssetOtaChannel::DEV) ? "Dev" : "Public");

  const char *labels[] = {wLine, "Set WiFi Network", "Reset WiFi Settings", tzLine, otaCheckLine, otaChannelLine};
  const int totalItems = 6;
#endif

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

    spr.setTextColor(textCol, fill);
    spr.drawString(labels[i], boxX + 10, ty);
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
  spr.drawString("HOLD ENTER: Reset   MENU: Cancel", screenW / 2, y + boxH - 2);

  spr.setTextDatum(TL_DATUM);
}

static void drawSystemSettingsMenu()
{
  drawTopBar();

  const int contentY = TOP_BAR_H;
  const int contentH = SCREEN_H - TOP_BAR_H;

  spr.fillRect(0, contentY, SCREEN_W, contentH, TFT_BLACK);

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
  spr.drawString("If new assets are downloaded,", screenW / 2, y + 28);
  spr.drawString("the system will reboot.", screenW / 2, y + 40);

  spr.setTextDatum(BC_DATUM);
  spr.drawString("ENTER: Continue   MENU: Cancel", screenW / 2, y + boxH - 6);

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

  spr.setTextColor(TFT_DARKGREY, TFT_BLACK);

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

void drawSettingsMenu()
{
  switch (g_settingsFlow.settingsPage)
  {
  default:
  case SettingsPage::TOP:
    drawSettingsTopMenu();
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
  case SettingsPage::CREDITS:
    drawCreditsScreen();
    break;
  case SettingsPage::AUTO_SCREEN:
    drawAutoScreenPickerMenu();
    break;
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
static void drawTinyBar(int x, int y, int w, int h, uint16_t fill, uint16_t outline, int value01_100);
static void drawTinyBarV(int x, int y, int w, int h, uint16_t fill, uint16_t outline, int value01_100);

void drawShopScreen()
{
  drawTopBar();
  drawTabBar();
  clearContentArea(TFT_BLACK);

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
  drawTopBar();
  drawTabBar();
  clearContentArea(TFT_BLACK);

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
  drawTopBar();
  drawTabBar();
  clearContentArea(TFT_BLACK);

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

  drawTinyBarV(barX, y0 + 0 * (barH + gapY), barW, barH, colHunger, colHunger, pet.hunger);
  drawTinyBarV(barX, y0 + 1 * (barH + gapY), barW, barH, colMood, colMood, pet.happiness);
  drawTinyBarV(barX, y0 + 2 * (barH + gapY), barW, barH, colEnergy, colEnergy, pet.energy);

  spr.setTextDatum(TL_DATUM);
}

void drawInventoryMenu()
{
  drawTopBar();
  drawTabBar();
  clearContentArea(TFT_BLACK);

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

  const bool needRestore = animChanged;

  cachePetAreaBackgroundIfNeeded(needPetBg);
  g_forcePetBgCache = false;

  if (needPetBg || needRestore)
  {
    restorePetAreaFromCache();
  }

  drawTopBar();

  const int petAreaW = SCREEN_W - MINI_STAT_W - MINI_STAT_PAD;
  const int petAreaX = 0;

  const PetRenderProfile &prof = getPetProfile(pet.type);

  const int centerX = petAreaX + (petAreaW / 2) + prof.xOff;
  const int bottomY = (PET_AREA_Y + PET_AREA_H) + prof.yOff;

  animDrawPetFrameAnchoredBottom(centerX, bottomY);

  drawMiniStatPreview();
  drawTabBar();

  drawPetPerfHud(); // <-- add it here
}

// -----------------------------------------------------------------------------
// Missing wrappers + helpers (linker fix)
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
  spr.fillRect(0, contentY, SCREEN_W, contentH, TFT_BLACK);

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
static void drawPlayTabMock(bool redrawBg)
{
  if (!isScreenOn())
    return;

  if (redrawBg)
  {
    spr.fillRect(0, TOP_BAR_H, SCREEN_W, SCREEN_H - TOP_BAR_H - TAB_BAR_H, TFT_BLACK);
  }

  drawTopBar();
  drawTabBar();
  clearContentArea(TFT_BLACK);

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

static bool ensureSleepAnimFrameCache(uint8_t mode, const char *const *frames, uint8_t frameCount, int drawX, int drawY)
{
  if (mode == 0 || !frames || frameCount == 0)
    return false;

  // Already built for this mode/count?
  if (s_sleepAnimFrameCacheReady && s_sleepAnimFrameCache && s_sleepAnimFrameCacheMode == mode &&
      s_sleepAnimFrameCacheCnt == frameCount)
  {
    return true;
  }

  freeSleepAnimFrameCache();

  uint16_t *sprBuf = (uint16_t *)spr.getBuffer();
  if (!sprBuf)
    return false;

  const size_t pxCount = (size_t)SCREEN_W * (size_t)SCREEN_H;
  const size_t bufBytes = pxCount * sizeof(uint16_t);

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
static void drawTinyBar(int x, int y, int w, int h, uint16_t fill, uint16_t outline, int value01_100)
{
  value01_100 = clampi(value01_100, 0, 100);

  spr.drawRect(x, y, w, h, outline);

  int innerW = w - 2;
  int innerH = h - 2;
  int fillW = (innerW * value01_100) / 100;

  spr.fillRect(x + 1, y + 1, innerW, innerH, TFT_BLACK);
  spr.fillRect(x + 1, y + 1, fillW, innerH, fill);
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
  const int panelW = 56;

  const int barW = panelW - 10;
  const int barH = 6;
  const int lineH = 14;
  const int gapY = 6;

  const int ICON_SIZE = 22;
  const int ICON_GAP_Y = 3;
  const int ICON_MOVE_UP = -5;

  const int TEXT_INSET_Y = 4;

  const uint16_t colHunger = 0xF800;
  const uint16_t colMood = 0x001F;
  const uint16_t colEnergy = 0x07E0;

  const int totalH = (3 * lineH) + gapY + (showCoin ? (2 * lineH) : (1 * lineH));

  int y0 = PET_AREA_Y + (PET_AREA_H - totalH) / 2;
  y0 -= 3;

  drawTinyBar(x0 + 4, y0 + 0 * lineH, barW, barH, colHunger, colHunger, pet.hunger);
  drawTinyBar(x0 + 4, y0 + 1 * lineH, barW, barH, colMood, colMood, pet.happiness);
  drawTinyBar(x0 + 4, y0 + 2 * lineH, barW, barH, colEnergy, colEnergy, pet.energy);

  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);

  const int textY = y0 + 3 * lineH + gapY;

  if (alignRight)
  {
    const int barsRight = (x0 + 4) + barW;
    const int ICON_PAD_R = 0;
    const int ICON_TEXT_GAP = 6;

    const int iconX = barsRight - ICON_SIZE - ICON_PAD_R;
    const int lifeY = (textY - 4) + ICON_MOVE_UP;
    const int coinY = lifeY + ICON_SIZE + ICON_GAP_Y;

    const int numRightX = iconX - ICON_TEXT_GAP;

    spr.setTextDatum(TR_DATUM);

    bool okLife = drawMiniStatIconCached(PATH_LIFE_ICON, iconX, lifeY);
    (void)okLife;
    drawMiniStatNumberRight(pet.health, numRightX, lifeY + TEXT_INSET_Y);

    if (showCoin)
    {
      bool okCoin = drawMiniStatIconCached(PATH_INF_COIN, iconX, coinY);
      (void)okCoin;
      drawMiniStatNumberRight(pet.inf, numRightX, coinY + TEXT_INSET_Y);
    }

    spr.setTextDatum(TL_DATUM);
    return;
  }

  // Left-aligned variant (used on sleep screen)
  const int iconX = x0 + 2;
  const int lifeY = (textY - 4) + ICON_MOVE_UP;
  const int coinY = lifeY + ICON_SIZE + ICON_GAP_Y;

  spr.setTextDatum(TR_DATUM);
  const int numRightX = x0 + panelW - 2;

  bool okLife = drawMiniStatIconCached(PATH_LIFE_ICON, iconX, lifeY);
  (void)okLife;
  drawMiniStatNumberRight(pet.health, numRightX, lifeY + TEXT_INSET_Y);

  if (showCoin)
  {
    bool okCoin = drawMiniStatIconCached(PATH_INF_COIN, iconX, coinY);
    (void)okCoin;
    drawMiniStatNumberRight(pet.inf, numRightX, coinY + TEXT_INSET_Y);
  }

  spr.setTextDatum(TL_DATUM);
}

static void drawMiniStatPreview()
{
  const int panelW = 56;
  const int x0 = SCREEN_W - panelW - 4;
  drawMiniStatPreviewAt(x0, /*showCoin=*/true, /*alignRight=*/true);
}

static void drawMiniStatPreviewSleepLeft()
{
  const int x0 = 4;
  const int panelW = 56;

  const int barW = panelW - 10;
  const int barH = 6;
  const int lineH = 14;
  const int gapY = 6;

  const int ICON_SIZE = 22;
  const int ICON_GAP_Y = 3;
  const int ICON_MOVE_UP = -5;
  const int TEXT_INSET_Y = 4;

  const uint16_t colHunger = 0xF800;
  const uint16_t colMood = 0x001F;
  const uint16_t colEnergy = 0x07E0;

  // Add one extra small line for Sleep Quality
  const int qualityLineH = 10;

  const int totalH = (3 * lineH) + gapY + (1 * lineH) + qualityLineH;

  int y0 = PET_AREA_Y + (PET_AREA_H - totalH) / 2;
  y0 -= 3;

  drawTinyBar(x0 + 4, y0 + 0 * lineH, barW, barH, colHunger, colHunger, pet.hunger);
  drawTinyBar(x0 + 4, y0 + 1 * lineH, barW, barH, colMood, colMood, pet.happiness);
  drawTinyBar(x0 + 4, y0 + 2 * lineH, barW, barH, colEnergy, colEnergy, pet.energy);

  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);

  const int textY = y0 + 3 * lineH + gapY;

  // Left-aligned health readout (same as drawMiniStatPreviewAt left variant)
  const int iconX = x0 + 2;
  const int lifeY = (textY - 4) + ICON_MOVE_UP;

  spr.setTextDatum(TR_DATUM);
  const int numRightX = x0 + panelW - 2;

  bool okLife = drawMiniStatIconCached(PATH_LIFE_ICON, iconX, lifeY);
  (void)okLife;
  drawMiniStatNumberRight(pet.health, numRightX, lifeY + TEXT_INSET_Y);

  // Sleep quality line under the health readout
  spr.setTextDatum(TL_DATUM);
  spr.setTextFont(1);
  spr.setTextSize(1);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);

  const int qY = lifeY + ICON_SIZE + 2;
  spr.drawString("Sleep:", x0 + 2, qY);
  spr.drawString(pet.getSleepQualityLabel(), x0 + 40, qY); // aligned under health row

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
      const int y = startY + row * (itemH + gap);
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

  spr.setTextDatum(CC_DATUM);

  if (g_wifi.connectResultPending)
  {
    if (g_wifi.connectResultSuccess)
    {
      spr.setTextColor(TFT_WHITE, TFT_BLACK);
      spr.drawString("Connected!", SCREEN_W / 2, contentY + 34, 2);
    }
    else
    {
      spr.setTextColor(TFT_WHITE, TFT_BLACK);
      spr.drawString("Connection failed", SCREEN_W / 2, contentY + 28, 2);
      spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
      spr.drawString("Please retry", SCREEN_W / 2, contentY + 52, 2);
    }

    spr.setTextDatum(TL_DATUM);
    return;
  }

  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.drawString("Connecting...", SCREEN_W / 2, contentY + 22, 2);

  if (wifiSetupSsid[0])
  {
    spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    spr.drawString(wifiSetupSsid, SCREEN_W / 2, contentY + 46, 2);
  }

  const uint32_t ageMs = wifiConsoleConnectAgeMs();
  char line[24];
  snprintf(line, sizeof(line), "%lus", (unsigned long)(ageMs / 1000));
  spr.drawString(line, SCREEN_W / 2, contentY + 70, 2);

  spr.setTextDatum(TL_DATUM);
}

// ============================================================================
// MAIN RENDER DISPATCHER HELPER
// ============================================================================
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
    drawPlayTabMock(redrawBg);
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

  if ((g_bootUiBlockedForAssetProvision || g_bootAssetProvisionActive) && !uiIsBootWifiOnboardingState(g_app.uiState))
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

  // If nothing changed, we normally throttle renders…
  // BUT if someone requested a redraw (sleep anim heartbeat, etc) we must not skip it.
  const bool redrawRequested = consumeUIRedrawRequest();

  if (!tabChanged && !stateChanged && !bgInvalid && !redrawRequested)
  {
    const uint32_t now = millis();
    const uint32_t gateMs = consoleIsOpen() ? 16 : 50;
    if (now - lastRenderTimeMs < gateMs)
      return;
    lastRenderTimeMs = now;
  }
  else
  {
    // Something changed or a redraw was explicitly requested — don't throttle.
    lastRenderTimeMs = millis();
  }

  lastTab = tabNow;

  const bool redrawBg = (!bgDrawnForState) || bgInvalid;

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
    spr.drawString("ENTER: Next   MENU: Cancel", screenW / 2, y + boxH - 6);
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
    spr.drawString("ENTER: Next   MENU: Cancel", screenW / 2, y + boxH - 6);
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
void ui_showMessage(const char *msg)
{
  if (!msg)
    return;

  strncpy(g_toastMsg, msg, sizeof(g_toastMsg) - 1);
  g_toastMsg[sizeof(g_toastMsg) - 1] = '\0';

  g_toastActive = true;
  g_toastUntilMs = millis() + 900; // show for ~0.9s

  // Ensure the next render paints it
  requestUIRedraw();
}

static void uiDrawToastOverlay()
{
  if (!g_toastActive)
    return;

  const uint32_t now = millis();
  if ((int32_t)(now - g_toastUntilMs) >= 0)
  {
    g_toastActive = false;
    g_toastMsg[0] = '\0';
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

static void drawCrackedEggBig(int cx, int cy)
{
  const char *path = pendingEggCrackedPng();
  if (!path || !path[0] || !g_sdReady)
    return;

  // Devil egg should NOT be scaled
  if (g_pendingPetType == PET_DEVIL)
  {
    drawCenteredImageSpr(path, cx, cy);
    return;
  }

  int w = 0;
  int h = 0;
  if (!getPngWH(path, w, h) || w <= 0 || h <= 0)
  {
    drawCenteredImageSpr(path, cx, cy);
    return;
  }

  M5Canvas egg(&M5.Display);
  egg.setColorDepth(16);

  if (!egg.createSprite(w, h))
  {
    drawCenteredImageSpr(path, cx, cy);
    return;
  }

  egg.fillSprite(TFT_BLACK);

  if (!egg.drawPngFile(SD, path, 0, 0))
  {
    egg.deleteSprite();
    drawCenteredImageSpr(path, cx, cy);
    return;
  }

  egg.setPivot(w / 2, h / 2);

  const float scale = 1.6f;
  egg.pushRotateZoom(&spr, cx, cy, 0.0f, scale, scale);

  egg.deleteSprite();
}

static void drawCenteredLine(const char *s, int y, int font = 2, int size = 1)
{
  spr.setTextDatum(TC_DATUM);
  spr.setTextFont(font);
  spr.setTextSize(size);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.drawString(s ? s : "", screenW / 2, y);
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

  static constexpr int EGG_TEXT_NUDGE_Y = 4;

  drawCenteredLine(label, (screenH / 2) + 32 + EGG_TEXT_NUDGE_Y, 1, 1);
  drawCenteredLine("Press ENTER to hatch", screenH - 22 + EGG_TEXT_NUDGE_Y, 1, 1);

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
  const int animEggY = 92;
  const int crackedEggY = 78;

  const char *const *crackFrames = pendingEggCrackFrames();

  if (!g_app.flow.hatch.showingMsg)
  {
    if (g_app.flow.hatch.frame < 4)
      drawCenteredImageSpr(crackFrames[g_app.flow.hatch.frame], centerX, animEggY);
    else
      drawCrackedEggBig(centerX, crackedEggY);

    return;
  }

  drawCrackedEggBig(centerX, crackedEggY);

  spr.setTextDatum(MC_DATUM);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.drawString(pendingHatchMessage(), centerX, 122);
}

// ============================================================================
// Death screen
// ============================================================================
static void drawDeathScreen(bool /*redrawBg*/)
{
  spr.fillSprite(TFT_BLACK);

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