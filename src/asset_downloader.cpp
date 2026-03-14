#include "asset_downloader.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <SD.h>
#include <WiFiClientSecure.h>
#include <mbedtls/sha256.h>

#include "asset_manifest.h"
#include "asset_ota_config.h"
#include "sdcard.h"

static String joinStagingPath(const String &relPath)
{
  return String(assetOtaStagingRoot()) + "/" + relPath + ".part";
}

static String synthesizeAssetUrl(const char *relPath)
{
  String url = "https://assets.raisinghellgame.com/assets/";
  if (!url.endsWith("/"))
    url += "/";
  url += relPath ? relPath : "";
  return url;
}

static String toLowerHex(const uint8_t *buf, size_t len)
{
  static const char *hex = "0123456789abcdef";
  String out;
  out.reserve(len * 2);
  for (size_t i = 0; i < len; ++i)
  {
    out += hex[(buf[i] >> 4) & 0x0F];
    out += hex[buf[i] & 0x0F];
  }
  return out;
}

bool assetDownloadToStaging(const AssetManifestFile &file, String *outStagingPath, String *outErr)
{
  if (outStagingPath)
    *outStagingPath = "";
  if (outErr)
    *outErr = "";

  if (!g_sdReady)
  {
    if (outErr)
      *outErr = "SD not ready";
    return false;
  }

  if (file.path[0] == '\0')
  {
    if (outErr)
      *outErr = "Bad manifest path";
    return false;
  }

  String rel;
  if (!assetManifestNormalizePath(String(file.path), &rel))
  {
    if (outErr)
      *outErr = "Bad manifest path";
    return false;
  }

  const String stagingPath = joinStagingPath(rel);
  Serial.printf("[OTA] staging path=%s\n", stagingPath.c_str());

  if (!assetOtaEnsureParentDir(stagingPath.c_str()))
  {
    if (outErr)
      *outErr = "Failed to create staging dirs";
    return false;
  }

  if (SD.exists(stagingPath.c_str()))
    SD.remove(stagingPath.c_str());

  File out = SD.open(stagingPath.c_str(), FILE_WRITE);
  if (!out)
  {
    if (outErr)
      *outErr = "Failed to open staging file";
    return false;
  }

  String fileUrl = synthesizeAssetUrl(file.path);
  Serial.printf("[OTA] file url=%s\n", fileUrl.c_str());
    Serial.printf("[OTA] file pre-http free=%u largest=%u\n",
                (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getMaxAllocHeap());

  HTTPClient http;
  http.setReuse(false);
  http.setTimeout(30000);
  http.addHeader("Accept-Encoding", "identity");
  http.addHeader("Cache-Control", "no-cache");
  http.setUserAgent("RaisingHellCardputer/1.0");

  const String sUrl = fileUrl;
    bool began = false;

  WiFiClient plainClient;
  WiFiClientSecure secureClient;

  if (sUrl.startsWith("https://"))
  {
    secureClient.setInsecure();
    secureClient.setTimeout(15000);
    secureClient.setHandshakeTimeout(15);
    began = http.begin(secureClient, fileUrl);  }
  else
  {
    began = http.begin(plainClient, fileUrl);  }

  if (!began)
  {
    out.close();
    SD.remove(stagingPath.c_str());
    if (outErr)
      *outErr = "HTTP begin failed";
    Serial.println("[OTA] file http.begin failed");
    return false;
  }

  const int code = http.GET();
  Serial.printf("[OTA] file http code=%d\n", code);
  if (code != HTTP_CODE_OK)
  {
    http.end();
    out.close();
    SD.remove(stagingPath.c_str());
    if (outErr)
      *outErr = String("HTTP ") + code;
    return false;
  }

  WiFiClient *stream = http.getStreamPtr();
  const int contentLength = http.getSize();
  Serial.printf("[OTA] file contentLength=%d expected=%u\n",
                contentLength,
                (unsigned)file.size);

  String wantHash(file.sha256);
  wantHash.toLowerCase();
  wantHash.trim();

  const bool verifyHash = !wantHash.isEmpty();
  mbedtls_sha256_context ctx;
  uint8_t hash[32];

  if (verifyHash)
  {
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts_ret(&ctx, 0);
  }

  uint8_t buf[1024];
  uint32_t total = 0;
  uint32_t idleLoops = 0;

  while (http.connected() || (stream && stream->available() > 0))
  {
    const size_t avail = stream ? stream->available() : 0;
    if (avail == 0)
    {
      ++idleLoops;
      if (idleLoops > 5000)
        break;
      delay(1);
      continue;
    }

    idleLoops = 0;
    const size_t want = (avail > sizeof(buf)) ? sizeof(buf) : avail;
    const int n = stream->readBytes((char *)buf, want);
    if (n <= 0)
      continue;

    const size_t w = out.write(buf, (size_t)n);
    if (w != (size_t)n)
    {
      http.end();
      out.close();
      SD.remove(stagingPath.c_str());
      if (outErr)
        *outErr = "Write failed";
      return false;
    }

    if (verifyHash)
      mbedtls_sha256_update_ret(&ctx, buf, (size_t)n);

    total += (uint32_t)n;
  }

  if (verifyHash)
  {
    mbedtls_sha256_finish_ret(&ctx, hash);
    mbedtls_sha256_free(&ctx);
  }

  out.flush();
  out.close();
  http.end();

  if (contentLength >= 0 && total != (uint32_t)contentLength)
  {
    SD.remove(stagingPath.c_str());
    if (outErr)
      *outErr = "HTTP size mismatch";
    return false;
  }

  if (total != file.size)
  {
    SD.remove(stagingPath.c_str());
    if (outErr)
      *outErr = "Manifest size mismatch";
    return false;
  }

  Serial.printf("[OTA] file total=%u wantSize=%u\n",
                (unsigned)total,
                (unsigned)file.size);

  if (verifyHash)
  {
    String gotHash = toLowerHex(hash, sizeof(hash));

    Serial.printf("[OTA] file gotHash=%s\n", gotHash.c_str());
    Serial.printf("[OTA] file wantHash=%s\n", wantHash.c_str());

    if (gotHash != wantHash)
    {
      SD.remove(stagingPath.c_str());
      if (outErr)
        *outErr = "SHA256 mismatch";
      return false;
    }
  }
  else
  {
    Serial.println("[OTA] file hash skipped (no manifest sha256)");
  }

  Serial.printf("[OTA] file done total=%u\n", (unsigned)total);

  if (outStagingPath)
    *outStagingPath = stagingPath;

  return true;
}