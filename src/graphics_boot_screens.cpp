#include "graphics_boot_screens.h"
#include "graphics.h"

#include "app_state.h"
#include "asset_ota.h"
#include "boot_pipeline.h"
#include "console.h"
#include "display.h"
#include "flow_boot_wifi.h"
#include "sdcard.h"
#include "sound.h"
#include "timezone.h"
#include "ui_runtime.h"
#include "wifi_time.h"

#include <Arduino.h>
#include <WiFi.h>

#include "graphics_shared_utils.h"
#include "graphics_nonpet_bg.h"

static const char *PATH_BG_SPLASH = "/raising_hell/graphics/background/flow/rh_splash.jpg";

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
    {HelpLineType::BODY, "ESC       Open Settings / cancel"},
    {HelpLineType::BODY, "Z-M         Jump to tab"},
    {HelpLineType::BODY, "GO          Toggle screen on/off"},
    {HelpLineType::BODY, "Shake       Wake screen"},
    {HelpLineType::BODY, "\\           Console"},
    {HelpLineType::GAP, nullptr},
    {HelpLineType::BODY, "Shake the device to wake screen"},
    {HelpLineType::GAP, nullptr},

    {HelpLineType::SECTION, "Alt Navigation Clusters"},
    {HelpLineType::BODY, "(E A S D) and (O J K L)"},
    {HelpLineType::GAP, nullptr},

    {HelpLineType::SECTION, "Congratulations!!"},
    {HelpLineType::BODY, "You summoned something from"},
    {HelpLineType::BODY, "beyond our mortal plane."},
    {HelpLineType::BODY, "Now it's your problem!!"},
    {HelpLineType::GAP, nullptr},

    {HelpLineType::SECTION, "Care for Your Pet"},
    {HelpLineType::BODY, "Your Pet needs care!"},
    {HelpLineType::BODY, "Take this very seriously"},
    {HelpLineType::BODY, "You have been warned!"},
    {HelpLineType::BODY, "If your pet dies, nothing short"},
    {HelpLineType::BODY, "of apocalypse will bring it"},
    {HelpLineType::BODY, "back to life."},
    {HelpLineType::GAP, nullptr},

    {HelpLineType::SECTION, "Stats"},
    {HelpLineType::BODY, "Keep the bars full, or else!"},
    {HelpLineType::BODY, "Feed when hunger bar is low"},
    {HelpLineType::BODY, "Play when mood bar is low"},
    {HelpLineType::BODY, "Sleep when rest bar is low"},
    {HelpLineType::BODY, "If your pet is hungry for a"},
    {HelpLineType::BODY, "while, it will start to lose HP"},
    {HelpLineType::BODY, "soon it will die."},
    {HelpLineType::GAP, nullptr},

    {HelpLineType::SECTION, "Alerts"},
    {HelpLineType::BODY, "Red - Pet is in danger"},
    {HelpLineType::BODY, "Yellow - Pet needs attention"},
    {HelpLineType::BODY, "Blue - Pet is tired or sleeping"},
    {HelpLineType::BODY, "Green - Pet is doing well"},
    {HelpLineType::BODY, "Purple - Pet is amazing"},
    {HelpLineType::BODY, "If the screen is off, alerts"},
    {HelpLineType::BODY, "will briefly light it up."},
    {HelpLineType::BODY, "Heed these warnings or else!"},
    {HelpLineType::GAP, nullptr},

    {HelpLineType::SECTION, "Death and Resurrection"},
    {HelpLineType::BODY, "Death is not the end for your"},
    {HelpLineType::BODY, "creature from beyond. Rip"},
    {HelpLineType::BODY, "your companion from the jaws of"},
    {HelpLineType::BODY, "death by completing a ritual"},
    {HelpLineType::BODY, "or commend it to the dirt and"},
    {HelpLineType::BODY, "face the consequences!"},
    {HelpLineType::GAP, nullptr},

    {HelpLineType::SECTION, "Mini Games"},
    {HelpLineType::BODY, "Play mini games to earn cash"},
    {HelpLineType::BODY, "XP, and valuable prizes. Keep"},
    {HelpLineType::BODY, "your pet amused, keep it happy."},
    {HelpLineType::BODY, "Else your world is forfeit."},
    {HelpLineType::BODY, "Your otherworldly ward will"},
    {HelpLineType::BODY, "Soon grow tired of these games"},
    {HelpLineType::GAP, nullptr},

    {HelpLineType::SECTION, "Sleep"},
    {HelpLineType::BODY, "Even the spawm of evil needs"},
    {HelpLineType::BODY, "sleep. When your pet is tired"},
    {HelpLineType::BODY, "it won't play any games."},
    {HelpLineType::BODY, "Tuck your beast into bed to"},
    {HelpLineType::BODY, "recover energy, or use one of"},
    {HelpLineType::BODY, "the cursed items in the shop!"},
    {HelpLineType::GAP, nullptr},

    {HelpLineType::SECTION, "Currency and Items"},
    {HelpLineType::BODY, "Inferium is the currency of"},
    {HelpLineType::BODY, "the underworld. It can be used"},
    {HelpLineType::BODY, "to buy food, cursed items, and"},
    {HelpLineType::BODY, "other mysterious artifacts"},
    {HelpLineType::GAP, nullptr},

    {HelpLineType::SECTION, "XP, Levels, and Evolution"},
    {HelpLineType::BODY, "Play Mini Games to Earn XP."},
    {HelpLineType::BODY, "XP will make your pet level up!"},
    {HelpLineType::BODY, "Level up enough, and your pet"},
    {HelpLineType::BODY, "can evolve! Who knows what"},
    {HelpLineType::BODY, "abominations you will nurture!"},
    {HelpLineType::GAP, nullptr},

    {HelpLineType::SECTION, "Multiple Pets!"},
    {HelpLineType::BODY, "If one pet is not enough, you"},
    {HelpLineType::BODY, "can have all the pets you want!"},
    {HelpLineType::BODY, "Simply store the pet in the menu."},
    {HelpLineType::BODY, "You are then free to hatch a new"},
    {HelpLineType::BODY, "creature. You can recall stored"},
    {HelpLineType::BODY, "pets at any time."},
    {HelpLineType::GAP, nullptr},

    {HelpLineType::SECTION, "Clock Mode!"},
    {HelpLineType::BODY, "Time is ticking, the end is near."},
    {HelpLineType::BODY, "You cannot stop it, but you can"},
    {HelpLineType::BODY, "watch it happen. Use clock mode!"},
    {HelpLineType::BODY, "available in the settings menu"},
    {HelpLineType::GAP, nullptr},

    {HelpLineType::SECTION, "Raising Hell"},
    {HelpLineType::BODY, "Made by: Aaron Ayers"},
    {HelpLineType::BODY, "Written by: Finley Ayers"},
    {HelpLineType::BODY, "Beta Testing by: Lincoln Ayers"},
    {HelpLineType::BODY, "Patience by: Nicci Ayers"},
    {HelpLineType::GAP, nullptr},

};

constexpr int kControlsManualCount = (int)(sizeof(kControlsManual) / sizeof(kControlsManual[0]));

int g_controlsHelpScroll = 0;

int controlsHelpLineHeight(const HelpLine &line)
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

int controlsHelpMaxScroll()
{
  const int topY = 6;
  const int bottomHelpH = 14;
  const int viewH = SCREEN_H - topY - bottomHelpH - 4;

  // Find the earliest line index such that everything from that line
  // to the end fits in the viewport. That is the true last scroll position.
  for (int start = 0; start < kControlsManualCount; ++start)
  {
    int totalRemainingH = 0;

    for (int i = start; i < kControlsManualCount; ++i)
      totalRemainingH += controlsHelpLineHeight(kControlsManual[i]);

    if (totalRemainingH <= viewH)
      return start;
  }

  return 0;
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

  g_controlsHelpScroll++;
  if (g_controlsHelpScroll > maxScroll)
    g_controlsHelpScroll = maxScroll;
  requestUIRedraw();
  return true;
}

static int getControlsManualTotalHeight()
{
  int y = 0;

  for (size_t i = 0; i < sizeof(kControlsManual) / sizeof(kControlsManual[0]); i++)
  {
    switch (kControlsManual[i].type)
    {
    case HelpLineType::TITLE:
      y += 20;
      break;
    case HelpLineType::SECTION:
      y += 18;
      break;
    case HelpLineType::BODY:
      y += 14;
      break;
    case HelpLineType::GAP:
      y += 8;
      break;
    }
  }

  return y;
}

// -----------------------------------------------------------------------------
// Controls Helper
// -----------------------------------------------------------------------------
void drawControlsHelpScreen()
{
  const int maxScroll = controlsHelpMaxScroll();
  if (g_controlsHelpScroll > maxScroll)
    g_controlsHelpScroll = maxScroll;

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

  if (g_controlsHelpScroll < controlsHelpMaxScroll())
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
void drawBootWifiImportedScreen()
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
    spr.drawString("Charging...", 10, 122);
    spr.drawString("Current battery shown above.", 10, 140);
  }
  else
  {
    spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    spr.drawString("Plug in USB to charge.", 10, 122);
    spr.drawString("Current battery shown above.", 10, 140);
  }

  spr.pushSprite(0, 0);
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
