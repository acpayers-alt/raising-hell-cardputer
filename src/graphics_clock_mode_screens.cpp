#include "graphics_clock_mode_screens.h"

#include "display.h"
#include "graphics.h"
#include "graphics_pet_presentation.h"
#include "pet.h"
#include "save_manager.h"

extern Pet pet;
extern bool g_sdReady;
extern bool g_forcePetBgCache;

bool isScreenOn();
void requestUIRedraw();

void cachePetAreaBackgroundIfNeeded(bool forceRefresh);
void restorePetAreaFromCache();

void drawTopBar();

bool sprDrawJpgFromSD(const char *path, int x, int y);
bool sprDrawPngFromSD(const char *path, int x, int y);
void animDrawPetFrameAnchoredBottom(int anchorCenterX, int anchorBottomY);
bool animConsumeFrameChanged();

void drawClockModeScreen(bool redrawBg)
{
  if (!isScreenOn())
    return;

  const bool hasLivePet = saveManagerSaveFileExists() || (saveManagerGetBirthEpoch() != 0 && pet.getName()[0] != '\0');

  if (!hasLivePet)
  {
    spr.fillScreen(TFT_BLACK);

    time_t now = time(nullptr);
    tm tmNow = {};
    localtime_r(&now, &tmNow);

    int hour12 = tmNow.tm_hour % 12;
    if (hour12 == 0)
      hour12 = 12;

    char timeBuf[8];
    snprintf(timeBuf, sizeof(timeBuf), "%d:%02d", hour12, tmNow.tm_min);

    static const char *kWeekdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char *kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    const char *weekday = (tmNow.tm_wday >= 0 && tmNow.tm_wday < 7) ? kWeekdays[tmNow.tm_wday] : "---";
    const char *month = (tmNow.tm_mon >= 0 && tmNow.tm_mon < 12) ? kMonths[tmNow.tm_mon] : "---";

    char dateBuf[24];
    snprintf(dateBuf, sizeof(dateBuf), "%s %s %d", weekday, month, tmNow.tm_mday);

    spr.setTextColor(TFT_WHITE);

    spr.setTextDatum(TC_DATUM);
    spr.drawString(timeBuf, SCREEN_W / 2, SCREEN_H / 2 - 20, 7);

    spr.setTextDatum(TC_DATUM);
    spr.drawString(dateBuf, SCREEN_W / 2, 20, 4);

    spr.setTextDatum(BC_DATUM);
    spr.drawString("ESC: Back", SCREEN_W / 2, SCREEN_H - 4, 1);

    spr.setTextDatum(TL_DATUM);
    return;
  }

  static PetType s_lastBgPetType = (PetType)255;
  static uint8_t s_lastBgEvoStage = 255;

  const bool petChanged = (s_lastBgPetType != pet.type) || (s_lastBgEvoStage != pet.evoStage);
  const bool needPetBg = redrawBg || petChanged || g_forcePetBgCache;

  s_lastBgPetType = pet.type;
  s_lastBgEvoStage = pet.evoStage;

  const bool animChanged = animConsumeFrameChanged();
  const bool needRestore = redrawBg || animChanged || needPetBg;

  if (petPresentationHasIntroHandoff() && animChanged)
  {
    clearPetPresentationIntroHandoff();
    requestUIRedraw();
  }

  cachePetAreaBackgroundIfNeeded(needPetBg);
  g_forcePetBgCache = false;

  if (needRestore)
  {
    // Clock Mode owns the full frame. Always repaint the non-pet regions so
    // transient UI like the power menu cannot bleed through.
    spr.fillRect(0, 0, SCREEN_W, PET_AREA_Y, TFT_BLACK);
    spr.fillRect(0, PET_AREA_Y + PET_AREA_H, SCREEN_W, SCREEN_H - (PET_AREA_Y + PET_AREA_H), TFT_BLACK);

    restorePetAreaFromCache();
  }

  // Keep Clock Mode on the same vertical baseline as the PET screen.
  // Only override X here; Y should match the normal pet home anchor.
  int homeX = 0;
  int homeY = 0;
  getPetHomeScreenPosition(homeX, homeY);

  const int clockHomeX = (SCREEN_W / 2) - 65;
  const int clockHomeY = homeY;

  const PetMood mood = petResolveMood(pet);
  const bool wanderAllowed = (mood == MOOD_HAPPY || mood == MOOD_BORED);

  time_t now = time(nullptr);
  tm tmNow = {};
  localtime_r(&now, &tmNow);

  int hour12 = tmNow.tm_hour % 12;
  if (hour12 == 0)
    hour12 = 12;

  char timeBuf[8];
  snprintf(timeBuf, sizeof(timeBuf), "%d:%02d", hour12, tmNow.tm_min);

  static const char *kWeekdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  static const char *kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

  const char *weekday = (tmNow.tm_wday >= 0 && tmNow.tm_wday < 7) ? kWeekdays[tmNow.tm_wday] : "---";
  const char *month = (tmNow.tm_mon >= 0 && tmNow.tm_mon < 12) ? kMonths[tmNow.tm_mon] : "---";

  char dateBuf[24];
  snprintf(dateBuf, sizeof(dateBuf), "%s %s %d", weekday, month, tmNow.tm_mday);

  spr.setTextColor(TFT_WHITE);

  spr.setTextDatum(TC_DATUM);
  spr.drawString(timeBuf, SCREEN_W / 2, SCREEN_H / 2 - 20, 7);

  spr.setTextDatum(TC_DATUM);
  spr.drawString(dateBuf, SCREEN_W / 2, 20, 4);

  // Clock Mode uses its own horizontal placement, but the vertical anchor and
  // motion ownership now live in the presentation module.
  if (wanderAllowed && petWalkOverrideActive())
  {
    if (!drawIntroWalkingPetOverride())
      animDrawPetFrameAnchoredBottom(petPresentationX(), petPresentationY());
  }
  else
  {
    const int drawX = wanderAllowed ? petPresentationX() : clockHomeX;
    const int drawY = wanderAllowed ? petPresentationY() : clockHomeY;
    animDrawPetFrameAnchoredBottom(drawX, drawY);
  }

  // Always repaint the footer strip in Clock Mode.
  // Do not rely on the pet-area restore path to preserve it.
  spr.fillRect(0, SCREEN_H - TAB_BAR_H, SCREEN_W, TAB_BAR_H, TFT_BLACK);

  spr.setTextDatum(BC_DATUM);
  spr.drawString("ESC: Back", SCREEN_W / 2, SCREEN_H - 4, 1);

  spr.setTextDatum(TL_DATUM);
}
