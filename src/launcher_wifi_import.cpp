#include "launcher_wifi_import.h"

#include <ArduinoJson.h>
#include <FS.h>
#include <SD.h>
#include <memory>

#include "sdcard.h"

#include <WiFi.h>

bool launcherImportWifiCreds(String &outSsid, String &outPwd)
{
  outSsid = "";
  outPwd = "";

  if (!g_sdReady)
    return false;

  const char *usePath = nullptr;
  if (SD.exists("/config.conf"))
    usePath = "/config.conf";
  else if (SD.exists("config.conf"))
    usePath = "config.conf";
  else
  {
    Serial.println("[WIFI] launcher config not found");
    return false;
  }

  Serial.printf("[WIFI] using launcher config path: %s\n", usePath);

  File f = SD.open(usePath, "r");
  if (!f)
    return false;

  const size_t len = f.size();
  Serial.printf("[WIFI] launcher config size: %u\n", (unsigned)len);

  if (len == 0 || len > 8192)
  {
    Serial.println("[WIFI] launcher config size invalid");
    f.close();
    return false;
  }

  std::unique_ptr<char[]> buf(new char[len + 1]);
  if (!buf)
  {
    f.close();
    return false;
  }

  const size_t n = f.readBytes(buf.get(), len);
  f.close();
  buf[n] = '\0';

  DynamicJsonDocument doc(8192);
  DeserializationError err = deserializeJson(doc, buf.get());
  if (err)
  {
    Serial.printf("[WIFI] launcher config parse failed: %s\n", err.c_str());
    return false;
  }

  if (!doc.is<JsonArray>())
  {
    Serial.println("[WIFI] launcher config root is not an array");
    return false;
  }

  JsonArray root = doc.as<JsonArray>();
  if (root.size() == 0)
  {
    Serial.println("[WIFI] launcher config root array is empty");
    return false;
  }

  JsonObject rootObj = root[0].as<JsonObject>();
  if (rootObj.isNull())
  {
    Serial.println("[WIFI] launcher config first object is null");
    return false;
  }

  JsonArray wifiArr = rootObj["wifi"].as<JsonArray>();
  if (wifiArr.isNull() || wifiArr.size() == 0)
  {
    Serial.println("[WIFI] launcher wifi array missing or empty");
    return false;
  }

  Serial.printf("[WIFI] launcher wifi entries: %u\n", (unsigned)wifiArr.size());

  for (JsonVariant v : wifiArr)
  {
    JsonObject ap = v.as<JsonObject>();
    if (ap.isNull())
      continue;

      const char *ssid =
          ap["ssid"] |
          ap["SSID"] |
          "";

      const char *pwd =
          ap["pwd"] |
          ap["password"] |
          ap["pass"] |
          ap["psk"] |
          "";

      Serial.printf("[WIFI] launcher entry ssid='%s' pwd_len=%u\n",
                    ssid,
                    (unsigned)strlen(pwd));

      if (!ssid[0] || !pwd[0])
      {
        Serial.println("[WIFI] skipping launcher wifi entry with missing ssid/pwd");
        continue;
      }

      const bool ssidPlaceholder =
          (strcmp(ssid, "myNetSSID") == 0) ||
          (strcmp(ssid, "YOUR_WIFI_SSID") == 0);

      const bool pwdPlaceholder =
          (strcmp(pwd, "myNetPassword") == 0) ||
          (strcmp(pwd, "YOUR_WIFI_PASSWORD") == 0);

      if (ssidPlaceholder || pwdPlaceholder)
      {
        Serial.println("[WIFI] skipping launcher placeholder wifi entry");
        continue;
      }

      outSsid = ssid;
      outPwd = pwd;
      Serial.printf("[WIFI] launcher creds found for SSID: %s\n", ssid);
      return true;
      }
      
  Serial.println("[WIFI] launcher wifi array had no valid ssid/pwd entries");
  return false;
}

bool launcherWifiSsidVisible(const char *ssid)
{
  if (!ssid || !ssid[0])
    return false;

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.disconnect(false, false);
  WiFi.scanDelete();
  delay(150);

  Serial.printf("[WIFI] scan for imported SSID '%s'\n", ssid);

  const int n = WiFi.scanNetworks(false, true);

  bool found = false;

  if (n > 0)
  {
    for (int i = 0; i < n; ++i)
    {
      String seen = WiFi.SSID(i);
      if (seen == ssid)
      {
        found = true;
        break;
      }
    }
  }

  WiFi.scanDelete();

  Serial.printf("[WIFI] imported SSID visible=%d n=%d ssid='%s'\n", found ? 1 : 0, n, ssid);
  return found;
}