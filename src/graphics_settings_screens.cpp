#include "graphics_settings_screens.h"

#include "graphics.h"

#include <Arduino.h>
#include <cstring>
#include <time.h>

#include "app_state.h"
#include "asset_ota.h"
#include "asset_ota_config.h"
#include "auto_screen.h"
#include "brightness_state.h"
#include "build_flags.h"
#include "console.h"
#include "display.h"
#include "factory_reset_state.h"
#include "flow_factory_reset.h"
#include "game_options_state.h"
#include "graphics_chrome.h"
#include "graphics_nonpet_bg.h"
#include "graphics_shared_utils.h"
#include "graphics_ui_common.h"
#include "motion.h"
#include "pet.h"
#include "save_manager.h"
#include "sdcard.h"
#include "settings_flow_state.h"
#include "sound.h"
#include "system_status_state.h"
#include "time_state.h"
#include "timezone.h"
#include "ui_settings_menu.h"
#include "ui_settings_pages.h"
#include "user_toggles_state.h"
#include "version.h"
#include "wifi_setup_state.h"
#include "wifi_time.h"

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

static void drawFactoryResetConfirmOverlay()
{
  spr.fillRect(0, 0, screenW, screenH, TFT_BLACK);

  const uint16_t modalOutline = uiModalOutline(pet.type);

  const int pad = 10;
  const int boxW = screenW - (pad * 2);
  const int boxH = 116;
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
  spr.drawString("Wipe your live pet and settings?", screenW / 2, y + 29);

  spr.setTextColor(TFT_YELLOW, TFT_BLACK);
  spr.drawString("HOLD ENTER to confirm", screenW / 2, y + 43);

  const int pillY = y + 58;
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

  const int barW = 96;
  const int barH = 5;
  const int barX = (screenW - barW) / 2;
  const int barY = pillY + pillH + 5;

  const uint8_t progress = factoryResetHoldProgress255();
  const int fillW = (barW * progress) / 255;

  spr.drawRoundRect(barX, barY, barW, barH, 2, yesSel ? uiPillOutline(pet.type) : TFT_DARKGREY);
  if (fillW > 0)
    spr.fillRoundRect(barX + 1, barY + 1, fillW - 2 > 0 ? fillW - 2 : 1, barH - 2, 2, TFT_YELLOW);

  spr.setTextFont(1);
  spr.setTextSize(1);
  spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  spr.setTextDatum(BC_DATUM);
  spr.drawString("ESC: Cancel", screenW / 2, y + boxH - 6);
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
      "Manual",    nullptr,           "Pet Options >", "Screen Settings >", "System Settings >", "Game Options >",
      "Console >", "System Status >", "Credits",       "Store Pet",         "Main Menu",
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
    const int y = startY + row * (itemH + gap);
    const bool sel = (i == g_app.settingsIndex);

    const uint16_t outline = sel ? uiPillOutline(pet.type) : TFT_DARKGREY;
    const uint16_t fill = sel ? uiPillFillSelected(pet.type) : TFT_BLACK;
    const uint16_t textCol = sel ? TFT_WHITE : TFT_LIGHTGREY;

    spr.fillRoundRect(boxX, y, boxW, itemH, radius, fill);
    spr.drawRoundRect(boxX, y, boxW, itemH, radius, outline);

    const int ty = y + (itemH - spr.fontHeight()) / 2;
    const char *label = labelsStatic[i];
    if (i == 1)
      label = volumeLine;

    spr.setTextColor(textCol, fill);
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

static void drawPetSettingsMenu()
{
  drawNonPetTabBackground();
  drawTopBar();

  const int contentY = TOP_BAR_H;
  const int contentH = SCREEN_H - TOP_BAR_H;

  const char *labels[] = {"Rename Pet", "Backup Current Pet", "Restore From Backup", "New Pet"};
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

    const int ty = y + (itemH - spr.fontHeight()) / 2;
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
    const bool sel = (i == g_app.gameOptionsIndex);

    const uint16_t outline = sel ? uiPillOutline(pet.type) : TFT_DARKGREY;
    const uint16_t fill = sel ? uiPillFillSelected(pet.type) : TFT_BLACK;
    const uint16_t textCol = sel ? TFT_WHITE : TFT_LIGHTGREY;

    spr.fillRoundRect(boxX, y, boxW, itemH, radius, fill);
    spr.drawRoundRect(boxX, y, boxW, itemH, radius, outline);

    const int ty = y + (itemH - spr.fontHeight()) / 2;
    spr.setTextColor(textCol, fill);
    spr.drawString(labels[i], boxX + 10, ty);
  }
}

static void drawAutoScreenPickerMenu()
{
  drawTopBar();

  const int contentY = TOP_BAR_H;
  const int footerH = 14;
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

  const int bottomPad = 6;
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

    const int ty = y + (itemH - spr.fontHeight()) / 2;
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

  const int itemH = 22;
  const int gap = 6;

  const int listTopY = contentY + 26;
  const int listAreaH = contentH - 26;

  const int totalH = visCount * itemH + (visCount - 1) * gap;
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

    const int ty = y + (itemH - spr.fontHeight()) / 2;
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

  char bLine[28];
  snprintf(bLine, sizeof(bLine), "Brightness: %s", brightnessToText(brightnessLevel));

  char aLine[28];
  snprintf(aLine, sizeof(aLine), "Auto Screen: %s", autoScreenToText((uint8_t)autoScreenTimeoutSel));

  char cLine[28];
  snprintf(cLine, sizeof(cLine), "Auto Clock: %s", autoClockToText((uint8_t)autoClockTimeoutSel));

  char shakeLine[32];
  snprintf(shakeLine, sizeof(shakeLine), "Shake to Wake: %s",
           motionShakeSensitivityToText(motionGetShakeSensitivity()));

  const char *labels[] = {
      bLine,
      aLine,
      cLine,
      shakeLine,
  };

  const int totalItems = 4;

  g_app.screenSettingsIndex = clampi(g_app.screenSettingsIndex, 0, totalItems - 1);

  constexpr int MAX_VISIBLE = 4;
  int start = 0, visCount = 0;
  listWindow(totalItems, g_app.screenSettingsIndex, MAX_VISIBLE, start, visCount);

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
    const bool sel = (i == g_app.screenSettingsIndex);

    const uint16_t outline = sel ? uiPillOutline(pet.type) : TFT_DARKGREY;
    const uint16_t fill = sel ? uiPillFillSelected(pet.type) : TFT_BLACK;
    const uint16_t textCol = sel ? TFT_WHITE : TFT_LIGHTGREY;

    spr.fillRoundRect(boxX, y, boxW, itemH, radius, fill);
    spr.drawRoundRect(boxX, y, boxW, itemH, radius, outline);

    const int ty = y + (itemH - spr.fontHeight()) / 2;
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

  const int totalItems = UiSettingsMenu::WifiItemCount();
  g_wifi.wifiSettingsIndex = clampi(g_wifi.wifiSettingsIndex, 0, totalItems - 1);

  constexpr int MAX_VISIBLE = 4;
  int start = 0, visCount = 0;
  listWindow(totalItems, g_wifi.wifiSettingsIndex, MAX_VISIBLE, start, visCount);

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
    const bool sel = (i == g_wifi.wifiSettingsIndex);

    const uint16_t outline = sel ? uiPillOutline(pet.type) : TFT_DARKGREY;
    const uint16_t fill = sel ? uiPillFillSelected(pet.type) : TFT_BLACK;
    const uint16_t textCol = sel ? TFT_WHITE : TFT_LIGHTGREY;

    spr.fillRoundRect(boxX, y, boxW, itemH, radius, fill);
    spr.drawRoundRect(boxX, y, boxW, itemH, radius, outline);

    const int ty = y + (itemH - spr.fontHeight()) / 2;

    const char *label = UiSettingsMenu::WifiItemLabel(i);
    char valueBuf[64];
    valueBuf[0] = '\0';

    if (strcmp(label, "WiFi") == 0)
    {
      snprintf(valueBuf, sizeof(valueBuf), "WiFi: %s", wifiIsEnabled() ? "ON" : "OFF");
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

static void drawSystemSettingsMenu()
{
  drawNonPetTabBackground();
  drawTopBar();

  const int contentY = TOP_BAR_H;
  const int contentH = SCREEN_H - TOP_BAR_H;

  const char *labels[] = {"Set Time", "Time Zone", "Factory Reset", "WiFi Settings >"};
  const int totalItems = 4;

  g_app.systemSettingsIndex = clampi(g_app.systemSettingsIndex, 0, totalItems - 1);

  constexpr int MAX_VISIBLE = 4;
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

    const int ty = y + (itemH - spr.fontHeight()) / 2;

    const char *label = labels[i];
    char valueBuf[64];
    valueBuf[0] = '\0';

    if (strcmp(label, "Time Zone") == 0)
    {
      snprintf(valueBuf, sizeof(valueBuf), "Time Zone: %s", tzName((uint8_t)tzIndex));
      label = valueBuf;
    }

    spr.setTextColor(textCol, fill);
    spr.drawString(label, boxX + 10, ty);
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

static void drawCreditsScreen()
{
  if (!isScreenOn())
    return;

  spr.clearClipRect();

  static const char *PATH_BG_SPLASH = "/raising_hell/graphics/background/flow/rh_splash.jpg";

  bool ok = false;
  if (g_sdReady)
    ok = sprDrawJpgFromSD(PATH_BG_SPLASH, 0, 0);
  if (!ok)
    spr.fillSprite(TFT_BLACK);

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

  const int panelPadX = 12;
  const int panelPadY = 2;
  const int panelX = panelPadX;
  const int panelY = yCreated - panelPadY;
  const int panelW = SCREEN_W - (panelPadX * 2);
  const int panelH = (yAssets - yCreated) + TIGHT_H + (panelPadY * 2);

  for (int yy = panelY; yy < panelY + panelH; ++yy)
  {
    const int xStart = panelX + ((yy & 1) ? 1 : 0);
    for (int xx = xStart; xx < panelX + panelW; xx += 2)
    {
      spr.drawPixel(xx, yy, TFT_BLACK);
    }
  }

  spr.setTextColor(TFT_WHITE);
  spr.drawString("Created By:", SCREEN_W / 2, yCreated);
  spr.drawString("Aaron & Finley Ayers", SCREEN_W / 2, yAaron);

  uint16_t versionCol = TFT_DARKGREY;
#if defined(PUBLIC_BUILD) && PUBLIC_BUILD
  versionCol = TFT_GREEN;
#else
  versionCol = TFT_RED;
#endif

spr.setTextColor(versionCol);

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
  const int lineH = 10;
  const int visibleLines = 9;

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

  const char *lines[] = {
      "BUILD",       buildBuf,
      "SAVE VER",    saveVerBuf,
      "UPTIME",      uptimeBuf,
      "BATTERY",     batteryBuf,
      "WIFI",        wifiStateBuf,
      "SSID",        (ssid && ssid[0]) ? ssid : "(none)",
      "IP",          (ip && ip[0]) ? ip : "(none)",
      "ASSET OTA",   assetBuf,
      "OTA CH",      otaChannelBuf,
      "MANIFEST",    basenameFromUrl(manifestUrl),
      "LOCAL MAN",   localManifestBuf,
      "FREE HEAP",   heapFreeBuf,
      "LARGEST BLK", heapLargestBuf,
      "PSRAM SIZE",  psramSizeBuf,
      "PSRAM FREE",  psramFreeBuf,
  };

  // Clamp AND store back into state
  const int totalLines = (int)(sizeof(lines) / sizeof(lines[0])) / 2;

  if (totalLines <= visibleLines)
  {
    g_systemStatus.scrollOffset = 0;
  }
  else
  {
    g_systemStatus.scrollOffset = clampi(g_systemStatus.scrollOffset, 0, totalLines - visibleLines);
  }

  const int start = g_systemStatus.scrollOffset;

  spr.fillRect(0, contentY, SCREEN_W, SCREEN_H - contentY, TFT_BLACK);

  spr.setTextDatum(TL_DATUM);
  spr.setTextFont(1);
  spr.setTextSize(1);

  const int leftX = 6;
  const int valX = 90;
  int y = contentY + 4;

  for (int pairIdx = start; pairIdx < totalLines && pairIdx < start + visibleLines; pairIdx++)
  {
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

  if (assetOtaConfirmActive())
  {
    drawAssetOtaConfirmOverlay();
  }
}