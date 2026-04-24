#include "wifi_power.h"

#include "wifi_power.h"
#include "wifi_store.h"
#include "wifi_time.h"
#include <Preferences.h>
#include <WiFi.h>

void applyWifiPower(bool enable)
{
  Serial.printf("[WIFI POWER] enable=%d mode=%d status=%d\n", enable ? 1 : 0, (WiFi.getMode() == WIFI_OFF ? 0 : 1),
                (int)WiFi.status());

  if (!enable)
  {
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

  String ssid, pass;
  if (wifiStoreLoad(ssid, pass) && ssid.length() > 0)
  {
    Serial.printf("[WIFI POWER] begin managed connect ssid='%s'\n", ssid.c_str());
    wifiConsoleBeginConnect(ssid.c_str(), pass.c_str());
  }
  else
  {
    Serial.println("[WIFI POWER] no stored creds; Wi-Fi remains enabled but idle");
    // Do NOT disconnect or turn Wi-Fi back off here.
    // Leave STA mode up so manual enable actually stays enabled.
  }
}

void wifiResetSettings()
{
  // Clear stored creds (matches your console code namespace/keys)
  Preferences prefs;
  prefs.begin("rh_wifi", false);
  prefs.putString("ssid", "");
  prefs.putString("pass", "");
  prefs.end();

  // Disconnect from current network
  WiFi.disconnect(true, true);
  delay(50);
}
