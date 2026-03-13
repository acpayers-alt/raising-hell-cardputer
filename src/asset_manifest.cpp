#include "asset_manifest.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <SD.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>

#include "asset_ota.h"
#include "asset_ota_config.h"
#include "graphics.h"
#include "sdcard.h"

static bool parseManifestJson(const String &json, AssetManifestData *out)
{
  if (!out)
    return false;

  out->clear();

  JsonDocument doc;

  Serial.printf("[OTA] parse start: jsonLen=%u\n", (unsigned)json.length());

  DeserializationError err = deserializeJson(doc, json);
  if (err)
  {
    Serial.printf("[OTA] manifest deserialize failed: %s\n", err.c_str());
    return false;
  }

  JsonObject root = doc.as<JsonObject>();
  if (root.isNull())
  {
    Serial.println("[OTA] manifest root is null");
    return false;
  }

  const char *packVersion = root["packVersion"] | root["pack_version"] | "";

  if (!packVersion[0])
  {
    Serial.println("[OTA] manifest missing packVersion");
    return false;
  }

  const char *channel = root["channel"] | "";

  JsonVariant filesVar = root["files"];
  if (!filesVar.is<JsonArray>())
  {
    Serial.println("[OTA] manifest missing files array");
    return false;
  }

  out->packVersion = packVersion;
  out->channel = channel;

  JsonArray files = filesVar.as<JsonArray>();
  out->files.reserve(files.size());

  unsigned fileIndex = 0;
  for (JsonObject obj : files)
  {
    ++fileIndex;

    const char *rel = obj["path"] | "";
    if (!rel[0])
    {
      Serial.printf("[OTA] manifest file entry missing path at index=%u\n", fileIndex);
      return false;
    }

    AssetManifestFile f;
    f.path = rel;
    f.size = (uint32_t)(obj["size"] | 0UL);

    const char *urlField = obj["url"] | "";
    if (urlField[0])
    {
      f.url = urlField;
    }
    else
    {
      // Minimal manifest support: synthesize URL from public assets root.
      String base = "https://assets.raisinghellgame.com/assets/";
      if (!base.endsWith("/"))
        base += "/";
      f.url = base + f.path;
    }

    const char *shaField = obj["sha256"] | "";
    if (shaField[0])
    {
      f.sha256 = shaField;
      f.sha256.toLowerCase();
    }
    else
    {
      f.sha256 = "";
    }

    if (f.url.isEmpty())
    {
      Serial.printf("[OTA] manifest file entry has empty url at index=%u path=%s\n",
                    fileIndex,
                    f.path.c_str());
      return false;
    }
    if ((fileIndex % 50) == 0)
    {
      Serial.printf("[OTA] parse progress index=%u path=%s\n",
                    fileIndex,
                    f.path.c_str());
    }
    out->files.push_back(f);
  }

  Serial.printf("[OTA] manifest parsed ok: version=%s files=%u\n", out->packVersion.c_str(),
                (unsigned)out->files.size());

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
  doc["packVersion"] = manifest.packVersion;
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
  http.addHeader("Accept-Encoding", "identity");
  http.addHeader("Cache-Control", "no-cache");
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

  const int contentLen = http.getSize();
  Serial.printf("[OTA] manifest contentLen=%d\n", contentLen);

  WiFiClient *stream = http.getStreamPtr();
  if (!stream)
  {
    Serial.println("[OTA] manifest stream unavailable");
    http.end();
    return false;
  }

  String payload;
  if (contentLen > 0)
    payload.reserve((size_t)contentLen + 1);
  else
    payload.reserve(8192);

  uint8_t buf[1024];
  uint32_t started = millis();

  while (http.connected() && (contentLen < 0 || payload.length() < (size_t)contentLen))
  {
    const size_t avail = stream->available();
    if (avail == 0)
    {
      if (!http.connected())
        break;

      if ((millis() - started) > 15000)
      {
        Serial.println("[OTA] manifest read timeout");
        break;
      }

      delay(1);
      continue;
    }

    const size_t toRead = (avail > sizeof(buf)) ? sizeof(buf) : avail;
    const int n = stream->readBytes((char *)buf, toRead);
    if (n <= 0)
      break;

    payload.concat((const char *)buf, (size_t)n);
    started = millis();
  }

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