#include "wifi_power.h"

#include "support_logging_state.h"
#include "wifi_store.h"
#include "wifi_time.h"
#include <WiFi.h>

void applyWifiPower(bool enable)
{
  if (supportLoggingEnabled())
    Serial.printf("[WIFI POWER] enable=%d mode=%d status=%d\n", enable ? 1 : 0, (WiFi.getMode() == WIFI_OFF ? 0 : 1),
                  (int)WiFi.status());

  if (!enable)
  {
    if (supportLoggingEnabled())
      Serial.println("[WIFI POWER] disabling radio");
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
    return;
  }

  // Enable STA mode and keep Wi-Fi logically ON even if we do not yet
  // have stored credentials.
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);

  if (WiFi.status() == WL_CONNECTED)
{
  if (supportLoggingEnabled())
    Serial.println("[WIFI POWER] already connected; leaving current association alone");
  return;
}

  String ssid, pass;
  bool found = false;

  for (int i = 0; i < WIFI_PROFILE_MAX; ++i)
  {
    if (!wifiStoreLoadProfile(i, ssid, pass) || ssid.length() == 0)
      continue;

    if (supportLoggingEnabled())
      Serial.printf("[WIFI POWER] begin managed connect profile=%d ssid='%s'\n", i, ssid.c_str());

    wifiConsoleBeginConnect(ssid.c_str(), pass.c_str());
    found = true;
    break;
  }

  if (!found)
  {
    if (supportLoggingEnabled())
      Serial.println("[WIFI POWER] no stored creds; Wi-Fi remains enabled but idle");
    // Do NOT disconnect or turn Wi-Fi back off here.
    // Leave STA mode up so manual enable actually stays enabled.
  }
}

void wifiResetSettings()
{
  // Clear all stored Wi-Fi profiles, including legacy creds.
  wifiStoreClear();

  // Disconnect from current network
  WiFi.disconnect(true, true);
  delay(50);
}
