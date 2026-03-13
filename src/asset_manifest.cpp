#include "asset_manifest.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <SD.h>
#include <WiFiClientSecure.h>

#include "asset_ota_config.h"
#include "graphics.h"
#include "sdcard.h"

static bool parseManifestJson(const String &json, AssetManifestData *out)
{
  if (!out)
    return false;

  out->clear();

  DynamicJsonDocument doc((size_t)4096 + json.length() * 2);
  DeserializationError err = deserializeJson(doc, json);
  if (err)
    return false;

  JsonObject root = doc.as<JsonObject>();
  if (root.isNull())
    return false;

  out->packVersion = String((const char *)(root["packVersion"] | ""));
  out->channel = String((const char *)(root["channel"] | ""));
  Serial.printf("[OTA] parsed packVersion='%s'\n", out->packVersion.c_str());
  
  JsonArray files = root["files"].as<JsonArray>();
  if (files.isNull())
    return false;

  out->files.reserve(files.size());

  for (JsonVariant v : files)
  {
    JsonObject obj = v.as<JsonObject>();
    if (obj.isNull())
      continue;

    String rel;
    if (!assetManifestNormalizePath(String((const char *)(obj["path"] | "")), &rel))
      return false;

    AssetManifestFile f;
    f.path = rel;
    f.url = String((const char *)(obj["url"] | ""));
    f.sha256 = String((const char *)(obj["sha256"] | ""));
    f.sha256.toLowerCase();
    f.size = (uint32_t)(obj["size"] | 0UL);

    if (f.path.isEmpty() || f.url.isEmpty() || f.sha256.isEmpty() || f.size == 0)
      return false;

    out->files.push_back(f);
  }

  return true;
}

bool assetManifestNormalizePath(const String &inPath, String *outRelPath)
{
  if (!outRelPath)
    return false;

  String p = inPath;
  p.trim();

  while (p.startsWith("/"))
    p.remove(0, 1);

  if (p.isEmpty())
    return false;
  if (p.indexOf('\\') >= 0)
    return false;
  if (p == "..")
    return false;
  if (p.indexOf("../") >= 0)
    return false;
  if (p.indexOf("/..") >= 0)
    return false;
  if (p.endsWith("/"))
    return false;

  *outRelPath = p;
  return true;
}

bool assetManifestLoadLocal(AssetManifestData *out)
{
  if (!out)
    return false;
  if (!g_sdReady)
    return false;
  if (!SD.exists(assetOtaLocalManifestPath()))
    return false;

  File f = SD.open(assetOtaLocalManifestPath(), FILE_READ);
  if (!f)
    return false;

  String json;
  json.reserve((size_t)f.size() + 8);
  while (f.available())
    json += (char)f.read();
  f.close();

  return parseManifestJson(json, out);
}

bool assetManifestSaveLocal(const AssetManifestData &manifest)
{
  if (!g_sdReady)
    return false;
  if (!assetOtaEnsureCoreDirs())
    return false;
  if (!assetOtaEnsureParentDir(assetOtaLocalManifestPath()))
    return false;

  DynamicJsonDocument doc((size_t)4096 + manifest.files.size() * 256);
  doc["pack_version"] = manifest.packVersion;
  doc["channel"] = manifest.channel;

  JsonArray files = doc.createNestedArray("files");
  for (const auto &f : manifest.files)
  {
    JsonObject o = files.createNestedObject();
    o["path"] = f.path;
    o["url"] = f.url;
    o["sha256"] = f.sha256;
    o["size"] = f.size;
  }

  if (SD.exists(assetOtaLocalManifestTmpPath()))
    SD.remove(assetOtaLocalManifestTmpPath());

  File out = SD.open(assetOtaLocalManifestTmpPath(), FILE_WRITE);
  if (!out)
    return false;

  if (serializeJson(doc, out) == 0)
  {
    out.close();
    SD.remove(assetOtaLocalManifestTmpPath());
    return false;
  }

  out.flush();
  out.close();

  if (SD.exists(assetOtaLocalManifestPath()))
    SD.remove(assetOtaLocalManifestPath());

  if (!SD.rename(assetOtaLocalManifestTmpPath(), assetOtaLocalManifestPath()))
  {
    SD.remove(assetOtaLocalManifestTmpPath());
    return false;
  }

  return true;
}

bool assetManifestDownloadRemote(const char *url, AssetManifestData *out)
{
  if (!out || !url || !url[0])
    return false;

  graphicsReleasePetLayerForOta();

  Serial.printf("[OTA] manifest url=%s\n", url);
  Serial.printf("[OTA] pre-http free=%u largest=%u\n", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());

  HTTPClient http;
  http.setReuse(false);
  http.setTimeout(15000);
  http.addHeader("Accept", "application/json");
  http.setUserAgent("RaisingHellCardputer/1.0");

  const String sUrl(url);
  bool began = false;

  WiFiClient plainClient;
  WiFiClientSecure secureClient;

  if (sUrl.startsWith("https://"))
  {
    secureClient.setInsecure();
    secureClient.setTimeout(15000);
    secureClient.setHandshakeTimeout(15);
    began = http.begin(secureClient, url);
  }
  else
  {
    began = http.begin(plainClient, url);
  }

  Serial.printf("[OTA] post-begin began=%d free=%u largest=%u\n", began ? 1 : 0, (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getMaxAllocHeap());

  if (!began)
  {
    Serial.println("[OTA] manifest http.begin failed");
    return false;
  }

  Serial.printf("[OTA] pre-GET free=%u largest=%u\n", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());

  const int code = http.GET();
  Serial.printf("[OTA] manifest http code=%d\n", code);

  if (code != HTTP_CODE_OK)
  {
    String errBody = http.getString();
    Serial.printf("[OTA] manifest error body: %s\n", errBody.c_str());
    http.end();
    return false;
  }

  String payload;
  payload.reserve(2048);
  payload = http.getString();
  http.end();

  Serial.printf("[OTA] manifest bytes=%u\n", (unsigned)payload.length());

  if (payload.isEmpty())
  {
    Serial.println("[OTA] manifest payload empty");
    return false;
  }

  const bool parsed = parseManifestJson(payload, out);
  Serial.printf("[OTA] manifest parsed=%d\n", parsed ? 1 : 0);
  if (!parsed)
  {
    Serial.println("[OTA] manifest parse failed");
    Serial.println(payload);
    return false;
  }

  return true;
}

void assetManifestBuildDiff(const AssetManifestData &localManifest, const AssetManifestData &remoteManifest,
                            std::vector<AssetManifestFile> &outChangedFiles)
{
  outChangedFiles.clear();

  for (const auto &rf : remoteManifest.files)
  {
    bool foundSame = false;

    for (const auto &lf : localManifest.files)
    {
      if (lf.path != rf.path)
        continue;

      const bool sameSize = (lf.size == rf.size);
      const bool sameHash = (lf.sha256.equalsIgnoreCase(rf.sha256));

      if (sameSize && sameHash)
      {
        String livePath = "/";
        livePath += rf.path;

        if (SD.exists(livePath.c_str()))
          foundSame = true;
      }

      break;
    }

    if (!foundSame)
      outChangedFiles.push_back(rf);
  }
}