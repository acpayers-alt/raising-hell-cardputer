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

#include "graphics_controls_manual_data.h"
#include "graphics_nonpet_bg.h"
#include "graphics_shared_utils.h"
#include "graphics_whats_new_data.h"

static const char *PATH_BG_SPLASH = "/raising_hell/graphics/background/flow/rh_splash.jpg";

int g_controlsHelpScroll = 0;
int g_whatsNewScroll = 0;

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

static int helpLinesMaxScroll(const HelpLine *lines, int count)
{
  const int topY = 6;
  const int bottomHelpH = 14;
  const int viewH = SCREEN_H - topY - bottomHelpH - 4;

  for (int start = 0; start < count; ++start)
  {
    int totalRemainingH = 0;

    for (int i = start; i < count; ++i)
      totalRemainingH += controlsHelpLineHeight(lines[i]);

    if (totalRemainingH <= viewH)
      return start;
  }

  return 0;
}

int controlsHelpMaxScroll() { return helpLinesMaxScroll(kControlsManual, kControlsManualCount); }

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

// -----------------------------------------------------------------------------
// Controls Helper
// -----------------------------------------------------------------------------
static void drawHelpLinesScreen(const HelpLine *lines, int count, int &scroll)
{
  const int maxScroll = helpLinesMaxScroll(lines, count);
  if (scroll > maxScroll)
    scroll = maxScroll;

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

  spr.fillRoundRect(panelX, panelY, panelW, panelH, panelRadius, TFT_BLACK);
  spr.drawRoundRect(panelX, panelY, panelW, panelH, panelRadius, TFT_DARKGREY);

  spr.setTextDatum(TL_DATUM);
  spr.setTextSize(1);

  const int titleH = 16;
  const int sectionH = 16;
  const int bodyH = 14;
  const int gapH = 8;

  int y = viewY;

  spr.setClipRect(viewX, viewY, viewW, viewH);

  for (int i = scroll; i < count; ++i)
  {
    const HelpLine &line = lines[i];

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
      break;
    case HelpLineType::SECTION:
      spr.setTextColor(TFT_YELLOW);
      break;
    case HelpLineType::BODY:
      spr.setTextColor(TFT_WHITE);
      break;
    case HelpLineType::GAP:
      break;
    }

    if (line.text)
      spr.drawString(line.text, viewX, y, 2);

    y += lineH;
  }

  spr.clearClipRect();

  spr.setTextColor(TFT_LIGHTGREY);
  if (scroll > 0)
    spr.drawString("^", panelX + panelW - 12, panelY + 4, 1);

  if (scroll < maxScroll)
    spr.drawString("v", panelX + panelW - 12, panelY + panelH - 12, 1);
}

void drawControlsHelpScreen() { drawHelpLinesScreen(kControlsManual, kControlsManualCount, g_controlsHelpScroll); }

void whatsNewResetScroll() { g_whatsNewScroll = 0; }

bool whatsNewScrollUp()
{
  if (g_whatsNewScroll <= 0)
    return false;

  --g_whatsNewScroll;
  requestUIRedraw();
  return true;
}

bool whatsNewScrollDown()
{
  const int maxScroll = helpLinesMaxScroll(kWhatsNew, kWhatsNewCount);
  if (g_whatsNewScroll >= maxScroll)
    return false;

  g_whatsNewScroll++;
  if (g_whatsNewScroll > maxScroll)
    g_whatsNewScroll = maxScroll;
  requestUIRedraw();
  return true;
}

void drawWhatsNewScreen() { drawHelpLinesScreen(kWhatsNew, kWhatsNewCount, g_whatsNewScroll); }

void drawBootWifiPromptScreen()
{
  spr.fillScreen(TFT_BLACK);
  spr.setTextDatum(TL_DATUM);
  spr.setTextFont(2);
  spr.setTextSize(1);

  spr.setTextColor(TFT_RED, TFT_BLACK);
  spr.drawString("Setup required", 10, 10);

  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.drawString("Set up WiFi for automatic", 10, 32);
  spr.drawString("time and updates.", 10, 48);

  spr.setTextColor(TFT_GREEN, TFT_BLACK);
  spr.drawString("ENTER: Set up WiFi", 10, 78);

  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.drawString("ESC: Manual Setup", 10, 94);

  spr.setTextColor(TFT_DARKGREY, TFT_BLACK);
  spr.drawString("\\: Console", 10, 116);

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
  spr.drawString("Raising Hell requires an internet ", 10, 32);
  spr.drawString("connection for initial assets", 10, 48);
  
  spr.setTextColor(TFT_GREEN, TFT_BLACK);
  spr.drawString("ENTER: Set up WiFi", 10, 72);
  
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.drawString("ESC: Manual Setup", 10, 88);
  
  spr.setTextColor(TFT_DARKGREY, TFT_BLACK);
  spr.drawString("\\: Console", 10, 110);

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

  const AssetOtaProgress &p = assetOtaGetProgress();

  const uint16_t cur = p.current;
  const uint16_t total = p.total;
  const char *stage = p.stage ? p.stage : "-";

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
    spr.drawCentreString(prog, screenW / 2, 92, 2);

    char detail[48];
    if (p.bytesTotal > 0)
      snprintf(detail, sizeof(detail), "%s  %.1f KB", stage, (double)p.bytesTotal / 1024.0);
    else
      snprintf(detail, sizeof(detail), "%s", stage);

    spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    spr.drawCentreString(detail, screenW / 2, 108, 1);

    const int barX = 20;
    const int barY = 118;
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
