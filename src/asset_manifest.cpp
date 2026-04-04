#include "asset_manifest.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <SD.h>
#include <mbedtls/sha256.h>

#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>
#include <vector>

#include "asset_ota.h"
#include "asset_ota_config.h"
#include "graphics.h"
#include "sdcard.h"

// --- Forward declarations ---
static bool parseManifestJsonWithCallback(File &input, size_t contentLen, String *outPackVersion, String *outChannel,
                                          bool (*onFile)(const AssetManifestFile &, void *), void *ctx);

static bool manifestEntryMatchesLocal(const AssetManifestData &localManifest, const AssetManifestFile &rf);

static bool manifestLiveAssetMatches(const AssetManifestFile &f);

struct WorklistCtx
{
  File *work;
  uint16_t changed;
  uint16_t processed;
};

const char *assetManifestTempPath()
{
  return "/raising_hell/ota/manifest.remote.tmp";
}

static bool manifestWorklistCallback(const AssetManifestFile &f, void *ctxVoid)
{
  WorklistCtx *ctx = (WorklistCtx *)ctxVoid;
  if (!ctx || !ctx->work)
    return false;

  ctx->processed++;

  if (!manifestLiveAssetMatches(f))
  {
    char line[256];

    int len = snprintf(line, sizeof(line), "%s\t%lu\t%s\n",
                       f.path,
                       (unsigned long)f.size,
                       f.sha256);

    if (len <= 0 || len >= (int)sizeof(line))
    {
      Serial.printf("[OTA WL WRITE] FAIL format path=%s\n", f.path);
      return false;
    }

    size_t written = ctx->work->write((const uint8_t *)line, len);

    if (written != (size_t)len)
    {
      Serial.printf("[OTA WL WRITE] FAIL partial write path=%s wrote=%u expected=%u\n",
                    f.path,
                    (unsigned)written,
                    (unsigned)len);
      return false;
    }

    if (!(*ctx->work))
    {
      Serial.printf("[OTA WL] callback fail: write %s\n", f.path);
      return false;
    }

    ctx->changed++;

    if ((ctx->changed % 25) == 0)
    {
      Serial.printf("[OTA WL WRITE] changed=%u processed=%u free=%u largest=%u\n",
                    (unsigned)ctx->changed,
                    (unsigned)ctx->processed,
                    (unsigned)ESP.getFreeHeap(),
                    (unsigned)ESP.getMaxAllocHeap());
    }
  }

  // unified progress logging (outside branch)
  if ((ctx->processed % 25) == 0)
  {
    Serial.printf("[OTA WL] progress processed=%u changed=%u free=%u largest=%u\n",
                  (unsigned)ctx->processed,
                  (unsigned)ctx->changed,
                  (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getMaxAllocHeap());
  }

  return true;
}

static bool manifestLiveAssetMatches(const AssetManifestFile &f)
{
  String livePath = "/";
  livePath += f.path;

  if (!SD.exists(livePath.c_str()))
    return false;

  File lf = SD.open(livePath.c_str(), FILE_READ);
  if (!lf)
    return false;

  if ((uint32_t)lf.size() != f.size)
  {
    lf.close();
    return false;
  }

  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);

  uint8_t buf[4096];
  while (true)
  {
    int r = lf.read(buf, sizeof(buf));
    if (r <= 0)
      break;
    mbedtls_sha256_update(&ctx, buf, (size_t)r);
  }

  lf.close();

  uint8_t hash[32];
  mbedtls_sha256_finish(&ctx, hash);
  mbedtls_sha256_free(&ctx);

  char hex[65];
  static const char *digits = "0123456789abcdef";
  for (int i = 0; i < 32; ++i)
  {
    hex[i * 2] = digits[(hash[i] >> 4) & 0x0F];
    hex[i * 2 + 1] = digits[hash[i] & 0x0F];
  }
  hex[64] = '\0';

  return strcasecmp(hex, f.sha256) == 0;
}

bool assetManifestLoadLocalPackVersion(String *outPackVersion)
{
  if (outPackVersion)
    *outPackVersion = "";

  File f = SD.open(assetOtaLocalManifestPath(), FILE_READ);
  if (!f)
    return false;

  StaticJsonDocument<128> filter;
  filter["packVersion"] = true;
  filter["pack_version"] = true;

  DynamicJsonDocument doc(256);

  DeserializationError err = deserializeJson(doc, f, DeserializationOption::Filter(filter));
  f.close();

  if (err)
    return false;

  const char *packVersion = doc["packVersion"] | doc["pack_version"] | "";
  if (!packVersion[0])
    return false;

  if (outPackVersion)
    *outPackVersion = packVersion;

  return true;
}

static bool assetManifestSelfCheckWorklist(uint16_t expectedCount, uint16_t *outReadableCount)
{
  if (outReadableCount)
    *outReadableCount = 0;

  // ✅ EARLY EXIT: empty worklist is valid
  if (expectedCount == 0)
  {
    Serial.println("[OTA WL CHECK] empty worklist OK (expected=0)");
    return true;
  }

  File in;
  if (!assetOtaWorklistOpenRead(&in))
  {
    Serial.printf("[OTA WL CHECK] open failed path=%s\n", assetOtaWorklistPath());
    return false;
  }

  uint16_t readable = 0;

  while (true)
  {
    AssetManifestFile mf{};
    if (!assetOtaWorklistReadNext(in, &mf))
    {
      // Only log as failure if we EXPECTED data
      Serial.printf("[OTA WL CHECK RAW] stop at record #%u pos=%ld size=%u\n", (unsigned)(readable + 1),
                    (long)in.position(), (unsigned)in.size());
      break;
    }

    ++readable;
  }

  in.close();

  if (outReadableCount)
    *outReadableCount = readable;

  Serial.printf("[OTA WL CHECK] done readable=%u expected=%u\n", (unsigned)readable, (unsigned)expectedCount);

  return true;
}

bool assetManifestBuildWorklistFromRemote(const char *url, String *outPackVersion, uint16_t *outChangedCount)
{
  Serial.printf("[OTA WL] begin url=%s free=%u largest=%u\n", url ? url : "(null)", (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getMaxAllocHeap());

  if (!url || !url[0])
  {
    Serial.println("[OTA WL] fail: empty url");
    return false;
  }

  if (outPackVersion)
    *outPackVersion = "";
  if (outChangedCount)
    *outChangedCount = 0;

  if (!assetOtaEnsureCoreDirs())
  {
    Serial.println("[OTA WL] fail: assetOtaEnsureCoreDirs");
    return false;
  }

  if (!assetOtaWorklistClear())
  {
    Serial.println("[OTA WL] fail: assetOtaWorklistClear");
    return false;
  }

  const char *tmpManifestPath = assetManifestTempPath();

  if (SD.exists(tmpManifestPath))
    SD.remove(tmpManifestPath);

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(15000);
  client.setHandshakeTimeout(15);

  HTTPClient http;
  http.setReuse(false);
  http.setTimeout(15000);
  http.addHeader("Accept", "application/json");
  http.addHeader("Accept-Encoding", "identity");
  http.addHeader("Cache-Control", "no-cache");
  http.setUserAgent("RaisingHellCardputer/1.0");

  Serial.println("[OTA WL] http.begin...");
  if (!http.begin(client, url))
  {
    Serial.println("[OTA WL] fail: http.begin");
    return false;
  }

  Serial.println("[OTA WL] http.GET...");
  const int code = http.GET();
  Serial.printf("[OTA WL] http code=%d\n", code);

  if (code != HTTP_CODE_OK)
  {
    String body = http.getString();
    Serial.printf("[OTA WL] fail: GET body=%s\n", body.c_str());
    http.end();
    return false;
  }

  const int contentLen = http.getSize();
  Serial.printf("[OTA WL] contentLen=%d\n", contentLen);

  File out = SD.open(tmpManifestPath, FILE_WRITE);
  if (!out)
  {
    Serial.printf("[OTA WL] fail: open temp file %s\n", tmpManifestPath);
    http.end();
    return false;
  }

  WiFiClient *stream = http.getStreamPtr();
  if (!stream)
  {
    Serial.println("[OTA WL] fail: null stream");
    out.close();
    SD.remove(tmpManifestPath);
    http.end();
    return false;
  }

  uint8_t buf[1024];
  size_t total = 0;
  uint32_t idleStartMs = 0;

  while (true)
  {
    const size_t toRead =
        (contentLen > 0)
            ? min(sizeof(buf), (size_t)max(0, contentLen - (int)total))
            : sizeof(buf);

    if (contentLen > 0 && total >= (size_t)contentLen)
      break;

    size_t n = stream->readBytes((char *)buf, toRead);

    if (n == 0)
    {
      if (idleStartMs == 0)
        idleStartMs = millis();

      // If server gave us a content length, do not accept a short file.
      if (contentLen > 0)
      {
        if ((millis() - idleStartMs) < 5000)
        {
          delay(10);
          continue;
        }

        Serial.printf("[OTA WL] fail: truncated download total=%u expected=%u\n",
                      (unsigned)total, (unsigned)contentLen);
        out.close();
        SD.remove(tmpManifestPath);
        http.end();
        return false;
      }

      // Unknown length: allow a short idle period before treating it as EOF.
      if ((millis() - idleStartMs) < 1000)
      {
        delay(10);
        continue;
      }

      break;
    }

    idleStartMs = 0;

    size_t wrote = out.write(buf, n);
    if (wrote != n)
    {
      Serial.printf("[OTA WL] fail: temp write total=%u wrote=%u want=%u\n",
                    (unsigned)total, (unsigned)wrote, (unsigned)n);
      out.close();
      SD.remove(tmpManifestPath);
      http.end();
      return false;
    }

    total += n;
    if ((total % 8192) == 0)
    {
      Serial.printf("[OTA WL] download progress=%u/%u\n",
                    (unsigned)total,
                    (unsigned)((contentLen > 0) ? contentLen : 0));
    }
  }

  out.flush();
  out.close();
  http.end();

  Serial.printf("[OTA WL] temp saved bytes=%u expected=%u\n",
                (unsigned)total,
                (unsigned)((contentLen > 0) ? contentLen : 0));

  if (contentLen > 0 && total != (size_t)contentLen)
  {
    Serial.printf("[OTA WL] fail: saved size mismatch total=%u expected=%u\n",
                  (unsigned)total, (unsigned)contentLen);
    SD.remove(tmpManifestPath);
    return false;
  }

  File mf = SD.open(tmpManifestPath, FILE_READ);
  if (!mf)
  {
    Serial.println("[OTA WL] fail: reopen temp manifest");
    SD.remove(tmpManifestPath);
    return false;
  }

  const size_t mfLen = mf.size();
  String channel;

  Serial.printf("[OTA WL] parsing callback manifest len=%u\n", (unsigned)mfLen);

  if (!assetOtaEnsureParentDir(assetOtaWorklistPath()))
  {
    Serial.printf("[OTA WL] fail: ensure worklist dir for %s\n", assetOtaWorklistPath());
    mf.close();
    SD.remove(tmpManifestPath);
    return false;
  }

  if (SD.exists(assetOtaWorklistPath()))
  {
    SD.remove(assetOtaWorklistPath());
  }

  // ensure worklist directory exists
  if (!assetOtaEnsureParentDir(assetOtaWorklistPath()))
  {
    Serial.printf("[OTA WL] fail: ensure worklist dir for %s\n", assetOtaWorklistPath());
    mf.close();
    SD.remove(tmpManifestPath);
    return false;
  }
  
  // wipe old worklist safely
  if (SD.exists(assetOtaWorklistPath()))
  {
    SD.remove(assetOtaWorklistPath());
  }
  
  // open fresh worklist
  File work = SD.open(assetOtaWorklistPath(), FILE_WRITE);
  if (!work)
  {
    Serial.println("[OTA WL] FAIL open");
    mf.close();
    SD.remove(tmpManifestPath);
    return false;
  }
  
  // create context ONCE
  WorklistCtx ctx{&work, 0, 0};

  const bool ok = parseManifestJsonWithCallback(mf, mfLen, outPackVersion, &channel, manifestWorklistCallback, &ctx);

  Serial.printf("[OTA WL] final worklist changed=%u processed=%u\n", (unsigned)ctx.changed, (unsigned)ctx.processed);

  work.flush();
  work.close();

  if (!ok)
  {
    mf.close();
    SD.remove(tmpManifestPath);
    assetOtaWorklistClear();
    return false;
  }

  uint16_t readableCount = 0;
  if (!assetManifestSelfCheckWorklist(ctx.changed, &readableCount))
  {
    Serial.printf("[OTA WL] self-check failed expected=%u readable=%u\n", (unsigned)ctx.changed,
                  (unsigned)readableCount);
    mf.close();
    SD.remove(tmpManifestPath);
    assetOtaWorklistClear();
    return false;
  }

  if (readableCount != ctx.changed)
  {
    Serial.printf("[OTA WL] self-check mismatch readable=%u changed=%u\n", (unsigned)readableCount,
                  (unsigned)ctx.changed);
    mf.close();
    SD.remove(tmpManifestPath);
    assetOtaWorklistClear();
    return false;
  }

  mf.close();

  Serial.printf("[OTA WL] parse result=%d changed=%u pack=%s channel=%s\n", ok ? 1 : 0, (unsigned)ctx.changed,
                outPackVersion ? outPackVersion->c_str() : "", channel.c_str());

  if (outChangedCount)
    *outChangedCount = ctx.changed;

  return true;
}

static bool saveStreamToFile(Stream &input, const char *path, size_t contentLen)
{
  if (!path || !path[0])
  {
    Serial.println("[OTA] temp manifest path missing");
    return false;
  }

  if (!assetOtaEnsureParentDir(path))
  {
    Serial.printf("[OTA] failed to ensure temp manifest parent dir: %s\n", path);
    return false;
  }

  File f = SD.open(path, FILE_WRITE);
  if (!f)
  {
    Serial.printf("[OTA] failed to open temp manifest file: %s\n", path);
    return false;
  }
  
  const size_t kBufSize = 1024;
  uint8_t buf[kBufSize];
  size_t total = 0;

  uint32_t idleStartMs = 0;

  while (true)
  {
    const size_t toRead =
        (contentLen > 0)
            ? min(sizeof(buf), (size_t)max((size_t)0, contentLen - total))
            : sizeof(buf);

    if (contentLen > 0 && total >= contentLen)
      break;

    size_t n = input.readBytes((char *)buf, toRead);

    if (n == 0)
    {
      if (idleStartMs == 0)
        idleStartMs = millis();

      if (contentLen > 0)
      {
        if ((millis() - idleStartMs) < 5000)
        {
          delay(10);
          continue;
        }

        Serial.printf("[OTA] manifest temp save truncated total=%u expected=%u\n",
                      (unsigned)total, (unsigned)contentLen);
        f.close();
        SD.remove(path);
        return false;
      }

      if ((millis() - idleStartMs) < 1000)
      {
        delay(10);
        continue;
      }

      break;
    }

    idleStartMs = 0;

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
      Serial.printf("[OTA] manifest download progress=%u/%u\n",
                    (unsigned)total, (unsigned)contentLen);
    }
  }

  f.flush();
  f.close();

  Serial.printf("[OTA] manifest temp file saved bytes=%u expected=%u\n",
                (unsigned)total, (unsigned)contentLen);

  if (contentLen > 0 && total != contentLen)
  {
    Serial.printf("[OTA] manifest temp file size mismatch total=%u expected=%u\n",
                  (unsigned)total, (unsigned)contentLen);
    SD.remove(path);
    return false;
  }

  return total > 0;}

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

  DynamicJsonDocument doc(4096);

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

  const char *tmpManifestPath = assetManifestTempPath();

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

static bool manifestEntryMatchesLocal(const AssetManifestData &localManifest, const AssetManifestFile &rf)
{
  for (const auto &lf : localManifest.files)
  {
    if (strcmp(lf.path, rf.path) != 0)
      continue;

    const bool sameSize = (lf.size == rf.size);
    const bool sameHash = strcasecmp(lf.sha256, rf.sha256) == 0;

    if (sameSize && sameHash)
    {
      String livePath = "/";
      livePath += rf.path;

      if (SD.exists(livePath.c_str()))
        return true;
    }

    return false;
  }

  return false;
}

static bool manifestReadTopLevelHeader(File &input, String *outPackVersion, String *outChannel)
{
  if (outPackVersion)
    *outPackVersion = "";
  if (outChannel)
    *outChannel = "";

  input.seek(0);

  StaticJsonDocument<128> filter;
  filter["packVersion"] = true;
  filter["pack_version"] = true;
  filter["channel"] = true;

  DynamicJsonDocument doc(256);
  DeserializationError err = deserializeJson(doc, input, DeserializationOption::Filter(filter));
  if (err || doc.overflowed())
  {
    Serial.printf("[OTA WL] header parse failed: %s overflowed=%d\n", err ? err.c_str() : "Ok",
                  doc.overflowed() ? 1 : 0);
    return false;
  }

  const char *packVersion = doc["packVersion"] | doc["pack_version"] | "";
  const char *channel = doc["channel"] | "";

  if (!packVersion || !packVersion[0])
  {
    Serial.println("[OTA WL] header parse failed: missing packVersion");
    return false;
  }

  if (outPackVersion)
    *outPackVersion = packVersion;
  if (outChannel)
    *outChannel = channel;

  return true;
}

static bool manifestSeekFilesArray(File &input)
{
  input.seek(0);

  const char *needle = "\"files\"";
  size_t match = 0;
  bool inString = false;
  bool escape = false;

  while (input.available())
  {
    int rc = input.read();
    if (rc < 0)
      break;

    char c = (char)rc;

    if (inString)
    {
      if (escape)
      {
        escape = false;
      }
      else if (c == '\\')
      {
        escape = true;
      }
      else if (c == '"')
      {
        inString = false;
      }
    }
    else
    {
      if (c == '"')
      {
        inString = true;
      }
    }

    if (c == needle[match])
    {
      match++;
      if (needle[match] == '\0')
        break;
    }
    else
    {
      match = (c == needle[0]) ? 1 : 0;
    }
  }

  if (needle[match] != '\0')
  {
    Serial.println("[OTA WL] files array key not found");
    return false;
  }

  while (input.available())
  {
    int rc = input.read();
    if (rc < 0)
      return false;

    char c = (char)rc;
    if (c == '[')
      return true;
  }

  Serial.println("[OTA WL] files array '[' not found");
  return false;
}

static bool manifestReadNextFileObject(File &input, String *outJson, bool *outDone)
{
  if (!outJson || !outDone)
    return false;

  *outJson = "";
  *outDone = false;

  bool inString = false;
  bool escape = false;

  while (input.available())
  {
    int rc = input.read();
    if (rc < 0)
      return false;

    char c = (char)rc;

    if (c == ']')
    {
      *outDone = true;
      return true;
    }

    if (c == '{')
    {
      int depth = 1;
      outJson->reserve(256);
      *outJson += c;

      while (input.available())
      {
        int rc2 = input.read();
        if (rc2 < 0)
          return false;

        char d = (char)rc2;
        *outJson += d;

        if (inString)
        {
          if (escape)
          {
            escape = false;
          }
          else if (d == '\\')
          {
            escape = true;
          }
          else if (d == '"')
          {
            inString = false;
          }
        }
        else
        {
          if (d == '"')
          {
            inString = true;
          }
          else if (d == '{')
          {
            depth++;
          }
          else if (d == '}')
          {
            depth--;
            if (depth == 0)
              return true;
          }
        }
      }

      Serial.println("[OTA WL] unexpected EOF while reading file object");
      return false;
    }
  }

  *outDone = true;
  return true;
}

static bool manifestParseFileObjectJson(const String &json, AssetManifestFile *out)
{
  if (!out)
    return false;

  StaticJsonDocument<128> filter;
  filter["path"] = true;
  filter["size"] = true;
  filter["sha256"] = true;

  DynamicJsonDocument doc(256);
  DeserializationError err = deserializeJson(doc, json, DeserializationOption::Filter(filter));
  if (err || doc.overflowed())
  {
    Serial.printf("[OTA WL] file object parse failed: %s overflowed=%d\n", err ? err.c_str() : "Ok",
                  doc.overflowed() ? 1 : 0);
    return false;
  }

  const char *rel = doc["path"] | "";
  if (!rel || !rel[0])
    return false;

  String normPath;
  if (!assetManifestNormalizePath(rel, &normPath))
    return false;

  memset(out, 0, sizeof(*out));
  strlcpy(out->path, normPath.c_str(), sizeof(out->path));
  out->size = (uint32_t)(doc["size"] | 0UL);

  const char *sha = doc["sha256"] | "";
  if (sha && sha[0])
  {
    strlcpy(out->sha256, sha, sizeof(out->sha256));

    for (size_t i = 0; out->sha256[i]; ++i)
      out->sha256[i] = (char)tolower((unsigned char)out->sha256[i]);

    if (strlen(out->sha256) != 64)
    {
      Serial.printf("[OTA WL] bad sha len=%u path=%s\n", (unsigned)strlen(out->sha256), out->path);
      return false;
    }
  }
  else
  {
    out->sha256[0] = '\0';
  }

  return true;
}

static bool parseManifestJsonWithCallback(File &input, size_t contentLen, String *outPackVersion, String *outChannel,
                                          bool (*onFile)(const AssetManifestFile &, void *), void *ctx)
{
  Serial.printf("[OTA WL] cb-parse start len=%u free=%u largest=%u\n", (unsigned)contentLen,
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());

  if (!manifestReadTopLevelHeader(input, outPackVersion, outChannel))
    return false;

  Serial.printf("[OTA WL] header pack=%s channel=%s\n", outPackVersion ? outPackVersion->c_str() : "",
                outChannel ? outChannel->c_str() : "");

  if (!manifestSeekFilesArray(input))
    return false;

  unsigned fileIndex = 0;

  while (true)
  {
    String objJson;
    bool done = false;

    if (!manifestReadNextFileObject(input, &objJson, &done))
      return false;

    if (done)
      break;

    AssetManifestFile f{};
    if (!manifestParseFileObjectJson(objJson, &f))
      continue;

    ++fileIndex;

    if (onFile)
    {
      if (!onFile(f, ctx))
      {
        Serial.printf("[OTA WL] cb-parse callback failed at %u path=%s\n", fileIndex, f.path);
        return false;
      }
    }

    if ((fileIndex % 25) == 0)
    {
      Serial.printf("[OTA WL] progress processed=%u free=%u largest=%u\n", fileIndex, (unsigned)ESP.getFreeHeap(),
                    (unsigned)ESP.getMaxAllocHeap());
    }
  }

  Serial.printf("[OTA WL] cb-parse complete processed=%u free=%u largest=%u\n", fileIndex, (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getMaxAllocHeap());

  return true;
}

static bool parseManifestJsonDiffOnly(Stream &input, size_t contentLen, const AssetManifestData &localManifest,
                                      String *outPackVersion, String *outChannel,
                                      std::vector<AssetManifestFile> &outChangedFiles)
{
  if (outPackVersion)
    *outPackVersion = "";
  if (outChannel)
    *outChannel = "";

  outChangedFiles.clear();

  StaticJsonDocument<512> filter;
  filter["packVersion"] = true;
  filter["pack_version"] = true;
  filter["channel"] = true;
  filter["files"][0]["path"] = true;
  filter["files"][0]["size"] = true;
  filter["files"][0]["sha256"] = true;

  DynamicJsonDocument doc(65536);

  Serial.printf("[OTA] diff-parse start: contentLen=%u free=%u largest=%u cap=%u\n", (unsigned)contentLen,
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap(), (unsigned)doc.capacity());

  DeserializationError err = deserializeJson(doc, input, DeserializationOption::Filter(filter));

  Serial.printf("[OTA] diff-parse deserialize: %s free=%u largest=%u overflowed=%d\n", err ? err.c_str() : "Ok",
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap(), doc.overflowed() ? 1 : 0);

  if (err)
    return false;

  JsonObject root = doc.as<JsonObject>();
  if (root.isNull())
    return false;

  const char *packVersion = root["packVersion"] | root["pack_version"] | "";
  if (!packVersion[0])
    return false;

  const char *channel = root["channel"] | "";

  if (outPackVersion)
    *outPackVersion = packVersion;
  if (outChannel)
    *outChannel = channel;

  JsonVariant filesVar = root["files"];
  if (!filesVar.is<JsonArray>())
    return false;

  JsonArray files = filesVar.as<JsonArray>();
  const unsigned totalFiles = files.size();

  Serial.printf("[OTA] diff-parse files=%u free=%u largest=%u\n", totalFiles, (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getMaxAllocHeap());

  outChangedFiles.reserve(totalFiles > 64 ? 64 : totalFiles);

  unsigned fileIndex = 0;
  for (JsonObject obj : files)
  {
    ++fileIndex;

    const char *rel = obj["path"] | "";
    if (!rel || !rel[0])
      continue;

    String normPath;
    if (!assetManifestNormalizePath(rel, &normPath))
      continue;

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
        Serial.printf("[OTA WL WRITE] ERROR bad sha before write len=%u path=%s sha='%s'\n", (unsigned)strlen(f.sha256),
                      f.path, f.sha256);
        return false;
      }
    }
    else
    {
      f.sha256[0] = '\0';
    }

    if (!manifestEntryMatchesLocal(localManifest, f))
      outChangedFiles.push_back(f);

    if ((fileIndex % 25) == 0)
    {
      Serial.printf("[OTA] diff-parse progress=%u/%u changed=%u free=%u largest=%u\n", fileIndex, totalFiles,
                    (unsigned)outChangedFiles.size(), (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
    }
  }

  Serial.printf("[OTA] diff-parse complete changed=%u free=%u largest=%u\n", (unsigned)outChangedFiles.size(),
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());

  return true;
}

bool assetManifestDownloadDiffOnly(const char *url, const AssetManifestData &localManifest, String *outPackVersion,
                                   std::vector<AssetManifestFile> &outChangedFiles)
{
  if (!url || !url[0])
    return false;

  if (outPackVersion)
    *outPackVersion = "";

  graphicsReleasePetLayerForOta();

  Serial.printf("[OTA] manifest url=%s\n", url);
  Serial.printf("[OTA] pre-http free=%u largest=%u\n", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());

  std::unique_ptr<HTTPClient> http(new HTTPClient());
  if (!http)
    return false;

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
      return false;

    secureClient->setInsecure();
    secureClient->setTimeout(15000);
    secureClient->setHandshakeTimeout(15);
    began = http->begin(*secureClient, url);
  }
  else
  {
    plainClient.reset(new WiFiClient());
    if (!plainClient)
      return false;

    began = http->begin(*plainClient, url);
  }

  Serial.printf("[OTA] post-begin began=%d free=%u largest=%u\n", began ? 1 : 0, (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getMaxAllocHeap());

  if (!began)
    return false;

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
    http->end();
    return false;
  }

  const char *tmpManifestPath = assetManifestTempPath();

  if (SD.exists(tmpManifestPath))
    SD.remove(tmpManifestPath);

  const bool saved = saveStreamToFile(*stream, tmpManifestPath, (size_t)((contentLen > 0) ? contentLen : 0));
  http->end();

  Serial.printf("[OTA] manifest temp saved=%d free=%u largest=%u\n", saved ? 1 : 0, (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getMaxAllocHeap());

  if (!saved)
  {
    SD.remove(tmpManifestPath);
    return false;
  }

  File mf = SD.open(tmpManifestPath, FILE_READ);
  if (!mf)
  {
    SD.remove(tmpManifestPath);
    return false;
  }

  const size_t mfLen = mf.size();
  String channel;
  const bool parsed = parseManifestJsonDiffOnly(mf, mfLen, localManifest, outPackVersion, &channel, outChangedFiles);
  mf.close();

  if (!parsed)
    SD.remove(tmpManifestPath);

  Serial.printf("[OTA] diff-only parsed=%d changed=%u\n", parsed ? 1 : 0, (unsigned)outChangedFiles.size());

  return parsed;
}