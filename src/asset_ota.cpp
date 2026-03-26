#include "asset_ota.h"

#include "asset_downloader.h"
#include "asset_manifest.h"
#include "asset_ota_config.h"
#include "boot_pipeline.h"
#include "display.h"
#include "graphics.h"
#include "sdcard.h"
#include "ui_invalidate.h"
#include "ui_runtime.h"
#include "wifi_time.h"

#include <Arduino.h>
#include <SD.h>
#include <mbedtls/sha256.h>
#include <string.h>
#include <vector>

const char *assetOtaWorklistPath() { return "/raising_hell/ota/worklist.txt"; }

bool assetOtaWorklistClear()
{
  if (SD.exists(assetOtaWorklistPath()))
    SD.remove(assetOtaWorklistPath());
  return true;
}

bool assetOtaWorklistAppend(const AssetManifestFile &f)
{
  if (!assetOtaEnsureParentDir(assetOtaWorklistPath()))
  {
    Serial.printf("[OTA WL] append fail: ensure parent dir for %s\n", assetOtaWorklistPath());
    return false;
  }

  File out = SD.open(assetOtaWorklistPath(), FILE_WRITE);
  if (!out)
  {
    Serial.printf("[OTA WL] append fail: open %s\n", assetOtaWorklistPath());
    return false;
  }

  out.seek(out.size());

  out.print(f.path);
  out.print('\t');
  out.print((unsigned long)f.size);
  out.print('\t');
  out.print(f.sha256);
  out.print('\n');

  out.flush();
  out.close();
  return true;
}

bool assetOtaWorklistOpenRead(File *out)
{
  if (!out)
    return false;
  *out = SD.open(assetOtaWorklistPath(), FILE_READ);
  return (bool)(*out);
}

bool assetOtaWorklistReadNext(File &in, AssetManifestFile *out)
{
  if (!out)
    return false;

  char buf[256];
  int len = in.readBytesUntil('\n', buf, sizeof(buf) - 1);
  if (len <= 0)
    return false;

  buf[len] = '\0';

  String line(buf);
  line.trim();

  if (!line.length())
    return false;

  int t1 = line.indexOf('\t');
  int t2 = line.indexOf('\t', t1 + 1);
  if (t1 < 0 || t2 < 0)
    return false;

  String path = line.substring(0, t1);
  String sizeStr = line.substring(t1 + 1, t2);
  String sha = line.substring(t2 + 1);
  sha.trim();

  AssetManifestFile f{};
  strlcpy(f.path, path.c_str(), sizeof(f.path));
  f.size = (uint32_t)sizeStr.toInt();
  strlcpy(f.sha256, sha.c_str(), sizeof(f.sha256));

  *out = f;
  return true;
}

static String assetFileResolvedUrl(const String &packVersion, const AssetManifestFile &f)
{
  String url = "https://assets.raisinghellgame.com/assets/";
  if (!url.endsWith("/"))
    url += "/";

  if (packVersion.length())
  {
    url += packVersion;
    if (!url.endsWith("/"))
      url += "/";
  }

  url += f.path;
  return url;
}

static AssetOtaConfig s_cfg{};
static AssetOtaState s_state{};
static AssetOtaStatus s_status = AssetOtaStatus::IDLE;
static AssetOtaError s_lastErr = AssetOtaError::NONE;
static String s_installedVersion;
static bool s_inited = false;
static bool s_loadedFromSd = false;
static bool s_graphicsReleasedForOta = false;
static bool s_assetOtaConfirmActive = false;

bool assetOtaConfirmActive() { return s_assetOtaConfirmActive; }

void assetOtaSetConfirmActive(bool v) { s_assetOtaConfirmActive = v; }

bool assetOtaDidReleaseGraphics() { return s_graphicsReleasedForOta; }

struct AssetOtaTaskResult
{
  bool done = false;
  bool ok = false;
  String message;
};

static AssetOtaTaskResult g_otaTaskResult;
static TaskHandle_t g_otaTaskHandle = nullptr;

static void assetOtaWorkerTask(void *param)
{
  String *msg = static_cast<String *>(param);

  String localMsg;
  bool ok = assetOtaCheckNow(&localMsg);

  g_otaTaskResult.ok = ok;
  g_otaTaskResult.message = localMsg;
  g_otaTaskResult.done = true;

  if (msg)
    delete msg;

  g_otaTaskHandle = nullptr;
  vTaskDelete(nullptr);
}

bool assetOtaRunInWorkerTask(String *outMessage)
{
  g_otaTaskResult = AssetOtaTaskResult{};

  String *heapMsg = new String();
  if (!heapMsg)
  {
    if (outMessage)
      *outMessage = "No memory for OTA worker";
    return false;
  }

  BaseType_t rc = xTaskCreatePinnedToCore(assetOtaWorkerTask, "asset_ota_worker",
                                          16384, // start here; can raise to 20480 if needed
                                          heapMsg, 1, &g_otaTaskHandle, 1);

  if (rc != pdPASS)
  {
    delete heapMsg;
    if (outMessage)
      *outMessage = "Failed to start OTA worker";
    return false;
  }

  while (!g_otaTaskResult.done)
  {
    delay(10);
  }

  if (outMessage)
    *outMessage = g_otaTaskResult.message;

  return g_otaTaskResult.ok;
}

static bool localAssetMatches(const AssetManifestFile &f)
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

  String hex;
  hex.reserve(64);

  static const char *digits = "0123456789abcdef";
  for (int i = 0; i < 32; ++i)
  {
    hex += digits[(hash[i] >> 4) & 0x0F];
    hex += digits[hash[i] & 0x0F];
  }

  return hex.equalsIgnoreCase(f.sha256);
}

static const char *statusString(AssetOtaStatus st)
{
  switch (st)
  {
  case AssetOtaStatus::IDLE:
    return "Idle";
  case AssetOtaStatus::CHECKING:
    return "Checking";
  case AssetOtaStatus::DOWNLOADING:
    return "Downloading";
  case AssetOtaStatus::INSTALLING:
    return "Installing";
  case AssetOtaStatus::SUCCESS:
    return "Success";
  case AssetOtaStatus::FAILED:
    return "Failed";
  }
  return "Unknown";
}

static const char *errorString(AssetOtaError err)
{
  switch (err)
  {
  case AssetOtaError::NONE:
    return "None";
  case AssetOtaError::WIFI_DISABLED:
    return "WiFi disabled";
  case AssetOtaError::WIFI_NOT_CONNECTED:
    return "WiFi not connected";
  case AssetOtaError::SD_NOT_READY:
    return "SD not ready";
  case AssetOtaError::CONFIG_IO:
    return "Config I/O failed";
  case AssetOtaError::STATE_IO:
    return "State I/O failed";
  case AssetOtaError::HTTP_FAIL:
    return "HTTP failed";
  case AssetOtaError::JSON_FAIL:
    return "Manifest parse failed";
  case AssetOtaError::HASH_MISMATCH:
    return "SHA256 mismatch";
  case AssetOtaError::SIZE_MISMATCH:
    return "Size mismatch";
  case AssetOtaError::RENAME_FAIL:
    return "Install rename failed";
  case AssetOtaError::BAD_PATH:
    return "Bad asset path";
  case AssetOtaError::NO_MANIFEST:
    return "Manifest missing";
  case AssetOtaError::STAGING_FAIL:
    return "Staging failed";
  case AssetOtaError::WRITE_FAIL:
    return "Write failed";
  }
  return "Unknown";
}

static void restoreMainUiSprite()
{
  if (g_bootAssetProvisionActive)
    return;

  if (!s_graphicsReleasedForOta)
  {
    invalidateBackgroundCache();
    requestUIRedraw();
    renderUI();
    return;
  }

  const bool ok = spr.createSprite(SCREEN_W, SCREEN_H);
  Serial.printf("[OTA/UI] recreate main sprite ok=%d free=%u largest=%u\n", (int)ok, (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getMaxAllocHeap());

  if (!ok)
    return;

  spr.setTextScroll(false);
  spr.fillScreen(TFT_BLACK);

  s_graphicsReleasedForOta = false;

  invalidateBackgroundCache();
  requestUIRedraw();
  renderUI();
}

static void otaRedrawProvisionScreen(const char *line1, const char *line2)
{
  drawBootAssetProvisionScreen(line1, line2);
  delay(1);
}

static void setFailure(AssetOtaError err)
{
  s_lastErr = err;
  s_status = AssetOtaStatus::FAILED;
  s_state.status = (uint8_t)s_status;
  s_state.lastError = (uint8_t)err;
  s_state.totalFileCount = 0;
  assetOtaStateSave(s_state);
}

static bool removeTree(const char *path)
{
  if (!path || !path[0])
    return false;
  if (!SD.exists(path))
    return true;

  File root = SD.open(path);
  if (!root)
  {
    SD.remove(path);
    return true;
  }

  if (!root.isDirectory())
  {
    root.close();
    SD.remove(path);
    return true;
  }

  while (true)
  {
    File entry = root.openNextFile();
    if (!entry)
      break;

    String p = String(entry.name());
    const bool isDir = entry.isDirectory();
    entry.close();

    if (isDir)
      removeTree(p.c_str());
    else
      SD.remove(p.c_str());
  }

  root.close();
  SD.rmdir(path);
  return true;
}

static bool installStagedFile(const String &relPath, const String &stagingPath)
{
  String livePath = "/";
  livePath += relPath;

  if (!assetOtaEnsureParentDir(livePath.c_str()))
    return false;

  if (SD.exists(livePath.c_str()))
    SD.remove(livePath.c_str());

  if (!SD.rename(stagingPath.c_str(), livePath.c_str()))
    return false;

  return true;
}

static void loadAssetOtaFromSdIfAvailable()
{
  if (!g_sdReady)
    return;

  assetOtaEnsureCoreDirs();
  (void)assetOtaConfigLoad(&s_cfg);
  (void)assetOtaStateLoad(&s_state);

  String packVersion;
  if (assetManifestLoadLocalPackVersion(&packVersion))
    s_installedVersion = packVersion;
  else
    s_installedVersion = "";

  if (s_state.inProgress)
  {
    Serial.println("[OTA] detected interrupted OTA session; preserving inProgress state for boot recovery");
  }

  s_loadedFromSd = true;
}

void assetOtaInit()
{
  if (s_inited)
    return;

  assetOtaConfigDefaults(s_cfg);
  assetOtaStateDefaults(s_state);
  s_installedVersion = "";

  if (g_sdReady)
  {
    loadAssetOtaFromSdIfAvailable();

    String packVersion;
    if (assetManifestLoadLocalPackVersion(&packVersion) && packVersion.length() > 0)
      s_installedVersion = packVersion;
  }

  s_status = AssetOtaStatus::IDLE;
  s_lastErr = AssetOtaError::NONE;
  s_inited = true;
}

void assetOtaTick()
{
  if (!s_inited)
    assetOtaInit();

  if (!s_loadedFromSd && g_sdReady)
    loadAssetOtaFromSdIfAvailable();
}

const AssetOtaConfig &assetOtaGetConfig()
{
  if (!s_inited)
    assetOtaInit();

  if (!s_loadedFromSd && g_sdReady)
    loadAssetOtaFromSdIfAvailable();

  return s_cfg;
}

bool assetOtaSetAutoCheckEnabled(bool en)
{
  if (!s_inited)
    assetOtaInit();

  if (!s_loadedFromSd && g_sdReady)
    loadAssetOtaFromSdIfAvailable();

  s_cfg.autoCheckEnabled = en ? 1 : 0;
  return assetOtaConfigSave(s_cfg);
}

bool assetOtaSetChannel(AssetOtaChannel ch)
{
  if (!s_inited)
    assetOtaInit();

  if (!s_loadedFromSd && g_sdReady)
    loadAssetOtaFromSdIfAvailable();

  s_cfg.channel = (uint8_t)ch;
  return assetOtaConfigSave(s_cfg);
}

const char *assetOtaInstalledVersion()
{
  if (!s_inited)
    assetOtaInit();

  if (!s_loadedFromSd && g_sdReady)
    loadAssetOtaFromSdIfAvailable();

  return s_installedVersion.c_str();
}
AssetOtaStatus assetOtaStatus() { return s_status; }
AssetOtaError assetOtaLastError() { return s_lastErr; }
uint16_t assetOtaCurrentFileIndex() { return s_state.currentFileIndex; }
uint16_t assetOtaTotalFileCount() { return s_state.totalFileCount; }
const char *assetOtaStatusString() { return statusString(s_status); }
const char *assetOtaLastErrorString() { return errorString(s_lastErr); }

bool assetOtaCheckNow(String *outMessage)
{
  if (outMessage)
    *outMessage = "";

  if (!s_inited)
    assetOtaInit();

  if (!s_loadedFromSd && g_sdReady)
    loadAssetOtaFromSdIfAvailable();

  s_graphicsReleasedForOta = false;

  if (!g_sdReady)
  {
    setFailure(AssetOtaError::SD_NOT_READY);
    if (outMessage)
      *outMessage = "SD not ready";
    restoreMainUiSprite();
    return false;
  }

  if (!wifiIsEnabled())
  {
    setFailure(AssetOtaError::WIFI_DISABLED);
    if (outMessage)
      *outMessage = "Enable WiFi first";
    restoreMainUiSprite();
    return false;
  }

  if (!wifiIsConnectedNow())
  {
    setFailure(AssetOtaError::WIFI_NOT_CONNECTED);
    if (outMessage)
      *outMessage = "Connect WiFi first";
    restoreMainUiSprite();
    return false;
  }

  if (!assetOtaEnsureCoreDirs())
  {
    setFailure(AssetOtaError::CONFIG_IO);
    if (outMessage)
      *outMessage = "OTA dirs failed";
    restoreMainUiSprite();
    return false;
  }

  removeTree(assetOtaStagingRoot());
  assetOtaEnsureCoreDirs();

  s_lastErr = AssetOtaError::NONE;
  s_status = AssetOtaStatus::CHECKING;
  s_state.inProgress = 1;
  s_state.status = (uint8_t)s_status;
  s_state.lastError = (uint8_t)AssetOtaError::NONE;
  s_state.currentFileIndex = 0;
  s_state.targetPackVersion[0] = '\0';
  assetOtaStateSave(s_state);
  otaRedrawProvisionScreen("Checking assets...", "Downloading manifest...");

  Serial.printf("[OTA DBG] loadedFromSd=%d channel=%u\n", s_loadedFromSd ? 1 : 0, (unsigned)s_cfg.channel);

  const AssetOtaChannel ch = (AssetOtaChannel)s_cfg.channel;
  const char *manifestUrl = assetOtaManifestUrlForChannel(ch);

  Serial.printf("[OTA DBG] manifestUrl=%s\n", manifestUrl);
  if (!manifestUrl || !manifestUrl[0])
  {
    setFailure(AssetOtaError::NO_MANIFEST);
    if (outMessage)
      *outMessage = "Manifest URL missing";
    restoreMainUiSprite();
    return false;
  }

  AssetManifestData localManifest;

  Serial.printf("[OTA] before cleanup free=%u largest=%u\n", (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getMaxAllocHeap());

  graphicsReleasePetLayerForOta();
  invalidateBackgroundCache();
  spr.deleteSprite();
  s_graphicsReleasedForOta = true;

  Serial.printf("[OTA] after cleanup free=%u largest=%u\n", (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getMaxAllocHeap());

  String remotePackVersion;
  uint16_t changedCount = 0;

  if (!assetManifestBuildWorklistFromRemote(manifestUrl, localManifest, &remotePackVersion, &changedCount))
  {
    setFailure(AssetOtaError::JSON_FAIL);
    if (outMessage)
      *outMessage = "Manifest download/parse failed";
    restoreMainUiSprite();
    return false;
  }

  strncpy(s_state.targetPackVersion, remotePackVersion.c_str(), sizeof(s_state.targetPackVersion) - 1);
  s_state.targetPackVersion[sizeof(s_state.targetPackVersion) - 1] = '\0';
  s_state.totalFileCount = changedCount;
  assetOtaStateSave(s_state);

  otaRedrawProvisionScreen("Preparing downloads...", "Building file list...");

  if (remotePackVersion == s_installedVersion && changedCount == 0)
  {
    Serial.printf("[OTA] already current; installed=%s remote=%s\n", s_installedVersion.c_str(),
                  remotePackVersion.c_str());
    s_status = AssetOtaStatus::SUCCESS;
    assetOtaStateDefaults(s_state);
    assetOtaStateSave(s_state);

    if (outMessage)
    {
      *outMessage = "Assets already up to date (";
      *outMessage += remotePackVersion;
      *outMessage += ")";
    }

    restoreMainUiSprite();
    return true;
  }

  if (changedCount == 0)
  {
    Serial.printf("[OTA] no file changes; adopting version=%s\n", remotePackVersion.c_str());
    s_installedVersion = remotePackVersion;
    s_status = AssetOtaStatus::SUCCESS;
    assetOtaStateDefaults(s_state);
    assetOtaStateSave(s_state);

    if (outMessage)
    {
      *outMessage = "Asset OTA ";
      *outMessage += remotePackVersion;
      *outMessage += " already installed";
    }

    restoreMainUiSprite();
    return true;
  }

  File work;
  if (!assetOtaWorklistOpenRead(&work))
  {
    setFailure(AssetOtaError::STATE_IO);
    if (outMessage)
      *outMessage = "Failed to open OTA worklist";
    restoreMainUiSprite();
    return false;
  }

  AssetManifestFile mf{};
  uint16_t i = 0;

  while (assetOtaWorklistReadNext(work, &mf))
  {
    ++i;

    s_status = AssetOtaStatus::DOWNLOADING;
    s_state.status = (uint8_t)s_status;
    s_state.currentFileIndex = i;
    assetOtaStateSave(s_state);

    otaRedrawProvisionScreen("Downloading assets...", mf.path);

    String rawPath(mf.path);
    String rel;
    if (!assetManifestNormalizePath(rawPath, &rel))
    {
      Serial.printf("[OTA] skipping invalid manifest path: %s\n", rawPath.c_str());
      continue;
    }

    AssetManifestFile dlFile{};
    strlcpy(dlFile.path, rawPath.c_str(), sizeof(dlFile.path));
    strlcpy(dlFile.sha256, mf.sha256, sizeof(dlFile.sha256));
    dlFile.size = mf.size;

    String stagingPath;
    String dlErr;
    bool dlOk = false;

    for (int attempt = 1; attempt <= 3; ++attempt)
    {
      stagingPath = "";
      dlErr = "";

      Serial.printf("[OTA] file attempt %d/3: %s\n", attempt, rawPath.c_str());

      String url = assetFileResolvedUrl(remotePackVersion, dlFile);
      if (assetDownloadToStaging(url, dlFile, &stagingPath, &dlErr))
      {
        dlOk = true;
        break;
      }

      Serial.printf("[OTA] file attempt failed: %s\n", dlErr.c_str());

      delay(500);

      uint32_t waitStart = millis();
      while (!wifiIsConnectedNow() && (millis() - waitStart) < 5000)
        delay(100);
    }

    if (!dlOk)
    {
      work.close();

      if (dlErr.indexOf("SHA256") >= 0)
        setFailure(AssetOtaError::HASH_MISMATCH);
      else if (dlErr.indexOf("size") >= 0 || dlErr.indexOf("Size") >= 0)
        setFailure(AssetOtaError::SIZE_MISMATCH);
      else
        setFailure(AssetOtaError::HTTP_FAIL);

      if (outMessage)
        *outMessage = dlErr;
      restoreMainUiSprite();
      return false;
    }

    s_status = AssetOtaStatus::INSTALLING;
    s_state.status = (uint8_t)s_status;
    assetOtaStateSave(s_state);

    otaRedrawProvisionScreen("Installing asset...", rel.c_str());

    if (!installStagedFile(rel, stagingPath))
    {
      work.close();
      setFailure(AssetOtaError::RENAME_FAIL);
      if (outMessage)
        *outMessage = "Install failed";
      restoreMainUiSprite();
      return false;
    }

    delay(25);
  }

  work.close();

  const char *tmpManifestPath = "/manifest.remote.tmp";
  const char *localManifestPath = assetOtaLocalManifestPath();

  if (!SD.exists(tmpManifestPath))
  {
    setFailure(AssetOtaError::WRITE_FAIL);
    if (outMessage)
      *outMessage = "Final manifest temp file missing";
    restoreMainUiSprite();
    return false;
  }

  if (!assetOtaEnsureParentDir(localManifestPath))
  {
    setFailure(AssetOtaError::WRITE_FAIL);
    if (outMessage)
      *outMessage = "Local manifest dir failed";
    restoreMainUiSprite();
    return false;
  }

  if (SD.exists(localManifestPath))
    SD.remove(localManifestPath);

  bool saveOk = SD.rename(tmpManifestPath, localManifestPath);

  if (!saveOk)
  {
    File src = SD.open(tmpManifestPath, FILE_READ);
    File dst = SD.open(localManifestPath, FILE_WRITE);

    if (src && dst)
    {
      uint8_t buf[1024];
      while (true)
      {
        int n = src.read(buf, sizeof(buf));
        if (n <= 0)
          break;
        if (dst.write(buf, n) != (size_t)n)
        {
          saveOk = false;
          break;
        }
        saveOk = true;
      }
      dst.flush();
    }
    else
    {
      saveOk = false;
    }

    if (src)
      src.close();
    if (dst)
      dst.close();

    if (saveOk)
      SD.remove(tmpManifestPath);
  }

  Serial.printf("[OTA] final manifest promote result=%d path=%s\n", (int)saveOk, localManifestPath);

  if (!saveOk)
  {
    setFailure(AssetOtaError::WRITE_FAIL);
    if (outMessage)
      *outMessage = "Local manifest write failed";
    restoreMainUiSprite();
    return false;
  }

  s_installedVersion = remotePackVersion;
  s_status = AssetOtaStatus::SUCCESS;

  assetOtaStateDefaults(s_state);
  assetOtaStateSave(s_state);

  Serial.printf("[OTA] final state save: inProgress=%u status=%u err=%u\n", (unsigned)s_state.inProgress,
                (unsigned)s_state.status, (unsigned)s_state.lastError);

  Serial.printf("[OTA] install success; version=%s files=%u\n", remotePackVersion.c_str(), (unsigned)changedCount);

  if (outMessage)
  {
    *outMessage = "Asset OTA ";
    *outMessage += remotePackVersion;
    *outMessage += " installed (";
    *outMessage += (int)changedCount;
    *outMessage += " file";
    if (changedCount != 1)
      *outMessage += "s";
    *outMessage += ")";
  }

  return true;
}