#include "wifi_store.h"

#include <Preferences.h>

static const char *NS = "rh_wifi";

static const char *K_LEGACY_SSID = "ssid";
static const char *K_LEGACY_PASS = "pass";

static void profileKeys(int index, char *ssidKey, size_t ssidKeyLen, char *passKey, size_t passKeyLen)
{
  snprintf(ssidKey, ssidKeyLen, "ssid%d", index);
  snprintf(passKey, passKeyLen, "pass%d", index);
}

static bool readProfile(Preferences &p, int index, String &ssid, String &pass)
{
  ssid = "";
  pass = "";

  if (index < 0 || index >= WIFI_PROFILE_MAX)
    return false;

  char ssidKey[8];
  char passKey[8];
  profileKeys(index, ssidKey, sizeof(ssidKey), passKey, sizeof(passKey));

  ssid = p.getString(ssidKey, "");
  pass = p.getString(passKey, "");

  return ssid.length() > 0;
}

static void writeProfile(Preferences &p, int index, const String &ssid, const String &pass)
{
  if (index < 0 || index >= WIFI_PROFILE_MAX)
    return;

  char ssidKey[8];
  char passKey[8];
  profileKeys(index, ssidKey, sizeof(ssidKey), passKey, sizeof(passKey));

  if (ssid.length() == 0)
  {
    p.remove(ssidKey);
    p.remove(passKey);
    return;
  }

  p.putString(ssidKey, ssid);
  p.putString(passKey, pass);
}

static bool loadLegacy(Preferences &p, String &ssid, String &pass)
{
  ssid = p.getString(K_LEGACY_SSID, "");
  pass = p.getString(K_LEGACY_PASS, "");
  return ssid.length() > 0;
}

bool wifiStoreLoadProfile(int index, String &ssid, String &pass)
{
  ssid = "";
  pass = "";

  Preferences p;
  if (!p.begin(NS, true))
  {
    Serial.println("[WIFI STORE] load profile: namespace missing or prefs open failed");
    return false;
  }

  const bool ok = readProfile(p, index, ssid, pass);

  p.end();

  Serial.printf("[WIFI STORE] load profile=%d ok=%d ssid='%s' passLen=%u\n",
                index,
                ok ? 1 : 0,
                ok ? ssid.c_str() : "",
                ok ? (unsigned)pass.length() : 0);

  return ok;
}

bool wifiStoreLoad(String &ssid, String &pass)
{
  ssid = "";
  pass = "";

  Preferences p;
  if (!p.begin(NS, true))
  {
    Serial.println("[WIFI STORE] load: namespace missing or prefs open failed");
    return false;
  }

  bool ok = readProfile(p, 0, ssid, pass);

  if (!ok)
    ok = loadLegacy(p, ssid, pass);

  p.end();

  Serial.printf("[WIFI STORE] load ok=%d ssid='%s' passLen=%u\n",
                ok ? 1 : 0,
                ok ? ssid.c_str() : "",
                ok ? (unsigned)pass.length() : 0);

  return ok;
}

int wifiStoreCount()
{
  Preferences p;
  if (!p.begin(NS, true))
    return 0;

  int count = 0;

  for (int i = 0; i < WIFI_PROFILE_MAX; ++i)
  {
    String ssid;
    String pass;
    if (readProfile(p, i, ssid, pass))
      ++count;
  }

  if (count == 0)
  {
    String legacySsid;
    String legacyPass;
    if (loadLegacy(p, legacySsid, legacyPass))
      count = 1;
  }

  p.end();
  return count;
}

bool wifiStoreHasCreds()
{
  return wifiStoreCount() > 0;
}

void wifiStoreSave(const String &ssid, const String &pass)
{
  if (ssid.length() == 0)
    return;

  Preferences p;
  if (!p.begin(NS, false))
    return;

  String ssids[WIFI_PROFILE_MAX];
  String passes[WIFI_PROFILE_MAX];

  for (int i = 0; i < WIFI_PROFILE_MAX; ++i)
    readProfile(p, i, ssids[i], passes[i]);

  if (ssids[0].length() == 0)
  {
    String legacySsid;
    String legacyPass;
    if (loadLegacy(p, legacySsid, legacyPass))
    {
      ssids[0] = legacySsid;
      passes[0] = legacyPass;
    }
  }

  String newSsids[WIFI_PROFILE_MAX];
  String newPasses[WIFI_PROFILE_MAX];

  newSsids[0] = ssid;
  newPasses[0] = pass;

  int out = 1;
  for (int i = 0; i < WIFI_PROFILE_MAX && out < WIFI_PROFILE_MAX; ++i)
  {
    if (ssids[i].length() == 0)
      continue;

    if (ssids[i] == ssid)
      continue;

    newSsids[out] = ssids[i];
    newPasses[out] = passes[i];
    ++out;
  }

  for (int i = 0; i < WIFI_PROFILE_MAX; ++i)
    writeProfile(p, i, newSsids[i], newPasses[i]);

  p.putString(K_LEGACY_SSID, newSsids[0]);
  p.putString(K_LEGACY_PASS, newPasses[0]);

  p.end();

  Serial.printf("[WIFI STORE] saved profile ssid='%s' profiles=%d\n",
                ssid.c_str(),
                wifiStoreCount());
}

void wifiStoreClear()
{
  Preferences p;
  if (!p.begin(NS, false))
    return;

  for (int i = 0; i < WIFI_PROFILE_MAX; ++i)
    writeProfile(p, i, "", "");

  p.remove(K_LEGACY_SSID);
  p.remove(K_LEGACY_PASS);

  p.end();

  Serial.println("[WIFI STORE] cleared all wifi profiles");
}