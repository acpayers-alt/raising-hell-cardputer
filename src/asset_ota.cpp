// -----------------------------------------------------------------------------
// asset_ota.cpp
// Asset OTA implementation
// -----------------------------------------------------------------------------

#include "asset_ota.h"

#include <Arduino.h>
#include <SD.h>
#include <mbedtls/sha256.h>
#include <string.h>
#include <vector>

#include "asset_downloader.h"
#include "asset_manifest.h"
#include "asset_ota_config.h"
#include "boot_pipeline.h"
#include "build_flags.h"
#include "display.h"
#include "graphics.h"
#include "sdcard.h"
#include "ui_invalidate.h"
#include "ui_runtime.h"
#include "version.h"
#include "wifi_time.h"

static AssetOtaProgress s_progress;

static constexpr bool kLogVerifySuccess = false;
static constexpr bool kLogInstallVerbose = false;
static bool s_otaDidInstallFiles = false;

const AssetOtaProgress &assetOtaGetProgress() { return s_progress; }

// -----------------------------------------------------------------------------
// Worklist helpers
// -----------------------------------------------------------------------------
static bool parseSemver3Local(const String &v, int &maj, int &min, int &pat)
{
  const int p1 = v.indexOf('.');
  const int p2 = (p1 >= 0) ? v.indexOf('.', p1 + 1) : -1;

  if (p1 <= 0 || p2 <= p1)
    return false;

  String a = v.substring(0, p1);
  String b = v.substring(p1 + 1, p2);
  String c = v.substring(p2 + 1);
  a.trim();
  b.trim();
  c.trim();

  if (!a.length() || !b.length() || !c.length())
    return false;

  maj = a.toInt();
  min = b.toInt();
  pat = c.toInt();
  return true;
}

static int compareSemver3Local(const String &lhs, const String &rhs)
{
  int lMaj, lMin, lPat;
  int rMaj, rMin, rPat;

  if (!parseSemver3Local(lhs, lMaj, lMin, lPat))
    return -1;
  if (!parseSemver3Local(rhs, rMaj, rMin, rPat))
    return 1;

  if (lMaj != rMaj)
    return (lMaj < rMaj) ? -1 : 1;
  if (lMin != rMin)
    return (lMin < rMin) ? -1 : 1;
  if (lPat != rPat)
    return (lPat < rPat) ? -1 : 1;
  return 0;
}

const char *assetOtaWorklistPath() { return "/raising_hell/ota/worklist.txt"; }

bool assetOtaWorklistClear()
{
  if (SD.exists(assetOtaWorklistPath()))
    SD.remove(assetOtaWorklistPath());
  return true;
}

bool assetOtaWorklistOpenRead(File *out)
{
  if (!out)
    return false;
  *out = SD.open(assetOtaWorklistPath(), FILE_READ);
  return (bool)(*out);
}

// -----------------------------------------------------------------------------
// Worklist parsing
// -----------------------------------------------------------------------------

bool assetOtaWorklistReadNext(File &in, AssetManifestFile *out)
{
  if (!out)
  {
    Serial.println("[OTA WL READ] fail: null out");
    return false;
  }

  String line;
  line.reserve(256);

  while (true)
  {
    line = "";

    // read line
    while (true)
    {
      int c = in.read();
      if (c < 0)
        break;
      if (c == '\n')
        break;

      if (line.length() >= 255)
      {
        Serial.printf("[OTA WL READ] ERROR: line too long pos=%ld size=%u\n", (long)in.position(), (unsigned)in.size());
        return false;
      }

      line += (char)c;
    }

    if (line.length() == 0)
    {
      Serial.printf("[OTA WL READ] stop: empty read pos=%ld size=%u\n", (long)in.position(), (unsigned)in.size());
      return false;
    }

    line.trim();
    if (line.length() == 0)
    {
      Serial.printf("[OTA WL READ] stop: blank line pos=%ld size=%u\n", (long)in.position(), (unsigned)in.size());
      return false;
    }

    const int tab1 = line.indexOf('\t');
    const int tab2 = (tab1 >= 0) ? line.indexOf('\t', tab1 + 1) : -1;

    if (tab1 <= 0 || tab2 <= tab1)
    {
      Serial.printf("[OTA WL READ] malformed line pos=%ld size=%u line='%s'\n", (long)in.position(),
                    (unsigned)in.size(), line.c_str());
      return false;
    }

    String path = line.substring(0, tab1);
    String sizeStr = line.substring(tab1 + 1, tab2);
    String sha = line.substring(tab2 + 1);

    path.trim();
    sizeStr.trim();
    sha.trim();

    if (sha.length() != 64)
    {
      Serial.printf("[OTA WL READ] bad sha len=%u path='%s'\n", (unsigned)sha.length(), path.c_str());
      return false;
    }

    memset(out, 0, sizeof(*out));
    strlcpy(out->path, path.c_str(), sizeof(out->path));
    out->size = (uint32_t)strtoul(sizeStr.c_str(), nullptr, 10);
    strlcpy(out->sha256, sha.c_str(), sizeof(out->sha256));

    return true;
  }
}

// -----------------------------------------------------------------------------
// Internal state
// -----------------------------------------------------------------------------

static AssetOtaConfig s_cfg{};
static AssetOtaState s_state{};

static AssetOtaStatus s_status = AssetOtaStatus::IDLE;
static AssetOtaError s_lastErr = AssetOtaError::NONE;

static String s_installedVersion;

static bool s_inited = false;
static bool s_loadedFromSd = false;
static bool s_graphicsReleasedForOta = false;
static bool s_assetOtaConfirmActive = false;

static bool assetOtaRemotePackAcceptable(const String &remotePackVersion)
{
  if (!remotePackVersion.length())
    return false;

  if (compareSemver3Local(remotePackVersion, RH_MIN_REQUIRED_ASSET_PACK) < 0)
  {
    Serial.printf("[OTA] reject remote pack below minimum: remote=%s required=%s\n", remotePackVersion.c_str(),
                  RH_MIN_REQUIRED_ASSET_PACK);
    return false;
  }

  if (s_installedVersion.length() && compareSemver3Local(remotePackVersion, s_installedVersion) < 0)
  {
    Serial.printf("[OTA] reject remote pack older than installed: remote=%s installed=%s\n", remotePackVersion.c_str(),
                  s_installedVersion.c_str());
    return false;
  }

  return true;
}

// -----------------------------------------------------------------------------
// URL resolution
// -----------------------------------------------------------------------------
static String assetFileResolvedUrl(const String &packVersion, const AssetManifestFile &f, bool fallback)
{
  String url = fallback ? RH_FALLBACK_ASSET_BASE_URL : RH_PRIMARY_ASSET_BASE_URL;

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

// -----------------------------------------------------------------------------
// Public flags
// -----------------------------------------------------------------------------

bool assetOtaConfirmActive() { return s_assetOtaConfirmActive; }

void assetOtaSetConfirmActive(bool v) { s_assetOtaConfirmActive = v; }

bool assetOtaDidReleaseGraphics() { return s_graphicsReleasedForOta; }

// -----------------------------------------------------------------------------
// Worker task
// -----------------------------------------------------------------------------
void assetOtaResetState()
{
  Serial.println("[OTA] RESET STATE");

  s_status = AssetOtaStatus::IDLE;
  s_lastErr = AssetOtaError::NONE;

  assetOtaStateDefaults(s_state);
  assetOtaStateSave(s_state);

  s_graphicsReleasedForOta = false;
  s_assetOtaConfirmActive = false;
}

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

  if (kLogVerifySuccess)
  {
    Serial.printf("[VERIFY] checking %s\n", livePath.c_str());
  }

  if (!SD.exists(livePath.c_str()))
  {
    Serial.printf("[VERIFY] MISSING %s\n", livePath.c_str());
    return false;
  }

  File lf = SD.open(livePath.c_str(), FILE_READ);
  if (!lf)
  {
    Serial.printf("[VERIFY] OPEN FAIL %s\n", livePath.c_str());
    return false;
  }

  const uint32_t actualSize = (uint32_t)lf.size();
  if (actualSize != f.size)
  {
    Serial.printf("[VERIFY] SIZE FAIL %s actual=%u expected=%u\n", livePath.c_str(), (unsigned)actualSize,
                  (unsigned)f.size);
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

  String gotHash;
  gotHash.reserve(64);

  static const char *digits = "0123456789abcdef";
  for (int i = 0; i < 32; ++i)
  {
    gotHash += digits[(hash[i] >> 4) & 0x0F];
    gotHash += digits[hash[i] & 0x0F];
  }

  String wantHash(f.sha256);
  wantHash.toLowerCase();
  wantHash.trim();

  if (!gotHash.equalsIgnoreCase(wantHash))
  {
    Serial.printf("[VERIFY] HASH FAIL %s\n", livePath.c_str());
    Serial.printf("[VERIFY] got=%s\n", gotHash.c_str());
    Serial.printf("[VERIFY] want=%s\n", wantHash.c_str());
    return false;
  }

  if (kLogVerifySuccess)
  {
    Serial.printf("[VERIFY] OK %s\n", livePath.c_str());
  }
  return true;
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
  // During active provisioning, avoid fighting the provisioning screen while
  // downloads are in progress. But once OTA has reached SUCCESS, the boot
  // pipeline may continue (especially for no-op updates), so we must restore
  // the main UI sprite even if provisioning is still marked active.
  if (g_bootAssetProvisionActive && s_status != AssetOtaStatus::SUCCESS)
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

  if (kLogInstallVerbose)
  {
    Serial.printf("\n[INSTALL] =========\n");
    Serial.printf("[INSTALL] rel=%s\n", relPath.c_str());
    Serial.printf("[INSTALL] staging=%s\n", stagingPath.c_str());
    Serial.printf("[INSTALL] live=%s\n", livePath.c_str());
  }

  if (!SD.exists(stagingPath.c_str()))
  {
    Serial.println("[INSTALL] ERROR: staging file missing");
    return false;
  }

  if (!assetOtaEnsureParentDir(livePath.c_str()))
  {
    Serial.println("[INSTALL] ERROR: mkdir failed");
    return false;
  }

  if (SD.exists(livePath.c_str()))
  {
    if (kLogInstallVerbose)
    {
      Serial.println("[INSTALL] removing existing file");
    }
    SD.remove(livePath.c_str());
  }

  if (!SD.rename(stagingPath.c_str(), livePath.c_str()))
  {
    Serial.println("[INSTALL] ERROR: rename failed");
    return false;
  }

  bool exists = SD.exists(livePath.c_str());

  if (kLogInstallVerbose)
  {
    Serial.printf("[INSTALL] DONE exists=%d\n", exists);
  }

  return exists;
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

static bool assetOtaLoadWorklistBatch(uint32_t startOffset, size_t maxEntries, std::vector<AssetManifestFile> &batch,
                                      uint32_t *outNextOffset, bool *outReachedEof)
{
  batch.clear();
  if (outNextOffset)
    *outNextOffset = startOffset;
  if (outReachedEof)
    *outReachedEof = false;

  File in;
  if (!assetOtaWorklistOpenRead(&in))
  {
    Serial.println("[OTA] batch load: worklist open failed");
    return false;
  }

  if (startOffset > 0)
  {
    if (!in.seek(startOffset))
    {
      Serial.printf("[OTA] batch load: seek failed offset=%lu\n", (unsigned long)startOffset);
      in.close();
      return false;
    }
  }

  batch.reserve(maxEntries);

  for (size_t i = 0; i < maxEntries; ++i)
  {
    AssetManifestFile mf{};
    if (!assetOtaWorklistReadNext(in, &mf))
    {
      if (outNextOffset)
        *outNextOffset = (uint32_t)in.position();
      if (outReachedEof)
        *outReachedEof = true;
      in.close();
      return true;
    }

    batch.push_back(mf);

    if (outNextOffset)
      *outNextOffset = (uint32_t)in.position();
  }

  if (outReachedEof)
    *outReachedEof = false;

  in.close();
  return true;
}

bool assetOtaCheckNow(String *outMessage)
{
  s_otaDidInstallFiles = false;

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
  s_state.totalFileCount = 0;
  s_state.targetPackVersion[0] = '\0';
  assetOtaStateSave(s_state);
  otaRedrawProvisionScreen("Checking assets...", "Downloading manifest...");

  const AssetOtaChannel ch = (AssetOtaChannel)s_cfg.channel;
  const char *manifestUrl = assetOtaManifestUrlForChannel(ch);
  const char *fallbackManifestUrl = assetOtaFallbackManifestUrlForChannel(ch);

  Serial.printf("[OTA] manifestUrl=%s\n", manifestUrl ? manifestUrl : "(null)");
  Serial.printf("[OTA] fallbackManifestUrl=%s\n", fallbackManifestUrl ? fallbackManifestUrl : "(null)");

  if (!manifestUrl || !manifestUrl[0])
  {
    setFailure(AssetOtaError::NO_MANIFEST);
    if (outMessage)
      *outMessage = "Manifest URL missing";
    restoreMainUiSprite();
    return false;
  }

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

  bool manifestOk = assetManifestBuildWorklistFromRemote(manifestUrl, &remotePackVersion, &changedCount);

  if (!manifestOk && fallbackManifestUrl && fallbackManifestUrl[0])
  {
    Serial.printf("[OTA] primary manifest failed; trying fallback: %s\n", fallbackManifestUrl);
    remotePackVersion = "";
    changedCount = 0;
    manifestOk = assetManifestBuildWorklistFromRemote(fallbackManifestUrl, &remotePackVersion, &changedCount);
  }

  if (!manifestOk)
  {
    setFailure(AssetOtaError::JSON_FAIL);
    if (outMessage)
      *outMessage = "Manifest download/parse failed";
    restoreMainUiSprite();
    return false;
  }

  if (!assetOtaRemotePackAcceptable(remotePackVersion))
  {
    setFailure(AssetOtaError::JSON_FAIL);
    if (outMessage)
      *outMessage = "Remote asset pack is older than required/current";
    restoreMainUiSprite();
    return false;
  }

  Serial.printf("[OTA] plan result: pack=%s changed=%u worklist=%s\n", remotePackVersion.c_str(),
                (unsigned)changedCount, assetOtaWorklistPath());

  s_progress.total = (changedCount > 0) ? changedCount : 1;
  s_progress.current = 0;
  s_progress.stage = (changedCount > 0) ? "planning" : "up-to-date";

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

    assetOtaWorklistClear();
    restoreMainUiSprite();
    return true;
  }

  if (changedCount == 0)
  {
    Serial.printf("[OTA] no file changes; adopting version=%s\n", remotePackVersion.c_str());

    const char *tmpManifestPath = assetManifestTempPath();
    const char *localManifestPath = assetOtaLocalManifestPath();

    Serial.printf("[OTA] pre-promote tmp exists=%d local exists=%d\n", SD.exists(tmpManifestPath) ? 1 : 0,
                  SD.exists(assetOtaLocalManifestPath()) ? 1 : 0);

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
        saveOk = true;
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

    String installedPack;
    bool havePack = assetManifestLoadLocalPackVersion(&installedPack);
    Serial.printf("[OTA] post-promote local pack load=%d version=%s\n", havePack ? 1 : 0,
                  havePack ? installedPack.c_str() : "(none)");

    if (outMessage)
    {
      *outMessage = "Asset OTA ";
      *outMessage += remotePackVersion;
      *outMessage += " already installed";
    }

    assetOtaWorklistClear();
    restoreMainUiSprite();
    return true;
  }

  uint16_t installOkCount = 0;
  uint16_t skipCount = 0;

  static constexpr size_t kWorklistBatchSize = 64;

  std::vector<AssetManifestFile> worklistBatch;
  uint32_t worklistOffset = 0;
  uint16_t processedCount = 0;

  while (processedCount < changedCount)
  {
    bool reachedEof = false;
    if (!assetOtaLoadWorklistBatch(worklistOffset, kWorklistBatchSize, worklistBatch, &worklistOffset, &reachedEof))
    {
      setFailure(AssetOtaError::JSON_FAIL);
      if (outMessage)
        *outMessage = "Worklist batch load failed";
      restoreMainUiSprite();
      return false;
    }

    if (worklistBatch.empty())
    {
      Serial.printf("[OTA] empty worklist batch processed=%u changed=%u eof=%d\n", (unsigned)processedCount,
                    (unsigned)changedCount, reachedEof ? 1 : 0);
      setFailure(AssetOtaError::JSON_FAIL);
      if (outMessage)
        *outMessage = "Worklist unexpectedly ended";
      restoreMainUiSprite();
      return false;
    }

    for (size_t batchIdx = 0; batchIdx < worklistBatch.size(); ++batchIdx)
    {
      const AssetManifestFile &f = worklistBatch[batchIdx];
      const uint16_t idx = processedCount;
      s_progress.current = idx + 1;
      s_progress.bytesCurrent = 0;
      s_progress.bytesTotal = f.size;
      
      s_status = AssetOtaStatus::DOWNLOADING;
      s_progress.stage = "downloading";

      s_state.status = (uint8_t)s_status;
      s_state.currentFileIndex = (uint16_t)(idx + 1);
      assetOtaStateSave(s_state);

      otaRedrawProvisionScreen("Downloading assets...", f.path);

      String rawPath(f.path);
      String rel;
      if (!assetManifestNormalizePath(rawPath, &rel))
      {
        Serial.printf("[OTA] skipping invalid manifest path: %s\n", rawPath.c_str());
        ++skipCount;
        ++processedCount;
        continue;
      }

      AssetManifestFile dlFile{};
      strlcpy(dlFile.path, rel.c_str(), sizeof(dlFile.path));
      strlcpy(dlFile.sha256, f.sha256, sizeof(dlFile.sha256));
      dlFile.size = f.size;

      String stagingPath;
      String dlErr;
      bool dlOk = false;

      for (int attempt = 1; attempt <= 3; ++attempt)
      {
        stagingPath = "";
        dlErr = "";

        Serial.printf("[OTA] file attempt %d/3: %s\n", attempt, rel.c_str());

        // Force heap churn to avoid TLS reuse bugs
        delay(1);
        yield();

        String url = assetFileResolvedUrl(remotePackVersion, dlFile, false);
        if (assetDownloadToStaging(url, dlFile, &stagingPath, &dlErr))
        {
          dlOk = true;
          break;
        }

        Serial.printf("[OTA] primary file attempt failed: %s\n", dlErr.c_str());

        String fallbackErr;
        String fallbackUrl = assetFileResolvedUrl(remotePackVersion, dlFile, true);
        Serial.printf("[OTA] trying fallback file url=%s\n", fallbackUrl.c_str());

        if (assetDownloadToStaging(fallbackUrl, dlFile, &stagingPath, &fallbackErr))
        {
          Serial.printf("[OTA] fallback file OK: %s\n", rel.c_str());
          dlOk = true;
          break;
        }

        Serial.printf("[OTA] fallback file attempt failed: %s\n", fallbackErr.c_str());

        if (fallbackErr.length())
          dlErr = fallbackErr;

        if (dlErr.indexOf("HTTP -1") >= 0)
        {
          Serial.println("[OTA] detected connection-level failure, extending recovery window");
          delay(800);
          yield();
        }

        // --- HARD RESET GAP BETWEEN ATTEMPTS ---
        int backoff = 200 * attempt; // 200ms, 400ms, 600ms
        delay(backoff);
        yield();

        // Let WiFi/TCP stack stabilize
        uint32_t waitStart = millis();
        while (!wifiIsConnectedNow() && (millis() - waitStart) < 5000)
        {
          delay(50);
          yield();
        }

        // Extra settle time EVEN IF CONNECTED (important)
        delay(200);
        yield();
      }

      if (!dlOk)
      {
        ++skipCount;
        ++processedCount;
        Serial.printf("[OTA] SKIP FAILED FILE: rel=%s err=%s\n", rel.c_str(), dlErr.c_str());
        Serial.printf("[OTA] SKIP FAILED PRIMARY URL: %s\n",
                      assetFileResolvedUrl(remotePackVersion, dlFile, false).c_str());
        Serial.printf("[OTA] SKIP FAILED FALLBACK URL: %s\n",
                      assetFileResolvedUrl(remotePackVersion, dlFile, true).c_str());
        continue;
      }

      s_status = AssetOtaStatus::INSTALLING;
      s_progress.stage = "installing";
      s_state.status = (uint8_t)s_status;
      assetOtaStateSave(s_state);

      otaRedrawProvisionScreen("Installing asset...", rel.c_str());

      if (!installStagedFile(rel, stagingPath))
      {
        setFailure(AssetOtaError::RENAME_FAIL);
        if (outMessage)
        {
          *outMessage = "Install failed: ";
          *outMessage += rel;
        }
        restoreMainUiSprite();
        return false;
      }

      ++installOkCount;
      s_otaDidInstallFiles = true;
      ++processedCount;
      Serial.printf("[OTA] INSTALL COUNT ok=%u skipped=%u current=%s\n", (unsigned)installOkCount, (unsigned)skipCount,
                    rel.c_str());
    }
  }

  Serial.printf("[OTA] INSTALL LOOP DONE ok=%u skipped=%u expected=%u\n", (unsigned)installOkCount, (unsigned)skipCount,
                (unsigned)changedCount);

  // ===== POST-INSTALL VERIFY =====
  {
    uint16_t verifyChanged = 0;
    uint16_t verifiedCount = 0;
    uint32_t verifyOffset = 0;
    std::vector<AssetManifestFile> verifyBatch;

    Serial.printf("[OTA] verify pass starting...\n");
    s_progress.stage = "verifying";
    s_progress.current = 0;

    while (verifiedCount < changedCount)
    {
      bool reachedEof = false;
      if (!assetOtaLoadWorklistBatch(verifyOffset, kWorklistBatchSize, verifyBatch, &verifyOffset, &reachedEof))
      {
        setFailure(AssetOtaError::JSON_FAIL);
        if (outMessage)
          *outMessage = "Verify worklist batch load failed";
        restoreMainUiSprite();
        return false;
      }

      if (verifyBatch.empty())
      {
        Serial.printf("[OTA] verify empty batch verified=%u changed=%u eof=%d\n", (unsigned)verifiedCount,
                      (unsigned)changedCount, reachedEof ? 1 : 0);
        setFailure(AssetOtaError::JSON_FAIL);
        if (outMessage)
          *outMessage = "Verify worklist unexpectedly ended";
        restoreMainUiSprite();
        return false;
      }

      for (size_t i = 0; i < verifyBatch.size(); ++i)
      {
        const AssetManifestFile &vf = verifyBatch[i];

        String rawPath(vf.path);
        String rel;
        if (!assetManifestNormalizePath(rawPath, &rel))
        {
          Serial.printf("[OTA VERIFY] skip bad path: %s\n", vf.path);
          ++verifiedCount;
          s_progress.current = verifiedCount;
          continue;
        }

        AssetManifestFile check{};
        strlcpy(check.path, rel.c_str(), sizeof(check.path));
        check.size = vf.size;
        strlcpy(check.sha256, vf.sha256, sizeof(check.sha256));

        if (!localAssetMatches(check))
        {
          Serial.printf("[OTA VERIFY FAIL] %s\n", rel.c_str());
          verifyChanged++;
        }

        ++verifiedCount;
        s_progress.current = verifiedCount;
      }
    }

    Serial.printf("[OTA] verify result: remainingChanged=%u\n", (unsigned)verifyChanged);

    if (verifyChanged != 0)
    {
      setFailure(AssetOtaError::WRITE_FAIL);
      if (outMessage)
      {
        *outMessage = "Install incomplete (";
        *outMessage += (int)verifyChanged;
        *outMessage += " files remaining)";
      }
      Serial.printf("[OTA] VERIFY FAILED - NOT promoting manifest\n");
      restoreMainUiSprite();
      return false;
    }

    Serial.printf("[OTA] verify pass OK\n");
  }

  const char *tmpManifestPath = assetManifestTempPath();
  const char *localManifestPath = assetOtaLocalManifestPath();

  if (!SD.exists(tmpManifestPath))
  {
    setFailure(AssetOtaError::WRITE_FAIL);
    if (outMessage)
      *outMessage = "Final manifest temp file missing";
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
  s_progress.stage = "finalizing";
  s_progress.current = s_progress.total;
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

  assetOtaWorklistClear();
  restoreMainUiSprite();
  return true;
}

bool assetOtaDidInstallFiles() { return s_otaDidInstallFiles; }