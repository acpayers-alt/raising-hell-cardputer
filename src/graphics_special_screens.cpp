#include "graphics_special_screens.h"

#include "graphics.h"

#include <Arduino.h>
#include <time.h>

#include "brightness_state.h"
#include "display.h"
#include "graphics_sd_draw.h"
#include "pet.h"
#include "pet_age.h"
#include "save_manager.h"
#include "ui_death_menu.h"
#include "ui_runtime.h"

extern M5Canvas spr;
extern int screenW;
extern int screenH;
extern int deathMenuIndex;

// These already exist elsewhere in the graphics system.
bool consumeDeathScreenFadeInStart();
void forceBacklightDuringFade(uint8_t brightness);
void requestUIRedraw();
void drawCenteredLine(const char *s, int y, int font, int size);
void drawMiniGame();

static void drawDeathScreenImpl(bool redrawBg)
{
  (void)redrawBg;

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

      const uint8_t targetBrightness2 = (uint8_t)brightnessValues[brightnessLevel];
      forceBacklightDuringFade(targetBrightness2);
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

void drawBurialScreen()
{
  static const char *kBurialBg = "/raising_hell/graphics/background/flow/grave.jpg";

  spr.fillSprite(TFT_BLACK);
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
    strncpy(birthBuf, "????-??-??", sizeof(birthBuf) - 1);
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
    strncpy(deathBuf, "????-??-??", sizeof(deathBuf) - 1);
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

void drawMiniGameScreen()
{
  drawMiniGame();
}

void drawDeathScreen()
{
  drawDeathScreenImpl(true);
}