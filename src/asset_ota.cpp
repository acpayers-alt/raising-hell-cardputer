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
#include <string.h>
#include <vector>

static String assetFileResolvedUrl(const AssetManifestFile &f)
{
  String url = "https://assets.raisinghellgame.com/assets/";
  if (!url.endsWith("/"))
    url += "/";
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

static bool writeLegacyMarker(const char *version)
{
  if (!assetOtaEnsureParentDir(assetOtaLegacyMarkerPath()))
    return false;

  if (SD.exists(assetOtaLegacyMarkerPath()))
    SD.remove(assetOtaLegacyMarkerPath());

  File f = SD.open(assetOtaLegacyMarkerPath(), FILE_WRITE);
  if (!f)
    return false;

  f.print(version ? version : "unknown");
  f.print("\n");
  f.close();
  return true;
}

static void loadAssetOtaFromSdIfAvailable()
{
  if (!g_sdReady)
    return;

  assetOtaEnsureCoreDirs();
  (void)assetOtaConfigLoad(&s_cfg);
  (void)assetOtaStateLoad(&s_state);

  AssetManifestData local;
  if (assetManifestLoadLocal(&local))
    s_installedVersion = local.packVersion;
  else
    s_installedVersion = "";

  if (s_state.inProgress)
  {
    removeTree(assetOtaStagingRoot());
    assetOtaEnsureCoreDirs();
    assetOtaStateDefaults(s_state);
    assetOtaStateSave(s_state);
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

    AssetManifestData localManifest;
    if (assetManifestLoadLocal(&localManifest) && localManifest.packVersion.length() > 0)
      s_installedVersion = localManifest.packVersion;
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
  s_cfg.autoCheckEnabled = en ? 1 : 0;
  return assetOtaConfigSave(s_cfg);
}

bool assetOtaSetChannel(AssetOtaChannel ch)
{
  if (!s_inited)
    assetOtaInit();
  s_cfg.channel = (uint8_t)ch;
  return assetOtaConfigSave(s_cfg);
}

const char *assetOtaInstalledVersion() { return s_installedVersion.c_str(); }
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

  const AssetOtaChannel ch = (AssetOtaChannel)s_cfg.channel;
  const char *manifestUrl = assetOtaManifestUrlForChannel(ch);
  if (!manifestUrl || !manifestUrl[0])
  {
    setFailure(AssetOtaError::NO_MANIFEST);
    if (outMessage)
      *outMessage = "Manifest URL missing";
    restoreMainUiSprite();
    return false;
  }

  AssetManifestData localManifest;
  (void)assetManifestLoadLocal(&localManifest);

  Serial.printf("[OTA] before cleanup free=%u largest=%u\n", (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getMaxAllocHeap());

  graphicsReleasePetLayerForOta();
  invalidateBackgroundCache();
  spr.deleteSprite();
  s_graphicsReleasedForOta = true;

  Serial.printf("[OTA] after cleanup free=%u largest=%u\n", (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getMaxAllocHeap());

  String remotePackVersion;
  std::vector<AssetManifestFile> changed;

  if (!assetManifestDownloadDiffOnly(manifestUrl, localManifest, &remotePackVersion, changed))
  {
    setFailure(AssetOtaError::JSON_FAIL);
    if (outMessage)
      *outMessage = "Manifest download/parse failed";
    restoreMainUiSprite();
    return false;
  }

  Serial.printf("[OTA] changed.size=%u\n", (unsigned)changed.size());

  for (size_t k = 0; k < changed.size(); ++k)
  {
    const AssetManifestFile &f = changed[k];
    const size_t pathLen = strlen(f.path);
    const size_t shaLen = strlen(f.sha256);

    Serial.printf("[OTA] verify[%u] pathLen=%u size=%u shaLen=%u\n", (unsigned)k, (unsigned)pathLen, (unsigned)f.size,
                  (unsigned)shaLen);

    if (pathLen == 0)
    {
      Serial.printf("[OTA] BAD ENTRY[%u]: empty path\n", (unsigned)k);
      setFailure(AssetOtaError::JSON_FAIL);
      if (outMessage)
        *outMessage = "Manifest contains empty path";
      restoreMainUiSprite();
      return false;
    }

    if (shaLen > 0 && shaLen != 64)
    {
      Serial.printf("[OTA] BAD ENTRY[%u]: bad sha len=%u path=%s\n", (unsigned)k, (unsigned)shaLen, f.path);
      setFailure(AssetOtaError::JSON_FAIL);
      if (outMessage)
        *outMessage = "Manifest contains invalid sha256";
      restoreMainUiSprite();
      return false;
    }

    if (k >= 255 && k <= 270)
    {
      Serial.printf("[OTA] ENTRY[%u] path=%s sha=%s\n", (unsigned)k, f.path, shaLen ? f.sha256 : "(none)");
    }
  }

  strncpy(s_state.targetPackVersion, remotePackVersion.c_str(), sizeof(s_state.targetPackVersion) - 1);
  s_state.targetPackVersion[sizeof(s_state.targetPackVersion) - 1] = '\0';
  s_state.totalFileCount = (uint16_t)changed.size();
  assetOtaStateSave(s_state);

  otaRedrawProvisionScreen("Preparing downloads...", "Building file list...");

  if (remotePackVersion == s_installedVersion && changed.empty())
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

  if (changed.empty())
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

  for (size_t i = 0; i < changed.size(); ++i)
  {
    Serial.printf("[OTA] loop-begin i=%u\n", (unsigned)i);

    const AssetManifestFile &mf = changed[i];

    s_status = AssetOtaStatus::DOWNLOADING;
    s_state.status = (uint8_t)s_status;
    s_state.currentFileIndex = (uint16_t)(i + 1);
    assetOtaStateSave(s_state);

    otaRedrawProvisionScreen("Downloading assets...", mf.path);
    String rawPath(mf.path);

    if (rawPath.equalsIgnoreCase("raising_hell/ASSET_MANIFEST.txt") ||
        rawPath.equalsIgnoreCase("/raising_hell/ASSET_MANIFEST.txt"))
    {
      Serial.println("[OTA] skipping legacy marker from remote manifest");
      continue;
    }

    Serial.printf("[OTA] normalize candidate path='%s'\n", rawPath.c_str());

    String rel;
    if (!assetManifestNormalizePath(rawPath, &rel))
    {
      Serial.printf("[OTA] skipping invalid manifest path: %s\n", rawPath.c_str());
      continue;
    }

    String resolvedUrl = assetFileResolvedUrl(mf);

    Serial.printf("[OTA] download index=%u/%u path=%s\n", (unsigned)(i + 1), (unsigned)changed.size(), rawPath.c_str());
    Serial.printf("[OTA] resolved url=%s\n", resolvedUrl.c_str());

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

      if (assetDownloadToStaging(dlFile, &stagingPath, &dlErr))
      {
        dlOk = true;
        break;
      }

      Serial.printf("[OTA] file attempt failed: %s\n", dlErr.c_str());

      delay(500);

      uint32_t waitStart = millis();
      while (!wifiIsConnectedNow() && (millis() - waitStart) < 5000)
      {
        delay(100);
      }
    }

    if (!dlOk)
    {
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

    Serial.printf("[OTA] pre-install-state idx=%u path=%s\n", (unsigned)(i + 1), rel.c_str());

    s_status = AssetOtaStatus::INSTALLING;
    s_state.status = (uint8_t)s_status;

    Serial.println("[OTA] pre-state-save");
    assetOtaStateSave(s_state);
    Serial.println("[OTA] post-state-save");

    otaRedrawProvisionScreen("Installing asset...", rel.c_str());

    Serial.printf("[OTA] pre-install-staged live=%s staging=%s\n", rel.c_str(), stagingPath.c_str());

    const bool installOk = installStagedFile(rel, stagingPath);

    Serial.printf("[OTA] post-install-staged ok=%d\n", (int)installOk);

    if (!installOk)
    {
      setFailure(AssetOtaError::RENAME_FAIL);
      if (outMessage)
        *outMessage = "Install failed";
      restoreMainUiSprite();
      return false;
    }

    Serial.println("[OTA] pre-delay");
    delay(25);
    Serial.println("[OTA] post-delay");
  }

  AssetManifestData finalRemoteManifest;
  if (!assetManifestDownloadRemote(manifestUrl, &finalRemoteManifest))
  {
    setFailure(AssetOtaError::WRITE_FAIL);
    if (outMessage)
      *outMessage = "Final manifest reload failed";
    restoreMainUiSprite();
    return false;
  }

  if (!assetManifestSaveLocal(finalRemoteManifest))
  {
    setFailure(AssetOtaError::WRITE_FAIL);
    if (outMessage)
      *outMessage = "Local manifest write failed";
    restoreMainUiSprite();
    return false;
  }

  (void)writeLegacyMarker(remotePackVersion.c_str());

  s_installedVersion = remotePackVersion;
  s_status = AssetOtaStatus::SUCCESS;
  assetOtaStateDefaults(s_state);
  assetOtaStateSave(s_state);

  Serial.printf("[OTA] install success; version=%s files=%u\n", remotePackVersion.c_str(), (unsigned)changed.size());

  if (outMessage)
  {
    *outMessage = "Asset OTA ";
    *outMessage += remotePackVersion;
    *outMessage += " installed (";
    *outMessage += (int)changed.size();
    *outMessage += " file";
    if (changed.size() != 1)
      *outMessage += "s";
    *outMessage += ")";
  }

  return true;
}