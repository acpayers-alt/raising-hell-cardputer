#include "asset_manifest.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <SD.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <ctype.h>
#include <string.h>
#include <vector>

#include "asset_ota.h"
#include "asset_ota_config.h"
#include "graphics.h"
#include "sdcard.h"

static String synthesizeAssetUrl(const String &relPath)
{
  String base = "https://assets.raisinghellgame.com/assets/";
  if (!base.endsWith("/"))
    base += "/";
  return base + relPath;
}
static bool saveStreamToFile(Stream &input, const char *path, size_t contentLen)
{
  File f = SD.open(path, FILE_WRITE);
  if (!f)
  {
    Serial.printf("[OTA] failed to open temp manifest file: %s\n", path);
    return false;
  }

  const size_t kBufSize = 1024;
  uint8_t buf[kBufSize];
  size_t total = 0;

  while (true)
  {
    size_t n = input.readBytes((char *)buf, sizeof(buf));
    if (n == 0)
      break;

    size_t wrote = f.write(buf, n);
    if (wrote != n)
    {
      Serial.printf("[OTA] failed writing temp manifest file at total=%u\n", (unsigned)total);
      f.close();
      SD.remove(path);
      return false;
    }

    total += n;

    if ((total % 8192) == 0)
    {
      Serial.printf("[OTA] manifest download progress=%u/%u\n", (unsigned)total, (unsigned)contentLen);
    }
  }

  f.flush();
  f.close();

  Serial.printf("[OTA] manifest temp file saved bytes=%u\n", (unsigned)total);
  return total > 0;
}

static bool parseManifestJson(Stream &input, size_t contentLen, AssetManifestData *out)
{
  if (!out)
    return false;

  out->clear();

  StaticJsonDocument<512> filter;
  filter["packVersion"] = true;
  filter["pack_version"] = true;
  filter["channel"] = true;
  filter["files"][0]["path"] = true;
  filter["files"][0]["size"] = true;
  filter["files"][0]["sha256"] = true;

  DynamicJsonDocument doc(65536);

  Serial.printf("[OTA] parse start: contentLen=%u free=%u largest=%u cap=%u\n", (unsigned)contentLen,
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap(), (unsigned)doc.capacity());

  DeserializationError err = deserializeJson(doc, input, DeserializationOption::Filter(filter));

  Serial.printf("[OTA] deserialize returned: %s free=%u largest=%u overflowed=%d\n", err ? err.c_str() : "Ok",
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap(), doc.overflowed() ? 1 : 0);

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
  Serial.printf("[OTA] files array count=%u free=%u largest=%u\n", (unsigned)files.size(), (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getMaxAllocHeap());

  out->files.reserve(files.size());

  unsigned fileIndex = 0;
  const unsigned totalFiles = files.size();

  for (JsonObject obj : files)
  {
    ++fileIndex;

    const char *rel = obj["path"] | "";
    if (!rel || !rel[0])
    {
      Serial.printf("[OTA] skipping manifest entry with empty path at index=%u\n", fileIndex);
      continue;
    }

    String normPath;
    if (!assetManifestNormalizePath(rel, &normPath))
    {
      Serial.printf("[OTA] manifest file entry invalid path at index=%u raw='%s'\n", fileIndex, rel);
      continue;
    }

    AssetManifestFile f{};
    strlcpy(f.path, normPath.c_str(), sizeof(f.path));
    f.size = (uint32_t)(obj["size"] | 0UL);

    const char *shaField = obj["sha256"] | "";
    if (shaField && shaField[0])
    {
      strlcpy(f.sha256, shaField, sizeof(f.sha256));

      for (size_t j = 0; f.sha256[j]; ++j)
        f.sha256[j] = (char)tolower((unsigned char)f.sha256[j]);

      if (strlen(f.sha256) != 64)
      {
        Serial.printf("[OTA] manifest file entry bad sha len=%u at index=%u path=%s\n", (unsigned)strlen(f.sha256),
                      fileIndex, f.path);
        continue;
      }
    }
    else
    {
      f.sha256[0] = '\0';
    }

    if ((fileIndex % 10) == 1)
    {
      Serial.printf("[OTA] pre-push index=%u/%u free=%u largest=%u path=%s\n", fileIndex, totalFiles,
                    (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap(), f.path);
    }
    out->files.push_back(f);

    if ((fileIndex % 10) == 1)
    {
      Serial.printf("[OTA] post-push index=%u/%u free=%u largest=%u\n", fileIndex, totalFiles,
                    (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
    }
  }

  Serial.printf("[OTA] parse complete files=%u free=%u largest=%u\n", (unsigned)out->files.size(),
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());

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

  const size_t len = (size_t)f.size();
  const bool ok = parseManifestJson(f, len, out);
  f.close();
  return ok;
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
    if (f.sha256[0])
      o["sha256"] = f.sha256;
    else
      o["sha256"] = "";
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

  std::unique_ptr<HTTPClient> http(new HTTPClient());
  if (!http)
  {
    Serial.println("[OTA] failed to allocate HTTPClient");
    return false;
  }

  http->setReuse(false);
  http->setTimeout(15000);
  http->addHeader("Accept", "application/json");
  http->addHeader("Accept-Encoding", "identity");
  http->addHeader("Cache-Control", "no-cache");
  http->setUserAgent("RaisingHellCardputer/1.0");

  const bool isHttps = (strncmp(url, "https://", 8) == 0);
  bool began = false;

  std::unique_ptr<WiFiClient> plainClient;
  std::unique_ptr<WiFiClientSecure> secureClient;

  if (isHttps)
  {
    secureClient.reset(new WiFiClientSecure());
    if (!secureClient)
    {
      Serial.println("[OTA] failed to allocate WiFiClientSecure");
      return false;
    }

    secureClient->setInsecure();
    secureClient->setTimeout(15000);
    secureClient->setHandshakeTimeout(15);
    began = http->begin(*secureClient, url);
  }
  else
  {
    plainClient.reset(new WiFiClient());
    if (!plainClient)
    {
      Serial.println("[OTA] failed to allocate WiFiClient");
      return false;
    }

    began = http->begin(*plainClient, url);
  }

  Serial.printf("[OTA] post-begin began=%d free=%u largest=%u\n", began ? 1 : 0, (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getMaxAllocHeap());

  if (!began)
  {
    Serial.println("[OTA] manifest http.begin failed");
    return false;
  }

  Serial.printf("[OTA] pre-GET free=%u largest=%u\n", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());

  const int code = http->GET();
  Serial.printf("[OTA] manifest http code=%d\n", code);

  if (code != HTTP_CODE_OK)
  {
    String errBody = http->getString();
    Serial.printf("[OTA] manifest error body: %s\n", errBody.c_str());
    http->end();
    return false;
  }

  const int contentLen = http->getSize();
  Serial.printf("[OTA] manifest contentLen=%d\n", contentLen);

  WiFiClient *stream = http->getStreamPtr();
  if (!stream)
  {
    Serial.println("[OTA] manifest stream unavailable");
    http->end();
    return false;
  }

  const char *tmpManifestPath = "/manifest.remote.tmp";

  if (SD.exists(tmpManifestPath))
    SD.remove(tmpManifestPath);

  const bool saved = saveStreamToFile(*stream, tmpManifestPath, (size_t)((contentLen > 0) ? contentLen : 0));

  http->end();

  Serial.printf("[OTA] manifest temp saved=%d free=%u largest=%u\n", saved ? 1 : 0, (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getMaxAllocHeap());

  if (!saved)
  {
    Serial.println("[OTA] manifest temp save failed");
    SD.remove(tmpManifestPath);
    return false;
  }

  File mf = SD.open(tmpManifestPath, FILE_READ);
  if (!mf)
  {
    Serial.println("[OTA] failed to reopen temp manifest file");
    SD.remove(tmpManifestPath);
    return false;
  }

  const size_t mfLen = mf.size();
  const bool parsed = parseManifestJson(mf, mfLen, out);
  mf.close();
  SD.remove(tmpManifestPath);

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
  outChangedFiles.reserve(remoteManifest.files.size());

  for (const auto &rf : remoteManifest.files)
  {
    String normPath;
    if (!assetManifestNormalizePath(String(rf.path), &normPath))
    {
      Serial.printf("[OTA] skipping remote manifest entry with invalid path '%s'\n", rf.path);
      continue;
    }

    bool foundSame = false;

    for (const auto &lf : localManifest.files)
    {
      if (String(lf.path) != normPath)
        continue;

      const bool sameSize = (lf.size == rf.size);
      const bool sameHash = String(lf.sha256).equalsIgnoreCase(String(rf.sha256));

      if (sameSize && sameHash)
      {
        String livePath = "/";
        livePath += normPath;

        if (SD.exists(livePath.c_str()))
          foundSame = true;
      }

      break;
    }

    if (!foundSame)
    {
      AssetManifestFile clean{};
      strlcpy(clean.path, normPath.c_str(), sizeof(clean.path));
      strlcpy(clean.sha256, rf.sha256, sizeof(clean.sha256));
      clean.size = rf.size;
      outChangedFiles.push_back(clean);
    }
  }
}

bool assetManifestDownloadDiffOnly(const char *url, const AssetManifestData &localManifest, String *outPackVersion,
                                   std::vector<AssetManifestFile> &outChangedFiles)
{
  AssetManifestData remoteManifest;
  if (!assetManifestDownloadRemote(url, &remoteManifest))
    return false;

  if (outPackVersion)
    *outPackVersion = remoteManifest.packVersion;

  Serial.printf("[OTA] pre-diff remote=%u local=%u free=%u largest=%u\n", (unsigned)remoteManifest.files.size(),
                (unsigned)localManifest.files.size(), (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());

  if (localManifest.files.empty())
  {
    Serial.printf("[OTA] local manifest empty; swapping in all %u remote files\n",
                  (unsigned)remoteManifest.files.size());

    outChangedFiles.clear();
    outChangedFiles.swap(remoteManifest.files);

    Serial.printf("[OTA] swap complete changed.size=%u free=%u largest=%u\n", (unsigned)outChangedFiles.size(),
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());

    return true;
  }

  assetManifestBuildDiff(localManifest, remoteManifest, outChangedFiles);

  Serial.printf("[OTA] diff complete changed.size=%u free=%u largest=%u\n", (unsigned)outChangedFiles.size(),
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());

  return true;
}