#include "wifi_store.h"

#include <Preferences.h>

static const char* NS     = "rh_wifi";
static const char* K_SSID = "ssid";
static const char* K_PASS = "pass";

bool wifiStoreHasCreds() {
  Preferences p;
  if (!p.begin(NS, true)) return false;
  String s = p.getString(K_SSID, "");
  p.end();
  return s.length() > 0;
}

bool wifiStoreLoad(String& ssid, String& pass)
{
  ssid = "";
  pass = "";

  Preferences p;

  // Read-only open can fail if the namespace does not exist yet.
  // Treat that as "no stored creds", not as a fatal Wi-Fi error.
  if (!p.begin(NS, true))
  {
    Serial.println("[WIFI STORE] load: namespace missing or prefs open failed");
    return false;
  }

  ssid = p.getString(K_SSID, "");
  pass = p.getString(K_PASS, "");
  p.end();

  const bool ok = ssid.length() > 0;

  Serial.printf("[WIFI STORE] load ok=%d ssid='%s' passLen=%u\n",
                ok ? 1 : 0,
                ok ? ssid.c_str() : "",
                ok ? (unsigned)pass.length() : 0);

  return ok;
}

void wifiStoreSave(const String& ssid, const String& pass) {
  Preferences p;
  if (!p.begin(NS, false)) return;
  p.putString(K_SSID, ssid);
  p.putString(K_PASS, pass);
  p.end();
}

void wifiStoreClear() {
  Preferences p;
  if (!p.begin(NS, false)) return;
  p.remove(K_SSID);
  p.remove(K_PASS);
  p.end();
}
