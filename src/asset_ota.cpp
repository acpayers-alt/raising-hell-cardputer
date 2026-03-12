#include "asset_ota.h"

#include "asset_downloader.h"
#include "asset_manifest.h"
#include "asset_ota_config.h"
#include "display.h"
#include "graphics.h"
#include "sdcard.h"
#include "wifi_time.h"
#include <Arduino.h>
#include <SD.h>
#include <string.h>
#include "ui_invalidate.h"

static AssetOtaConfig s_cfg{};
static AssetOtaState s_state{};
static AssetOtaStatus s_status = AssetOtaStatus::IDLE;
static AssetOtaError s_lastErr = AssetOtaError::NONE;
static String s_installedVersion;
static bool s_inited = false;

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
  spr.deleteSprite();
  spr.createSprite(SCREEN_W, SCREEN_H);
  spr.setTextScroll(false);
  spr.fillScreen(TFT_BLACK);
  invalidateBackgroundCache();
  requestUIRedraw();
}

static void setFailure(AssetOtaError err)
{
  s_lastErr = err;
  s_status = AssetOtaStatus::FAILED;
  s_state.status = (uint8_t)s_status;
  s_state.lastError = (uint8_t)err;
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

void assetOtaInit()
{
  if (s_inited)
    return;

  assetOtaConfigDefaults(s_cfg);
  assetOtaStateDefaults(s_state);

  if (g_sdReady)
  {
    assetOtaEnsureCoreDirs();
    (void)assetOtaConfigLoad(&s_cfg);
    (void)assetOtaStateLoad(&s_state);

    AssetManifestData local;
    if (assetManifestLoadLocal(&local))
      s_installedVersion = local.packVersion;

    if (s_state.inProgress)
    {
      removeTree(assetOtaStagingRoot());
      assetOtaEnsureCoreDirs();
      assetOtaStateDefaults(s_state);
      assetOtaStateSave(s_state);
    }
  }

  s_status = AssetOtaStatus::IDLE;
  s_lastErr = AssetOtaError::NONE;
  s_inited = true;
}

void assetOtaTick()
{
  if (!s_inited)
    assetOtaInit();
}

const AssetOtaConfig &assetOtaGetConfig()
{
  if (!s_inited)
    assetOtaInit();
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

const char *assetOtaStatusString() { return statusString(s_status); }

const char *assetOtaLastErrorString() { return errorString(s_lastErr); }

bool assetOtaCheckNow(String *outMessage)
{
  if (outMessage)
    *outMessage = "";

  if (!s_inited)
    assetOtaInit();

  Serial.printf("[OTA] before cleanup free=%u largest=%u\n", (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getMaxAllocHeap());

  invalidateBackgroundCache();
  spr.deleteSprite();

  Serial.printf("[OTA] after cleanup free=%u largest=%u\n", (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getMaxAllocHeap());

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

  AssetManifestData remoteManifest;
  if (!assetManifestDownloadRemote(manifestUrl, &remoteManifest))
  {
    setFailure(AssetOtaError::JSON_FAIL);
    if (outMessage)
      *outMessage = "Remote manifest failed";
    restoreMainUiSprite();
    return false;
  }

  strncpy(s_state.targetPackVersion, remoteManifest.packVersion.c_str(), sizeof(s_state.targetPackVersion) - 1);
  s_state.targetPackVersion[sizeof(s_state.targetPackVersion) - 1] = '\0';
  assetOtaStateSave(s_state);

  std::vector<AssetManifestFile> changed;
  assetManifestBuildDiff(localManifest, remoteManifest, changed);

  if (changed.empty())
  {
    (void)assetManifestSaveLocal(remoteManifest);
    s_installedVersion = remoteManifest.packVersion;
    s_status = AssetOtaStatus::SUCCESS;
    assetOtaStateDefaults(s_state);
    assetOtaStateSave(s_state);
    if (outMessage)
    {
      *outMessage = "Asset OTA ";
      *outMessage += remoteManifest.packVersion;
      *outMessage += " already installed";
    }
    restoreMainUiSprite();
    return true;
  }

  for (size_t i = 0; i < changed.size(); ++i)
  {
    s_status = AssetOtaStatus::DOWNLOADING;
    s_state.status = (uint8_t)s_status;
    s_state.currentFileIndex = (uint16_t)i;
    assetOtaStateSave(s_state);

    String rel;
    if (!assetManifestNormalizePath(changed[i].path, &rel))
    {
      setFailure(AssetOtaError::BAD_PATH);
      if (outMessage)
        *outMessage = "Bad manifest path";
      restoreMainUiSprite();
      return false;
    }

    String stagingPath;
    String dlErr;
    if (!assetDownloadToStaging(changed[i], &stagingPath, &dlErr))
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

    s_status = AssetOtaStatus::INSTALLING;
    s_state.status = (uint8_t)s_status;
    assetOtaStateSave(s_state);

    if (!installStagedFile(rel, stagingPath))
    {
      setFailure(AssetOtaError::RENAME_FAIL);
      if (outMessage)
        *outMessage = "Install failed";
      restoreMainUiSprite();
      return false;
    }
  }

  if (!assetManifestSaveLocal(remoteManifest))
  {
    setFailure(AssetOtaError::WRITE_FAIL);
    if (outMessage)
      *outMessage = "Local manifest write failed";
    restoreMainUiSprite();
    return false;
  }

  (void)writeLegacyMarker(remoteManifest.packVersion.c_str());

  s_installedVersion = remoteManifest.packVersion;
  s_status = AssetOtaStatus::SUCCESS;
  assetOtaStateDefaults(s_state);
  assetOtaStateSave(s_state);

  if (outMessage)
  {
    *outMessage = "Asset OTA ";
    *outMessage += remoteManifest.packVersion;
    *outMessage += " installed (";
    *outMessage += (int)changed.size();
    *outMessage += " file";
    if (changed.size() != 1)
      *outMessage += "s";
    *outMessage += ")";
  }

  restoreMainUiSprite();
  return true;
}