#include "graphics_wifi_screens.h"

#include "graphics.h"

#include <WiFi.h>

#include "display.h"
#include "graphics_chrome.h"
#include "graphics_nonpet_bg.h"
#include "graphics_shared_utils.h"
#include "graphics_ui_common.h"
#include "pet.h"
#include "wifi_setup_state.h"
#include "wifi_time.h"

extern M5Canvas spr;

void drawWifiSetupScreen()
{
  if (g_wifi.setupStage == WIFI_SETUP_STAGE_SCAN)
  {
    drawNonPetTabBackground();
    drawTopBar();

    const int contentY = TOP_BAR_H;
    const int contentH = SCREEN_H - TOP_BAR_H;
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
    const int totalItems = hasResults ? (g_wifi.scanCount + 2) : 2;
    constexpr int MAX_VISIBLE = 4;
    const int itemH = 20;
    const int gap = 6;

    int start = 0;
    int visCount = 0;
    listWindow(totalItems, g_wifi.scanIndex, MAX_VISIBLE, start, visCount);

    const int totalH = visCount * itemH + (visCount - 1) * gap;
    const int startY = contentY + (contentH - totalH) / 2 + 1;

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
          snprintf(line, sizeof(line), "Scan for networks");
        else
          snprintf(line, sizeof(line), "Manual entry");
      }
      else
      {
        if (i == 0)
        {
          snprintf(line, sizeof(line), "Rescan");
        }
        else if (i <= g_wifi.scanCount)
        {
          const int realIndex = i - 1;
          snprintf(line, sizeof(line), "%s %s(%d)", g_wifi.scanSsids[realIndex],
                   g_wifi.scanOpen[realIndex] ? "[open] " : "", (int)g_wifi.scanRssi[realIndex]);
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

    spr.setTextFont(2);
    spr.setTextDatum(TL_DATUM);

    return;
  }

  const bool isPass = (g_wifi.setupStage == WIFI_SETUP_STAGE_PASS);
  ui_drawMessageWindow("WiFi Setup", isPass ? "Password:" : "SSID:", wifiSetupBuf,
                       /*maskLine2=*/isPass,
                       /*showCursor=*/true);
}

void drawWifiConnectWaitScreen()
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