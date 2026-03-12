#include <Arduino.h>
#include <SD.h>
#include <string.h>
#include "asset_ota_config.h"
#include "asset_ota_types.h"
#include "sdcard.h"

static const char *kCfgPath = "/raising_hell/save/asset_ota_cfg.bin";
static const char *kCfgTmpPath = "/raising_hell/save/asset_ota_cfg.tmp";
static const char *kStatePath = "/raising_hell/save/asset_ota_state.bin";
static const char *kStateTmpPath = "/raising_hell/save/asset_ota_state.tmp";

static const char *kLocalManifestPath = "/raising_hell/assets/manifest_local.json";
static const char *kLocalManifestTmpPath = "/raising_hell/assets/manifest_local.tmp";
static const char *kStagingRoot = "/raising_hell/assets_staging";
static const char *kLegacyMarkerPath = "/raising_hell/ASSET_MANIFEST.txt";

static const char *kPublicManifestUrl = "https://example.com/raising_hell/manifest-public.json";
static const char *kDevManifestUrl = "https://example.com/raising_hell/manifest-dev.json";

const char *assetOtaConfigPath() { return kCfgPath; }
const char *assetOtaConfigTmpPath() { return kCfgTmpPath; }
const char *assetOtaStatePath() { return kStatePath; }
const char *assetOtaStateTmpPath() { return kStateTmpPath; }
const char *assetOtaLocalManifestPath() { return kLocalManifestPath; }
const char *assetOtaLocalManifestTmpPath() { return kLocalManifestTmpPath; }
const char *assetOtaStagingRoot() { return kStagingRoot; }
const char *assetOtaLegacyMarkerPath() { return kLegacyMarkerPath; }

const char *assetOtaManifestUrlForChannel(AssetOtaChannel ch)
{
  return (ch == AssetOtaChannel::DEV) ? kDevManifestUrl : kPublicManifestUrl;
}

void assetOtaConfigDefaults(AssetOtaConfig &cfg)
{
  cfg = AssetOtaConfig{};
  cfg.autoCheckEnabled = 0;
  cfg.channel = (uint8_t)AssetOtaChannel::PUBLIC;
}

void assetOtaStateDefaults(AssetOtaState &st)
{
  st = AssetOtaState{};
  st.inProgress = 0;
  st.status = (uint8_t)AssetOtaStatus::IDLE;
  st.lastError = (uint8_t)AssetOtaError::NONE;
  st.currentFileIndex = 0;
  st.targetPackVersion[0] = '\0';
}

static bool ensureDir(const char *path)
{
  if (!path || !path[0])
    return false;
  if (SD.exists(path))
    return true;
  return SD.mkdir(path);
}

bool assetOtaEnsureCoreDirs()
{
  if (!g_sdReady)
    return false;

  if (!ensureDir("/raising_hell"))
    return false;
  if (!ensureDir("/raising_hell/save"))
    return false;
  if (!ensureDir("/raising_hell/assets"))
    return false;
  if (!ensureDir(kStagingRoot))
    return false;

  return true;
}

bool assetOtaEnsureParentDir(const char *fullPath)
{
  if (!g_sdReady || !fullPath || fullPath[0] != '/')
    return false;

  if (!assetOtaEnsureCoreDirs())
    return false;

  String cur;
  const size_t len = strlen(fullPath);
  for (size_t i = 0; i < len; ++i)
  {
    const char c = fullPath[i];
    cur += c;
    if (c != '/')
      continue;

    if (cur.length() <= 1)
      continue;

    if (!SD.exists(cur.c_str()))
    {
      if (!SD.mkdir(cur.c_str()))
        return false;
    }
  }

  const int slash = String(fullPath).lastIndexOf('/');
  if (slash <= 0)
    return true;

  const String parent = String(fullPath).substring(0, slash);
  if (parent.isEmpty())
    return true;
  if (SD.exists(parent.c_str()))
    return true;
  return SD.mkdir(parent.c_str());
}

static bool writeAtomic(const char *tmpPath, const char *livePath, const uint8_t *data, size_t len)
{
  if (!g_sdReady)
    return false;
  if (!assetOtaEnsureParentDir(tmpPath) || !assetOtaEnsureParentDir(livePath))
    return false;

  if (SD.exists(tmpPath))
    SD.remove(tmpPath);

  File f = SD.open(tmpPath, FILE_WRITE);
  if (!f)
    return false;

  const size_t w = f.write(data, len);
  f.flush();
  f.close();

  if (w != len)
  {
    SD.remove(tmpPath);
    return false;
  }

  if (SD.exists(livePath))
    SD.remove(livePath);

  if (!SD.rename(tmpPath, livePath))
  {
    SD.remove(tmpPath);
    return false;
  }

  return true;
}

bool assetOtaConfigLoad(AssetOtaConfig *outCfg)
{
  if (!outCfg)
    return false;

  assetOtaConfigDefaults(*outCfg);

  if (!g_sdReady)
    return false;
  if (!assetOtaEnsureCoreDirs())
    return false;
  if (!SD.exists(kCfgPath))
    return false;

  File f = SD.open(kCfgPath, FILE_READ);
  if (!f)
    return false;

  if ((size_t)f.size() != sizeof(AssetOtaConfig))
  {
    f.close();
    return false;
  }

  AssetOtaConfig tmp{};
  const int r = f.read((uint8_t *)&tmp, sizeof(tmp));
  f.close();

  if (r != (int)sizeof(tmp))
    return false;
  if (tmp.magic != 0x41544346UL || tmp.version != 1)
    return false;
  if (tmp.channel > (uint8_t)AssetOtaChannel::DEV)
    tmp.channel = (uint8_t)AssetOtaChannel::PUBLIC;

  *outCfg = tmp;
  return true;
}

bool assetOtaConfigSave(const AssetOtaConfig &cfg)
{
  return writeAtomic(kCfgTmpPath, kCfgPath, (const uint8_t *)&cfg, sizeof(cfg));
}

bool assetOtaStateLoad(AssetOtaState *outState)
{
  if (!outState)
    return false;

  assetOtaStateDefaults(*outState);

  if (!g_sdReady)
    return false;
  if (!assetOtaEnsureCoreDirs())
    return false;
  if (!SD.exists(kStatePath))
    return false;

  File f = SD.open(kStatePath, FILE_READ);
  if (!f)
    return false;

  if ((size_t)f.size() != sizeof(AssetOtaState))
  {
    f.close();
    return false;
  }

  AssetOtaState tmp{};
  const int r = f.read((uint8_t *)&tmp, sizeof(tmp));
  f.close();

  if (r != (int)sizeof(tmp))
    return false;
  if (tmp.magic != 0x41545354UL || tmp.version != 1)
    return false;

  *outState = tmp;
  return true;
}

bool assetOtaStateSave(const AssetOtaState &st)
{
  return writeAtomic(kStateTmpPath, kStatePath, (const uint8_t *)&st, sizeof(st));
}