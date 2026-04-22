#include "console.h"

// -----------------------------------------------------------------------------
// Project: Core App / State
// -----------------------------------------------------------------------------
#include "app_state.h"
#include "settings_state.h"
#include "user_toggles_state.h"

// -----------------------------------------------------------------------------
// Project: Systems
// -----------------------------------------------------------------------------
#include "asset_manifest.h"
#include "asset_ota.h"
#include "asset_ota_config.h"
#include "asset_provision_request.h"
#include "boot_firmware_marker.h"
#include "boot_pipeline.h"
#include "controls_help_state.h"
#include "save_manager.h"
#include "sdcard.h"
#include "timezone.h"
#include "wifi_power.h"
#include "wifi_store.h"
#include "wifi_time.h"

// -----------------------------------------------------------------------------
// Project: Gameplay / Domain
// -----------------------------------------------------------------------------
#include "currency.h"
#include "pet.h"
#include "pet_age.h"

// -----------------------------------------------------------------------------
// Project: UI / Rendering
// -----------------------------------------------------------------------------
#include "graphics.h"
#include "led_status.h"
#include "ui_runtime.h"

// -----------------------------------------------------------------------------
// Project: Input / Debug
// -----------------------------------------------------------------------------
#include "debug.h"
#include "input.h"
#include "runtime_log.h"

// -----------------------------------------------------------------------------
// Project: Build / Version
// -----------------------------------------------------------------------------
#include "build_flags.h"
#include "version.h"

// -----------------------------------------------------------------------------
// External / Framework
// -----------------------------------------------------------------------------
#include <FS.h>
#include <Preferences.h>
#include <SD.h>
#include <esp_err.h>
#include <esp_system.h>
#include <nvs_flash.h>

// -----------------------------------------------------------------------------
// Standard Library
// -----------------------------------------------------------------------------
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// End of Includesvangelion

// -----------------------------------------------------------------------------
// Console state
// -----------------------------------------------------------------------------
static bool g_consoleOpen = false;
static bool g_consoleJustOpened = false;
static bool g_consoleSupportMode = false;

// Current editable input line
static char g_buf[64];
static uint8_t g_len = 0;

// Scrollback (fixed ring)
static constexpr int MAX_LINES = 96;
static char g_lines[MAX_LINES][64];
static int g_lineCount = 0;

// 0 = pinned to bottom/live tail
// >0 = scrolled upward by this many lines
static int g_scrollOffset = 0;

static void consoleClampScroll();

// Command history (fixed ring)
static const int CONSOLE_HIST_MAX = 12;
static const int CONSOLE_HIST_LINE_MAX = 64;

static char g_hist[CONSOLE_HIST_MAX][CONSOLE_HIST_LINE_MAX];
static int g_histCount = 0; // number of valid entries (<= MAX)
static int g_histHead = 0;  // next write index (ring)
static int g_histNav = -1;  // -1 = not navigating; otherwise 0..histCount-1 (relative age)

static char g_histDraft[CONSOLE_HIST_LINE_MAX]; // what user typed before navigating
static bool g_histHasDraft = false;

static UIState g_consolePrevUiState{};
static Tab g_consolePrevTab = Tab::TAB_PET;
static bool g_consoleHadPrevState = false;

static void logLine(const char *s);

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------
static bool consoleSaveRuntimeLogToSd(const char *path, String *outErr)
{
  if (outErr)
    *outErr = "";

  if (!g_sdReady)
  {
    if (outErr)
      *outErr = "SD not ready";
    return false;
  }

  if (!path || !path[0])
  {
    if (outErr)
      *outErr = "Invalid path";
    return false;
  }

  if (!SD.exists("/raising_hell"))
    SD.mkdir("/raising_hell");
  if (!SD.exists("/raising_hell/logs"))
    SD.mkdir("/raising_hell/logs");

  if (SD.exists(path))
    SD.remove(path);

  File f = SD.open(path, FILE_WRITE);
  if (!f)
  {
    if (outErr)
      *outErr = "Open failed";
    return false;
  }

  f.println("--- LOGDUMP BEGIN ---");

  const int count = runtimeLogCount();
  for (int i = 0; i < count; i++)
  {
    char line[160];
    snprintf(line, sizeof(line), "[%03d] %s", i, runtimeLogGetLine(i));
    f.println(line);
  }

  f.println("--- LOGDUMP END ---");
  f.flush();
  f.close();

  return true;
}

static bool consoleSaveSupportReportToSd(const char *path, String *outErr)
{
  if (outErr)
    *outErr = "";

  if (!g_sdReady)
  {
    if (outErr)
      *outErr = "SD not ready";
    return false;
  }

  if (!path || !path[0])
  {
    if (outErr)
      *outErr = "Invalid path";
    return false;
  }

  if (!SD.exists("/raising_hell"))
    SD.mkdir("/raising_hell");
  if (!SD.exists("/raising_hell/logs"))
    SD.mkdir("/raising_hell/logs");

  if (SD.exists(path))
    SD.remove(path);

  File f = SD.open(path, FILE_WRITE);
  if (!f)
  {
    if (outErr)
      *outErr = "Open failed";
    return false;
  }

  f.println("=== SUPPORT REPORT ===");

  f.printf("Version: %s\n", RH_VERSION_STRING);

#if defined(PUBLIC_BUILD) && PUBLIC_BUILD
  f.println("Build: PUBLIC");
#else
  f.println("Build: DEV");
#endif

  f.printf("Save version: %u\n", (unsigned)SAVE_VERSION);

  const char *assetVer = assetOtaInstalledVersion();
  f.printf("Assets: %s\n", (assetVer && assetVer[0]) ? assetVer : "none");

  const AssetOtaConfig &cfg = assetOtaGetConfig();
  f.printf("OTA channel: %s\n", ((AssetOtaChannel)cfg.channel == AssetOtaChannel::DEV) ? "DEV" : "PUBLIC");

  f.printf("Timezone idx: %d\n", tzIndex);
  f.printf("Timezone name: %s\n", tzName(tzIndex));

  f.printf("WiFi enabled: %s\n", wifiIsEnabled() ? "YES" : "NO");
  f.printf("WiFi connected: %s\n", wifiIsConnectedNow() ? "YES" : "NO");

  const char *ssid = wifiConsoleSsid();
  f.printf("SSID: %s\n", (ssid && ssid[0]) ? ssid : "(none)");

  const char *ip = wifiConsoleIpString();
  f.printf("IP: %s\n", (ip && ip[0]) ? ip : "(none)");

  f.printf("Free heap: %u\n", (unsigned)ESP.getFreeHeap());
  f.printf("Largest block: %u\n", (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

  f.println("======================");
  f.flush();
  f.close();

  return true;
}

static bool consoleParseSemver3(const String &s, int &maj, int &min, int &pat)
{
  maj = min = pat = 0;

  int firstDot = s.indexOf('.');
  if (firstDot < 0)
    return false;

  int secondDot = s.indexOf('.', firstDot + 1);
  if (secondDot < 0)
    return false;

  String a = s.substring(0, firstDot);
  String b = s.substring(firstDot + 1, secondDot);
  String c = s.substring(secondDot + 1);

  if (!a.length() || !b.length() || !c.length())
    return false;

  maj = a.toInt();
  min = b.toInt();
  pat = c.toInt();
  return true;
}

static int consoleCompareSemver3(const String &lhs, const String &rhs)
{
  int lMaj = 0, lMin = 0, lPat = 0;
  int rMaj = 0, rMin = 0, rPat = 0;

  if (!consoleParseSemver3(lhs, lMaj, lMin, lPat))
    return -1;
  if (!consoleParseSemver3(rhs, rMaj, rMin, rPat))
    return 1;

  if (lMaj != rMaj)
    return (lMaj < rMaj) ? -1 : 1;
  if (lMin != rMin)
    return (lMin < rMin) ? -1 : 1;
  if (lPat != rPat)
    return (lPat < rPat) ? -1 : 1;
  return 0;
}

static bool consoleAssetPackTooOld()
{
  if (!g_sdReady)
    return false;

  String installedPack;
  const bool haveInstalled = assetManifestLoadLocalPackVersion(&installedPack);

  return (!haveInstalled || !installedPack.length() ||
          consoleCompareSemver3(installedPack, RH_MIN_REQUIRED_ASSET_PACK) < 0);
}

static const char *assetChannelToString(uint8_t ch) { return (ch == (uint8_t)AssetOtaChannel::DEV) ? "dev" : "public"; }

static bool consoleSupportModeEnabled()
{
#if defined(PUBLIC_BUILD) && PUBLIC_BUILD
  return g_consoleSupportMode;
#else
  return true;
#endif
}

static void consoleLogSupportModeStatus()
{
#if defined(PUBLIC_BUILD) && PUBLIC_BUILD
  if (g_consoleSupportMode)
    logLine("Support mode: ON");
  else
    logLine("Support mode: OFF");

  if (!g_consoleSupportMode)
    logLine("Use 'support on' to enable support commands.");
#else
  logLine("Support mode: always available in dev build");
#endif
}

static bool consoleRequireSupportMode()
{
#if defined(PUBLIC_BUILD) && PUBLIC_BUILD
  if (!consoleSupportModeEnabled())
  {
    logLine("Command locked. Use 'support on'.");
    return false;
  }
#endif
  return true;
}

static void resetLine()
{
  g_len = 0;
  g_buf[0] = '\0';
}

static void pushLine(const char *s)
{
  if (!s)
    return;

  // Truncate to fit line storage
  char tmp[64];
  strncpy(tmp, s, sizeof(tmp) - 1);
  tmp[sizeof(tmp) - 1] = '\0';

  if (g_lineCount < MAX_LINES)
  {
    strncpy(g_lines[g_lineCount], tmp, sizeof(g_lines[g_lineCount]) - 1);
    g_lines[g_lineCount][sizeof(g_lines[g_lineCount]) - 1] = '\0';
    g_lineCount++;
  }
  else
  {
    for (int i = 1; i < MAX_LINES; i++)
    {
      strcpy(g_lines[i - 1], g_lines[i]);
    }
    strncpy(g_lines[MAX_LINES - 1], tmp, sizeof(g_lines[MAX_LINES - 1]) - 1);
    g_lines[MAX_LINES - 1][sizeof(g_lines[MAX_LINES - 1]) - 1] = '\0';
  }

  consoleClampScroll();
}

static void logLine(const char *s)
{
  pushLine(s);

  // Never stall gameplay/UI if Serial isn't draining.
  if (!s)
    return;
  int need = (int)strlen(s) + 2; // \r\n
  if (Serial && Serial.availableForWrite() >= need)
  {
    Serial.println(s);
  }
}

static void consoleArmReprovisionRecovery()
{
  const bool wifiEnabledBefore = settingsWifiEnabled();
  const bool hasCredsBefore = wifiStoreHasCreds();

  Serial.printf("[RESCUE] reprovision armed wifiEnabled=%d hasCreds=%d\n", wifiEnabledBefore ? 1 : 0,
                hasCredsBefore ? 1 : 0);

  if (!bootFirmwareMarkerClear())
  {
    logLine("FAILED to clear firmware marker");
    return;
  }

  requestAssetProvisionOnNextBoot();

  if (!wifiEnabledBefore)
  {
    settingsSetWifiEnabled(true);
    saveSettingsToSD();
  }

  const bool wifiEnabledAfter = settingsWifiEnabled();
  const bool hasCredsAfter = wifiStoreHasCreds();

  Serial.printf("[RESCUE] wifiEnabledAfter=%d hasCreds=%d\n", wifiEnabledAfter ? 1 : 0, hasCredsAfter ? 1 : 0);

  logLine("[OK] firmware marker cleared");
  logLine("[OK] asset provision boot flag set");

  if (!wifiEnabledBefore)
    logLine("[OK] WiFi enabled for next-boot reprovision");

  if (!hasCredsAfter)
    logLine("[WARN] no WiFi credentials saved; boot WiFi flow may still be required");

  logLine("[OK] rebooting into rescue reprovision...");
  delay(100);
  ESP.restart();
}

static void consoleRequestAssetRepair()
{
  const bool wifiEnabledBefore = settingsWifiEnabled();
  const bool hasCredsBefore = wifiStoreHasCreds();

  Serial.printf("[REPAIR] asset repair requested wifiEnabled=%d hasCreds=%d\n", wifiEnabledBefore ? 1 : 0,
                hasCredsBefore ? 1 : 0);

  requestAssetProvisionOnNextBoot();

  if (!wifiEnabledBefore)
  {
    settingsSetWifiEnabled(true);
    saveSettingsToSD();
  }

  const bool wifiEnabledAfter = settingsWifiEnabled();
  const bool hasCredsAfter = wifiStoreHasCreds();

  Serial.printf("[REPAIR] wifiEnabledAfter=%d hasCreds=%d\n", wifiEnabledAfter ? 1 : 0, hasCredsAfter ? 1 : 0);

  logLine("[OK] asset provision requested");

  if (!wifiEnabledBefore)
    logLine("[OK] WiFi enabled for asset repair");

  if (!hasCredsAfter)
    logLine("[WARN] no WiFi credentials saved");

  logLine("[OK] provisioning should start automatically");
}

static void logf(const char *fmt, ...)
{
  char buf[96];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  // Split on '\n' so a multi-line format becomes multiple scrollback lines
  const char *p = buf;
  while (*p)
  {
    const char *nl = strchr(p, '\n');
    if (!nl)
    {
      logLine(p);
      break;
    }
    char line[64];
    size_t n = (size_t)(nl - p);
    if (n >= sizeof(line))
      n = sizeof(line) - 1;
    memcpy(line, p, n);
    line[n] = '\0';
    logLine(line);
    p = nl + 1;
  }
}

// -----------------------------------------------------------------------------
// History helpers
// -----------------------------------------------------------------------------
static void consoleSetInputLine(const char *s)
{
  if (!s)
    s = "";
  strncpy(g_buf, s, sizeof(g_buf) - 1);
  g_buf[sizeof(g_buf) - 1] = '\0';
  g_len = (uint8_t)strlen(g_buf);
}

static bool histIsSameAsLast(const char *s)
{
  if (!s || !*s)
    return true;
  if (g_histCount <= 0)
    return false;

  int lastIdx = g_histHead - 1;
  if (lastIdx < 0)
    lastIdx = CONSOLE_HIST_MAX - 1;

  return strncmp(g_hist[lastIdx], s, CONSOLE_HIST_LINE_MAX) == 0;
}

static void histPush(const char *s)
{
  if (!s)
    return;

  // trim leading spaces
  while (*s == ' ' || *s == '\t')
    s++;
  if (!*s)
    return;

  if (histIsSameAsLast(s))
    return;

  strncpy(g_hist[g_histHead], s, CONSOLE_HIST_LINE_MAX - 1);
  g_hist[g_histHead][CONSOLE_HIST_LINE_MAX - 1] = '\0';

  g_histHead = (g_histHead + 1) % CONSOLE_HIST_MAX;
  if (g_histCount < CONSOLE_HIST_MAX)
    g_histCount++;

  // reset navigation state
  g_histNav = -1;
  g_histHasDraft = false;
}

static const char *histGetByNavIndex(int nav)
{
  // nav: 0 = most recent, 1 = one older, ...
  if (nav < 0 || nav >= g_histCount)
    return nullptr;

  int idx = g_histHead - 1 - nav;
  while (idx < 0)
    idx += CONSOLE_HIST_MAX;
  idx %= CONSOLE_HIST_MAX;
  return g_hist[idx];
}

static bool histPrev()
{
  if (g_histCount <= 0)
    return false;

  if (g_histNav < 0)
  {
    // entering navigation: store draft
    strncpy(g_histDraft, g_buf, sizeof(g_histDraft) - 1);
    g_histDraft[sizeof(g_histDraft) - 1] = '\0';
    g_histHasDraft = true;
    g_histNav = 0;
  }
  else
  {
    if (g_histNav < g_histCount - 1)
      g_histNav++;
  }

  const char *s = histGetByNavIndex(g_histNav);
  if (!s)
    return false;
  consoleSetInputLine(s);
  return true;
}

static bool histNext()
{
  if (g_histCount <= 0)
    return false;
  if (g_histNav < 0)
    return false;

  if (g_histNav > 0)
  {
    g_histNav--;
    const char *s = histGetByNavIndex(g_histNav);
    if (!s)
      return false;
    consoleSetInputLine(s);
    return true;
  }

  // g_histNav == 0 -> leave history navigation and restore draft
  g_histNav = -1;
  if (g_histHasDraft)
  {
    consoleSetInputLine(g_histDraft);
  }
  else
  {
    consoleSetInputLine("");
  }
  return true;
}

static void histCancelNav()
{
  g_histNav = -1;
  g_histHasDraft = false;
}

// ---------------------------------------------------------------------------
// Pet type helpers
// ---------------------------------------------------------------------------
static const char *petTypeToString(PetType t)
{
  switch (t)
  {
  case PET_DEVIL:
    return "devil";
  case PET_ELDRITCH:
    return "eldritch";
  default:
    return "unknown";
  }
}
static bool parsePetType(const char *s, PetType &out)
{
  if (!s || !*s)
    return false;

  if (!strcmp(s, "devil"))
  {
    out = PET_DEVIL;
    return true;
  }

  if (!strcmp(s, "eldritch"))
  {
    out = PET_ELDRITCH;
    return true;
  }

  return false;
}

// -----------------------------------------------------------------------------
// WiFi helpers (Preferences-backed creds)
// -----------------------------------------------------------------------------
static void wifiSaveCreds(const char *ssid, const char *pass) { wifiStoreSave(ssid, pass); }

static bool wifiLoadCreds(String &ssidOut, String &passOut) { return wifiStoreLoad(ssidOut, passOut); }

static void wifiClearCreds() { wifiStoreClear(); }

void wifiBeginConnect(const char *ssid, const char *pass)
{
  logf("wifi: connecting to '%s'...", ssid ? ssid : "");
  wifiConsoleBeginConnect(ssid, pass);
}

// -----------------------------------------------------------------------------
// Timezone console helpers
// -----------------------------------------------------------------------------
static bool consoleParseNonNegativeInt(const char *s, int &out)
{
  if (!s || !s[0])
    return false;

  int v = 0;
  for (const char *p = s; *p; ++p)
  {
    if (*p < '0' || *p > '9')
      return false;
    v = (v * 10) + (*p - '0');
  }

  out = v;
  return true;
}

static void consoleFormatTm(char *buf, size_t bufSize, const tm &t)
{
  if (!buf || bufSize == 0)
    return;

  snprintf(buf, bufSize, "%04d-%02d-%02d %02d:%02d:%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour,
           t.tm_min, t.tm_sec);
}

static void consoleLogTimeSnapshot(const char *prefix)
{
  const time_t now = time(nullptr);

  logf("%sepoch: %lu", prefix ? prefix : "", (unsigned long)now);

  if (now <= 1600000000)
  {
    logf("%stime:  INVALID", prefix ? prefix : "");
    return;
  }

  tm tmLocal = {};
  tm tmUtc = {};
  localtime_r(&now, &tmLocal);
  gmtime_r(&now, &tmUtc);

  char localBuf[32];
  char utcBuf[32];
  consoleFormatTm(localBuf, sizeof(localBuf), tmLocal);
  consoleFormatTm(utcBuf, sizeof(utcBuf), tmUtc);

  logf("%slocal: %s", prefix ? prefix : "", localBuf);
  logf("%sutc:   %s", prefix ? prefix : "", utcBuf);
}

static void consoleLogTimezoneStatusForIndex(int idx, const char *prefix)
{
  const char *p = prefix ? prefix : "";

  if (!tzIndexIsValid(idx))
  {
    logf("%sidx=%d INVALID", p, idx);
    return;
  }

  logf("%sidx=%d", p, idx);
  logf("%slabel=%s", p, tzName((uint8_t)idx));
  logf("%siana=%s", p, tzIanaName((uint8_t)idx));
  logf("%srule=%s", p, tzPosixRule((uint8_t)idx));
}

static bool consoleResolveTimezoneSpec(const char *spec, int &outIdx)
{
  if (!spec || !spec[0])
    return false;

  int numericIdx = -1;
  if (consoleParseNonNegativeInt(spec, numericIdx))
  {
    if (!tzIndexIsValid(numericIdx))
      return false;
    outIdx = numericIdx;
    return true;
  }

  const int mapped = tzFindIndexByIana(spec);
  if (!tzIndexIsValid(mapped))
    return false;

  outIdx = mapped;
  return true;
}

static void consoleApplyTimezoneForTest(int idx)
{
  if (!tzIndexIsValid(idx))
    return;
  applyTimezoneIndex((uint8_t)idx);
}

static void consoleLogTimezonePreview(int idx, const char *prefix)
{
  if (!tzIndexIsValid(idx))
  {
    logf("%sINVALID idx=%d", prefix ? prefix : "", idx);
    return;
  }

  const int restoreIdx = tzIndexIsValid(tzIndex) ? tzIndex : (int)tzDefaultIndex();

  consoleApplyTimezoneForTest(idx);
  consoleLogTimezoneStatusForIndex(idx, prefix);
  consoleLogTimeSnapshot(prefix);

  consoleApplyTimezoneForTest(restoreIdx);
}

static void consoleShowCurrentTimezoneStatus()
{
  if (!tzIndexIsValid(tzIndex))
  {
    logf("tzIndex invalid (%d), defaulting to %u for display", tzIndex, (unsigned)tzDefaultIndex());
  }

  const int currentIdx = tzIndexIsValid(tzIndex) ? tzIndex : (int)tzDefaultIndex();

  logLine("Timezone status:");
  consoleLogTimezoneStatusForIndex(currentIdx, "  ");
  consoleLogTimeSnapshot("  ");
}

static void consoleRunTimezoneCaseSuite()
{
  struct TzCase
  {
    const char *input;
    const char *expectIana;
  };

  static const TzCase kCases[] = {
      {"America/Chicago", "America/Chicago"},
      {"America/Phoenix", "America/Phoenix"},
      {"America/St_Johns", "America/St_Johns"},
      {"Europe/Paris", "Europe/Paris"},
      {"Asia/Kolkata", "Asia/Kolkata"},
      {"Australia/Brisbane", "Australia/Brisbane"},
      {"Australia/Adelaide", "Australia/Adelaide"},
      {"Bad/Zone", nullptr},
  };

  logLine("TZ case suite:");

  for (const TzCase &tc : kCases)
  {
    const int idx = tzFindIndexByIana(tc.input);

    if (!tc.expectIana)
    {
      if (idx < 0)
        logf("  PASS %-24s -> unsupported", tc.input);
      else
        logf("  FAIL %-24s -> mapped=%d iana=%s", tc.input, idx, tzIanaName((uint8_t)idx));
      continue;
    }

    if (idx < 0)
    {
      logf("  FAIL %-24s -> unsupported (expected %s)", tc.input, tc.expectIana);
      continue;
    }

    const char *actualIana = tzIanaName((uint8_t)idx);
    if (strcmp(actualIana, tc.expectIana) == 0)
      logf("  PASS %-24s -> idx=%d %s", tc.input, idx, actualIana);
    else
      logf("  FAIL %-24s -> idx=%d %s (expected %s)", tc.input, idx, actualIana, tc.expectIana);
  }
}

// -----------------------------------------------------------------------------
// Command execution
// -----------------------------------------------------------------------------
static void execLine(char *line)
{
  // tokenize (in-place)
  char *argv[6];
  int argc = 0;

  char *tok = strtok(line, " ");
  while (tok && argc < (int)(sizeof(argv) / sizeof(argv[0])))
  {
    argv[argc++] = tok;
    tok = strtok(nullptr, " ");
  }
  if (argc == 0)
    return;

  // HELP
  if (!strcmp(argv[0], "help") || !strcmp(argv[0], "?"))
  {
    logLine("Commands:");
    logLine("  help | ?            show this");
    logLine("  clear               clear console");
    logLine("  exit                close console");
    logLine("  reboot              reboot device");

    logLine("Pet Commands:");
    logLine("  mon                 show stats");
    logLine("  age                 show birth epoch + age string");
    logLine("  pet                 show current pet type");
    logLine("  name <pet name>     set pet name");
    
    logLine("Status / info:");
    logLine("  version             show firmware + asset version");
    logLine("  assetstatus         show asset OTA/debug status");
    logLine("  uptime              show device uptime");

    logLine("WiFi:");
    logLine("  wifi                show wifi status + saved ssid");
    logLine("  wifi off|on         toggle wifi power");
    logLine("  wifi <ssid> <pass>  save + connect");
    logLine("  wifi clear          clear saved creds");

    logLine("Logs:");
    logLine("  logdump             dump runtime log buffer");
    logLine("  logtail [n]         dump last n log lines");
    logLine("  logclear            clear runtime log buffer");
    logLine("  logsave             save runtime log to /raising_hell/logs/logdump.txt");

    logLine("Support:");
    logLine("  support             show support mode status");
    logLine("  support status      show support mode status");
    logLine("  support report      system diagnostic dump");
#if defined(PUBLIC_BUILD) && PUBLIC_BUILD
    logLine("  support on|off      enable/disable support commands");
#endif

    if (consoleSupportModeEnabled())
    {
      logLine("Support commands:");
      logLine("  tz                  show current timezone + local/UTC time");
      logLine("  tz list             list supported timezone indices");
      logLine("  tz map <IANA>       resolve IANA zone -> internal zone");
      logLine("  tz test <IANA|idx>  apply temporarily, print local time, restore");
      logLine("  tz save <IANA|idx>  apply + persist timezone");
      logLine("  tz set <idx>        shortcut for tz save <idx>");
      logLine("  tz cases            run tricky timezone mapping suite");
      logLine("  assetflag           show asset provision boot flag");
      logLine("  assetflag clear     clear asset provision boot flag");
      logLine("  assetflag set       set asset provision boot flag");
      logLine("  ntpskip             bypass stalled NTP check");
      logLine("  bootflags           inspect boot / recovery flags");
      logLine("  bootheal            clear stuck boot flags");
      logLine("  saveheal            repair save name/pending-flag issues");
      logLine("  clearnamepending    clear name-pending flag");
      logLine("  clearpostprov       clear post-provision help flag");
      logLine("  clearbootsetup      clear boot setup pending flag");
      logLine("  repair              alias for fwmark reprovision");
      logLine("  repair assets       re-run asset provisioning (safe)");
      logLine("  rescue ota          clear firmware marker + reprovision");
      logLine("  rescue              alias for fwmark reprovision");
      logLine("  fwmark              show firmware marker status");
      logLine("  fwmark show         show stored/current build id");
      logLine("  fwmark clear        clear stored build id");
      logLine("  fwmark reprovision  clear marker + set asset flag");
      logLine("  timeinvalidate      mark current time invalid");
      logLine("  nvsclear            wipe NVS + reboot");
    }

#if !PUBLIC_BUILD
    logLine("Dev / test:");
    logLine("  fwmark set <id>     set stored build id");
    logLine("  otach public|dev    set OTA channel");
    logLine("  otadev              switch to DEV OTA + provision + reboot");
    logLine("  nuke                FULL WIPE (SD + NVS + reboot)");
    logLine("  giveinf <amount>    add Inferium");
    logLine("  sethunger <0-100>   set hunger");
    logLine("  setmood <0-100>     set mood");
    logLine("  setrest <0-100>     set energy");
    logLine("  sethealth <0-100>   set health");
    logLine("  ledtest             cycle LED colors (~5s)");
    logLine("  reset_settings      delete settings.bin");
    logLine("  newpet!             OVERWRITE save + start a new pet");
    logLine("  pet cycle|devil|eldritch  set pet type");
    logLine("  givexp <amount>     give the pet XP (levels up if needed)");
    logLine("  setlevel <level>    set pet level (resets XP progress)");
    logLine("  setevo <0-3|baby|teen|adult|elder>  force evo stage");
    logLine("  hurtpet             set low stats + HP=25 (test death flow)");
    logLine("  killpet             instantly kill pet (test death/resurrection)");
    logLine("  healpet             restore HP + all core stats to 100");
    logLine("  fadeboot            trigger pet intro fade on next boot");
#endif

    return;
  }

  // NEW PET (destructive)
#if !PUBLIC_BUILD
  if (!strcmp(argv[0], "newpet"))
  {
    logLine("Type 'newpet!' to confirm (this overwrites your save).");
    return;
  }

  if (!strcmp(argv[0], "newpet!"))
  {
    logLine("[OK] Creating new pet...");
    saveManagerNewPet();
    logLine("[OK] New pet created (save overwritten).");
    return;
  }
#endif

#if !PUBLIC_BUILD
  if (!strcmp(argv[0], "nuke"))
  {
    logLine("Type 'nuke!' to confirm (THIS WILL ERASE EVERYTHING).");
    return;
  }

  if (!strcmp(argv[0], "nuke!"))
  {
    logLine("[DEV] FULL WIPE REQUESTED");
    saveManagerFullWipe();
    return;
  }
#endif

  // CLEAR
  if (!strcmp(argv[0], "clear"))
  {
    consoleClear();
    return;
  }

  // SUPPORT MODE
  if (!strcmp(argv[0], "support"))
  {
    if (argc == 1 || !strcmp(argv[1], "status"))
    {
      consoleLogSupportModeStatus();
      return;
    }

    if (!strcmp(argv[1], "report"))
    {
      static const char *kSupportReportPath = "/raising_hell/logs/support_report.txt";

      logLine("=== SUPPORT REPORT ===");

      logf("Version: %s", RH_VERSION_STRING);

#if defined(PUBLIC_BUILD) && PUBLIC_BUILD
      logLine("Build: PUBLIC");
#else
      logLine("Build: DEV");
#endif

      logf("Save version: %u", (unsigned)SAVE_VERSION);

      const char *assetVer = assetOtaInstalledVersion();
      logf("Assets: %s", (assetVer && assetVer[0]) ? assetVer : "none");

      const AssetOtaConfig &cfg = assetOtaGetConfig();
      logf("OTA channel: %s", ((AssetOtaChannel)cfg.channel == AssetOtaChannel::DEV) ? "DEV" : "PUBLIC");

      logf("Timezone idx: %d", tzIndex);
      logf("Timezone name: %s", tzName(tzIndex));

      logf("WiFi enabled: %s", wifiIsEnabled() ? "YES" : "NO");
      logf("WiFi connected: %s", wifiIsConnectedNow() ? "YES" : "NO");

      const char *ssid = wifiConsoleSsid();
      logf("SSID: %s", (ssid && ssid[0]) ? ssid : "(none)");

      const char *ip = wifiConsoleIpString();
      logf("IP: %s", (ip && ip[0]) ? ip : "(none)");

      logf("Free heap: %u", (unsigned)ESP.getFreeHeap());
      logf("Largest block: %u", (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

      logLine("======================");

      String err;
      if (!consoleSaveSupportReportToSd(kSupportReportPath, &err))
        logf("support report save failed: %s", err.c_str());
      else
        logf("[OK] support report saved: %s", kSupportReportPath);

      return;
    }

#if defined(PUBLIC_BUILD) && PUBLIC_BUILD
    if (!strcmp(argv[1], "on"))
    {
      g_consoleSupportMode = true;
      logLine("[OK] Support mode enabled");
      logLine("[OK] Additional support commands are now available");
      return;
    }

    if (!strcmp(argv[1], "off"))
    {
      g_consoleSupportMode = false;
      logLine("[OK] Support mode disabled");
      return;
    }
#else
    if (!strcmp(argv[1], "on") || !strcmp(argv[1], "off"))
    {
      logLine("Support mode is always available in dev build");
      return;
    }
#endif

    logLine("Usage:");
    logLine("  support");
    logLine("  support status");
    logLine("  support report");
#if defined(PUBLIC_BUILD) && PUBLIC_BUILD
    logLine("  support on");
    logLine("  support off");
#endif
    return;
  }

#if !PUBLIC_BUILD
  if (!strcmp(argv[0], "reset_settings"))
  {
    if (!g_sdReady)
    {
      logLine("SD not ready");
      return;
    }

    const char *path = "/raising_hell/save/settings.bin";

    if (!SD.exists(path))
    {
      logLine("settings.bin does not exist");
      logLine("[OK] reboot to test first boot");
      return;
    }

    if (SD.remove(path))
    {
      logLine("[OK] settings.bin deleted");
      logLine("[OK] reboot to test first boot");
    }
    else
    {
      logLine("failed to delete settings.bin");
    }
    return;
  }
#endif

#if !PUBLIC_BUILD
  if (!strcmp(argv[0], "fadeboot"))
  {
    saveManagerSetPetIntroFadeBootFlag();
    saveSettingsToSD();

    logLine("[OK] pet intro fade armed for next boot");
    logLine("[OK] reboot to trigger");
    return;
  }
#endif

  // GIVE INF
#if !PUBLIC_BUILD
  if (!strcmp(argv[0], "giveinf"))
  {
    if (argc < 2)
    {
      logLine("Usage: giveinf <amount>");
      return;
    }
    int amt = atoi(argv[1]);
    if (amt <= 0)
    {
      logLine("Amount must be > 0");
      return;
    }
    addInf(amt);
    logf("[OK] INF +%d (now %d)", amt, getInf());
    return;
  }
#endif

  // GIVE XP
#if !PUBLIC_BUILD
  if (!strcmp(argv[0], "givexp"))
  {
    if (argc < 2)
    {
      logLine("Usage: givexp <amount>");
      return;
    }
    long amt = atol(argv[1]);
    if (amt <= 0)
    {
      logLine("Amount must be > 0");
      return;
    }

    pet.addXP((uint32_t)amt);
    saveManagerMarkDirty();
    requestUIRedraw();

    logf("[OK] XP +%ld (Level %u, XP %lu/%lu)", amt, (unsigned)pet.level, (unsigned long)pet.xp,
         (unsigned long)pet.xpForNextLevel());
    return;
  }
#endif

  // SET LEVEL
#if !PUBLIC_BUILD
  if (!strcmp(argv[0], "setlevel"))
  {
    if (argc < 2)
    {
      logLine("Usage: setlevel <level>");
      return;
    }
    long lvl = atol(argv[1]);
    if (lvl < 1)
      lvl = 1;
    if (lvl > 999)
      lvl = 999;

    pet.level = (uint16_t)lvl;
    pet.xp = 0; // reset progress toward next level
    saveManagerMarkDirty();
    requestUIRedraw();

    logf("[OK] Level set to %ld (XP reset, next %lu)", lvl, (unsigned long)pet.xpForNextLevel());
    return;
  }
#endif

  // -------------------------------------------------
  // SET EVO STAGE
  // Usage: setevo <0-3|baby|teen|adult|elder>
  // -------------------------------------------------
#if !PUBLIC_BUILD
  if (!strcmp(argv[0], "setevo"))
  {
    if (argc < 2)
    {
      logLine("Usage: setevo <0-3|baby|teen|adult|elder>");
      return;
    }

    uint8_t st = 0;

    if (!strcmp(argv[1], "baby"))
      st = 0;
    else if (!strcmp(argv[1], "teen"))
      st = 1;
    else if (!strcmp(argv[1], "adult"))
      st = 2;
    else if (!strcmp(argv[1], "elder"))
      st = 3;
    else
    {
      // numeric
      int v = atoi(argv[1]);
      if (v < 0)
        v = 0;
      if (v > 3)
        v = 3;
      st = (uint8_t)v;
    }

    pet.setEvoStage(st);

    logf("[OK] Evo stage set to %u", (unsigned)pet.evoStage);
    return;
  }
#endif

  // -------------------------------------------------
  // SET NAME
  // Usage: name <pet name...>
  // -------------------------------------------------
  if (!strcmp(argv[0], "name"))
  {
    if (argc < 2)
    {
      logLine("Usage: name <pet name>");
      return;
    }

    // Recombine argv[1..] to allow spaces
    char nm[PET_NAME_MAX + 1];
    nm[0] = '\0';

    int pos = 0;
    for (int i = 1; i < argc && pos < PET_NAME_MAX; i++)
    {
      if (i > 1 && pos < PET_NAME_MAX)
        nm[pos++] = ' ';
      for (const char *p = argv[i]; *p && pos < PET_NAME_MAX; p++)
      {
        nm[pos++] = *p;
      }
    }
    nm[pos] = '\0';

    // Optional: trim leading spaces (safety)
    while (nm[0] == ' ')
      memmove(nm, nm + 1, strlen(nm));

    if (!nm[0])
    {
      logLine("Name cannot be empty.");
      return;
    }

    pet.setName(nm);
    saveManagerMarkDirty();

    logf("[OK] Pet name set to: %s", pet.getName());
    return;
  }

  // -------------------------------------------------
  // PET TYPE (show / set)
  //  pet
  //  pet cycle
  //  pet devil|kaiju|eldritch|alien
  // -------------------------------------------------
  if (!strcmp(argv[0], "pet"))
  {
    if (argc == 1)
    {
      logf("Pet type: %s (%d)", petTypeToString(pet.type), (int)pet.type);
      return;
    }

#if PUBLIC_BUILD
    logLine("Command disabled in public build.");
    return;
#else
    if (!strcmp(argv[1], "cycle"))
    {
      int next = ((int)pet.type + 1) % 4;
      pet.type = (PetType)next;
      saveManagerMarkDirty();
      requestUIRedraw();
      logf("[OK] Pet type set to: %s", petTypeToString(pet.type));
      return;
    }

    PetType t;
    if (!parsePetType(argv[1], t))
    {
      logLine("Usage: pet cycle|devil|kaiju|eldritch|alien");
      return;
    }

    pet.type = t;
    saveManagerMarkDirty();
    requestUIRedraw();
    logf("[OK] Pet type set to: %s", petTypeToString(pet.type));
    return;
#endif
  }

  // -------------------------------------------------
  // SET HUNGER
  // -------------------------------------------------
#if !PUBLIC_BUILD
  if (!strcmp(argv[0], "sethunger"))
  {
    if (argc < 2)
    {
      logLine("Usage: sethunger <0-100>");
      return;
    }
    int v = constrain(atoi(argv[1]), 0, 100);
    pet.hunger = v;
    pet.clampStats();
    saveManagerMarkDirty();
    logf("[OK] Hunger set to %d", pet.hunger);
    return;
  }
#endif

  // -------------------------------------------------
  // SET MOOD (happiness)
  // -------------------------------------------------
#if !PUBLIC_BUILD
  if (!strcmp(argv[0], "setmood"))
  {
    if (argc < 2)
    {
      logLine("Usage: setmood <0-100>");
      return;
    }
    int v = constrain(atoi(argv[1]), 0, 100);
    pet.happiness = v;
    pet.clampStats();
    saveManagerMarkDirty();
    logf("[OK] Mood set to %d", pet.happiness);
    return;
  }
#endif

  // -------------------------------------------------
  // SET REST (energy)
  // -------------------------------------------------
#if !PUBLIC_BUILD
  if (!strcmp(argv[0], "setrest"))
  {
    if (argc < 2)
    {
      logLine("Usage: setrest <0-100>");
      return;
    }
    int v = constrain(atoi(argv[1]), 0, 100);
    pet.energy = v;
    pet.clampStats();
    saveManagerMarkDirty();
    logf("[OK] Energy set to %d", pet.energy);
    return;
  }
#endif

  // -------------------------------------------------
  // SET HEALTH
  // -------------------------------------------------
#if !PUBLIC_BUILD
  if (!strcmp(argv[0], "sethealth"))
  {
    if (argc < 2)
    {
      logLine("Usage: sethealth <0-100>");
      return;
    }
    int v = constrain(atoi(argv[1]), 0, 100);
    pet.health = v;
    pet.clampStats();
    saveManagerMarkDirty();
    logf("[OK] Health set to %d", pet.health);
    return;
  }
#endif

  // -------------------------------------------------
  // PET TORTURE (For...testing)
  // -------------------------------------------------

#if !PUBLIC_BUILD
  if (!strcmp(argv[0], "hurtpet"))
  {
    int hp = 25;
    if (argc >= 2)
      hp = constrain(atoi(argv[1]), 0, 100);

    pet.hunger = 0;
    pet.energy = 0;
    pet.happiness = 0;
    pet.health = hp;

    pet.clampStats();

    saveManagerMarkDirty();
    requestUIRedraw();

    logf("[OK] Pet hurt (HP=%d, other stats=0)", pet.health);
    return;
  }
#endif

#if !PUBLIC_BUILD
  if (!strcmp(argv[0], "healpet"))
  {
    pet.hunger = 100;
    pet.energy = 100;
    pet.happiness = 100;
    pet.health = 100;

    pet.clampStats();

    saveManagerMarkDirty();
    requestUIRedraw();

    logLine("[OK] Pet healed (HP/stats restored to 100)");
    return;
  }
#endif

  if (!strcmp(argv[0], "saveheal"))
  {
    const bool hadBlankName = (pet.getName() == nullptr || pet.getName()[0] == '\0');
    const bool changed = saveManagerAutoHeal();

    requestUIRedraw();

    if (changed)
    {
      logf("[OK] Save healed. name='%s'", pet.getName());
    }
    else
    {
      logf("[OK] No save heal needed. name='%s'", pet.getName());
    }

    if (hadBlankName && pet.getName() && pet.getName()[0] != '\0')
    {
      logLine("[OK] Blank pet name was repaired.");
    }

    return;
  }

#if !PUBLIC_BUILD
  if (!strcmp(argv[0], "killpet"))
  {
    pet.health = 0;
    pet.clampStats();
    saveManagerMarkDirty();

    // Enter death flow immediately (same as main loop)
    if (g_app.uiState != UIState::DEATH)
    {
      petEnterDeathState();
      invalidateBackgroundCache();
      requestUIRedraw();
      clearInputLatch();
    }

    logLine("[OK] Pet killed (death flow entered).");
    return;
  }
#endif

  // WIFI
  if (!strcmp(argv[0], "wifi"))
  {
    // wifi  (status)
    if (argc == 1)
    {
      String ssid, pass;
      bool has = wifiLoadCreds(ssid, pass);

      logf("WiFi enabled: %s", wifiIsEnabled() ? "ON" : "OFF");
      logf("WiFi status:  %s", wifiIsConnected() ? "CONNECTED" : "NOT CONNECTED");

      if (wifiIsConnectedNow())
      {
        logf("SSID:         %s", wifiConsoleSsid());
        logf("IP:           %s", wifiConsoleIpString());
        logf("RSSI:         %d", wifiConsoleRssi());
      }
      else
      {
        logf("Saved SSID:   %s", has ? ssid.c_str() : "(none)");
      }

      return;
    }

    if (!strcmp(argv[1], "off"))
    {
      wifiSetEnabled(false);
      settingsSetWifiEnabled(false);
      applyWifiPower(false);
      wifiConsoleDisconnect(false);
      saveSettingsToSD();
      logLine("[OK] WiFi OFF");
      saveManagerMarkDirty(); // optional
      return;
    }

    // wifi on
    if (!strcmp(argv[1], "on"))
    {
      wifiSetEnabled(true);
      settingsSetWifiEnabled(true);
      applyWifiPower(true);
      saveSettingsToSD();

      // If creds exist, try to connect automatically
      String ssid, pass;
      if (wifiLoadCreds(ssid, pass))
      {
        wifiBeginConnect(ssid.c_str(), pass.c_str());
        logLine("[OK] WiFi ON (connecting using saved creds)");
      }
      else
      {
        logLine("[OK] WiFi ON (no saved creds)");
      }

      saveManagerMarkDirty(); // optional
      return;
    }

    // wifi clear
    if (!strcmp(argv[1], "clear"))
    {
      wifiClearCreds();
      wifiConsoleDisconnect(true);
      logLine("[OK] WiFi creds cleared");
      saveManagerMarkDirty(); // optional
      return;
    }

    // wifi <ssid> <pass>
    if (argc >= 3)
    {
      const char *ssid = argv[1];
      const char *pass = argv[2];

      wifiSaveCreds(ssid, pass);
      saveManagerMarkDirty(); // optional

      wifiBeginConnect(ssid, pass);
      logLine("[OK] saved creds; connecting...");
      return;
    }

    logLine("Usage:");
    logLine("  wifi");
    logLine("  wifi on|off");
    logLine("  wifi clear");
    logLine("  wifi <ssid> <pass>");
    return;
  }

  // AGE (birth epoch + formatted age)
  if (!strcmp(argv[0], "age"))
  {
    time_t now = time(nullptr);

    uint32_t birth = saveManagerGetBirthEpoch();

    logf("now:   %lu", (unsigned long)now);
    logf("birth: %lu", (unsigned long)birth);

    AgeParts a = calcAgeParts((time_t)birth, now);
    char s[32];
    formatAgeString(s, sizeof(s), a, false);
    logf("age:   %s", s);
    return;
  }

  if (!strcmp(argv[0], "version"))
  {
    logLine("Raising Hell");

    logf("Firmware: %s", RH_VERSION_STRING);
    logf("Build ID:  %s", bootCurrentBuildId());

    const char *assetVer = assetOtaInstalledVersion();

    if (assetVer && assetVer[0])
      logf("Assets:   %s", assetVer);
    else
      logLine("Assets:   none installed");

    return;
  }

  if (!strcmp(argv[0], "logclear"))
  {
    runtimeLogClear();
    logLine("[OK] runtime log cleared");
    return;
  }

  if (!strcmp(argv[0], "logdump"))
  {
    const int count = runtimeLogCount();

    logLine("--- LOGDUMP BEGIN ---");
    if (count <= 0)
    {
      logLine("(empty)");
      logLine("--- LOGDUMP END ---");
      return;
    }

    for (int i = 0; i < count; i++)
    {
      char line[128];
      snprintf(line, sizeof(line), "[%03d] %s", i, runtimeLogGetLine(i));
      logLine(line);
    }

    logLine("--- LOGDUMP END ---");
    return;
  }

  if (!strcmp(argv[0], "logsave"))
  {
    static const char *kLogPath = "/raising_hell/logs/logdump.txt";

    String err;
    if (!consoleSaveRuntimeLogToSd(kLogPath, &err))
    {
      logf("logsave failed: %s", err.c_str());
      return;
    }

    logf("[OK] runtime log saved: %s", kLogPath);
    return;
  }

  if (!strcmp(argv[0], "logtail"))
  {
    int n = 20;
    if (argc >= 2)
    {
      n = atoi(argv[1]);
      if (n <= 0)
        n = 20;
    }

    const int count = runtimeLogCount();
    const int start = (count > n) ? (count - n) : 0;

    logLine("--- LOGTAIL BEGIN ---");
    if (count <= 0)
    {
      logLine("(empty)");
      logLine("--- LOGTAIL END ---");
      return;
    }

    for (int i = start; i < count; i++)
    {
      char line[128];
      snprintf(line, sizeof(line), "[%03d] %s", i, runtimeLogGetLine(i));
      logLine(line);
    }

    logLine("--- LOGTAIL END ---");
    return;
  }

  if (!strcmp(argv[0], "assetstatus"))
  {
    const AssetOtaConfig &cfg = assetOtaGetConfig();
    const char *installed = assetOtaInstalledVersion();
    String storedFwId;
    const bool haveStoredFwId = bootFirmwareMarkerRead(storedFwId);

    logLine("Asset OTA status:");
    logf("  boot flag:       %s", assetProvisionBootRequested() ? "SET" : "CLEAR");
    logf("  build current:   %s", bootCurrentBuildId());
    logf("  build stored:    %s",
         haveStoredFwId ? (storedFwId.length() ? storedFwId.c_str() : "(none)") : "(read failed)");

    logf("  installed:       %s", (installed && installed[0]) ? installed : "(none)");
    logf("  min required:    %s", RH_MIN_REQUIRED_ASSET_PACK);
    logf("  too old:         %s", consoleAssetPackTooOld() ? "YES" : "NO");

    logf("  channel:         %s", assetChannelToString(cfg.channel));
    logf("  manifest url:    %s", assetOtaManifestUrlForChannel((AssetOtaChannel)cfg.channel));

    logf("  sd ready:        %s", g_sdReady ? "YES" : "NO");
    logf("  local manifest:  %s", (g_sdReady && SD.exists(assetOtaLocalManifestPath())) ? "YES" : "NO");

    logf("  ota status:      %s", assetOtaStatusString());
    logf("  ota error:       %s", assetOtaLastErrorString());
    logf("  progress:        %u / %u", (unsigned)assetOtaCurrentFileIndex(), (unsigned)assetOtaTotalFileCount());

    const bool needsRepair = consoleAssetPackTooOld() || assetProvisionBootRequested() ||
                             (g_sdReady && !SD.exists(assetOtaLocalManifestPath())) ||
                             (strcmp(assetOtaLastErrorString(), "None") != 0);

    if (needsRepair)
    {
      logLine("  suggestion:      asset recovery available");
      logLine("  suggestion:      run 'repair' then reboot");
    }

    return;
  }

  if (!strcmp(argv[0], "uptime"))
  {
    uint32_t ms = millis();
    uint32_t sec = ms / 1000;
    uint32_t min = sec / 60;
    uint32_t hr = min / 60;

    sec %= 60;
    min %= 60;

    logf("Uptime: %lu:%02lu:%02lu", (unsigned long)hr, (unsigned long)min, (unsigned long)sec);

    return;
  }

  if (!strcmp(argv[0], "tz"))
  {
    if (!consoleRequireSupportMode())
      return;

    if (argc == 1)
    {
      consoleShowCurrentTimezoneStatus();
      return;
    }

    if (!strcmp(argv[1], "list"))
    {
      logf("Supported timezones: %u", (unsigned)tzCount());
      for (uint8_t i = 0; i < tzCount(); ++i)
      {
        logf("  [%u] %s -> %s", (unsigned)i, tzName(i), tzIanaName(i));
      }
      return;
    }

    if (!strcmp(argv[1], "map"))
    {
      if (argc < 3)
      {
        logLine("Usage: tz map <IANA>");
        return;
      }

      const int idx = tzFindIndexByIana(argv[2]);
      if (idx < 0)
      {
        logf("tz map: unsupported IANA zone '%s'", argv[2]);
        return;
      }

      logf("tz map: '%s' -> idx=%d", argv[2], idx);
      consoleLogTimezoneStatusForIndex(idx, "  ");
      return;
    }

    if (!strcmp(argv[1], "test"))
    {
      if (argc < 3)
      {
        logLine("Usage: tz test <IANA|idx>");
        return;
      }

      int idx = -1;
      if (!consoleResolveTimezoneSpec(argv[2], idx))
      {
        logf("tz test: unknown timezone '%s'", argv[2]);
        return;
      }

      logf("tz test: previewing '%s'", argv[2]);
      consoleLogTimezonePreview(idx, "  ");
      logf("  restore idx=%d", tzIndexIsValid(tzIndex) ? tzIndex : (int)tzDefaultIndex());
      return;
    }

    if (!strcmp(argv[1], "save") || !strcmp(argv[1], "set"))
    {
      if (argc < 3)
      {
        logLine(!strcmp(argv[1], "set") ? "Usage: tz set <idx>" : "Usage: tz save <IANA|idx>");
        return;
      }

      int idx = -1;
      if (!consoleResolveTimezoneSpec(argv[2], idx))
      {
        logf("tz save: unknown timezone '%s'", argv[2]);
        return;
      }

      tzIndex = idx;
      applyTimezoneIndex((uint8_t)tzIndex);
      saveTzIndexToNVS((uint8_t)tzIndex);
      saveManagerMarkDirty();

      logLine("tz save: applied + persisted");
      consoleLogTimezoneStatusForIndex(tzIndex, "  ");
      consoleLogTimeSnapshot("  ");
      return;
    }

    if (!strcmp(argv[1], "cases"))
    {
      consoleRunTimezoneCaseSuite();
      return;
    }

    logLine("Usage:");
    logLine("  tz");
    logLine("  tz list");
    logLine("  tz map <IANA>");
    logLine("  tz test <IANA|idx>");
    logLine("  tz save <IANA|idx>");
    logLine("  tz set <idx>");
    logLine("  tz cases");
    return;
  }

  if (!strcmp(argv[0], "timeinvalidate"))
  {
    if (!consoleRequireSupportMode())
      return;

    struct timeval tv = {0, 0};
    settimeofday(&tv, nullptr);

    g_timeAnchorAttempted = false;
    g_timeAnchorRestored = false;

    logLine("[TIME] invalidated");
    return;
  }

  // -------------------------------------------------
  // NTP SKIP (recover from stalled time sync)
  // -------------------------------------------------
  if (!strcmp(argv[0], "ntpskip"))
  {
    if (!consoleRequireSupportMode())
      return;
    if (time(nullptr) > 1600000000)
    {
      logLine("[NTP] time already valid");
      return;
    }

    time_t before = time(nullptr);

    // Force a valid epoch (anything > ~2019 works)
    const time_t fallback = 1700000000;
    struct timeval tv = {fallback, 0};
    settimeofday(&tv, nullptr);

    // Align with boot pipeline expectations
    g_timeAnchorAttempted = true;
    g_timeAnchorRestored = true;

    time_t after = time(nullptr);

    logf("[NTP] skip applied");
    logf("time: %lu -> %lu", (unsigned long)before, (unsigned long)after);

    return;
  }

  if (!strcmp(argv[0], "bootflags"))
  {
    if (!consoleRequireSupportMode())
      return;

    logLine("Boot / recovery flags:");
    logf("  save exists:        %s", saveManagerSaveFileExists() ? "YES" : "NO");
    logf("  import exists:      %s", saveManagerHasImportableBubJson() ? "YES" : "NO");
    logf("  name pending:       %s", saveManagerNamePendingFlagExists() ? "SET" : "CLEAR");
    logf("  asset boot flag:    %s", assetProvisionBootRequested() ? "SET" : "CLEAR");
    logf("  boot setup pending: %s", bootSetupPendingFlagExists() ? "SET" : "CLEAR");
    logf("  postprov help:      %s", bootPostProvisionControlsHelpPending() ? "SET" : "CLEAR");
    logf("  controls help seen: %s", g_controlsHelpSeen ? "YES" : "NO");
    return;
  }

  if (!strcmp(argv[0], "clearnamepending"))
  {
    if (!consoleRequireSupportMode())
      return;

    const bool before = saveManagerNamePendingFlagExists();
    saveManagerClearNamePendingFlag();
    const bool after = saveManagerNamePendingFlagExists();

    logf("name pending: before=%s after=%s", before ? "SET" : "CLEAR", after ? "SET" : "CLEAR");
    return;
  }

  if (!strcmp(argv[0], "clearpostprov"))
  {
    if (!consoleRequireSupportMode())
      return;

    const bool before = bootPostProvisionControlsHelpPending();
    bootPostProvisionControlsHelpClear();
    const bool after = bootPostProvisionControlsHelpPending();

    logf("postprov help: before=%s after=%s", before ? "SET" : "CLEAR", after ? "SET" : "CLEAR");
    return;
  }

  if (!strcmp(argv[0], "clearbootsetup"))
  {
    if (!consoleRequireSupportMode())
      return;

    const bool before = bootSetupPendingFlagExists();
    bootSetupClearPendingFlag();
    const bool after = bootSetupPendingFlagExists();

    logf("boot setup pending: before=%s after=%s", before ? "SET" : "CLEAR", after ? "SET" : "CLEAR");
    return;
  }

  if (!strcmp(argv[0], "bootheal"))
  {
    if (!consoleRequireSupportMode())
      return;
    if (!consoleRequireSupportMode())
      return;

    const bool namePendingBefore = saveManagerNamePendingFlagExists();
    const bool postProvBefore = bootPostProvisionControlsHelpPending();
    const bool bootSetupBefore = bootSetupPendingFlagExists();

    saveManagerClearNamePendingFlag();
    bootPostProvisionControlsHelpClear();
    bootSetupClearPendingFlag();

    const bool namePendingAfter = saveManagerNamePendingFlagExists();
    const bool postProvAfter = bootPostProvisionControlsHelpPending();
    const bool bootSetupAfter = bootSetupPendingFlagExists();

    logLine("bootheal:");
    logf("  name pending:       %s -> %s", namePendingBefore ? "SET" : "CLEAR", namePendingAfter ? "SET" : "CLEAR");
    logf("  postprov help:      %s -> %s", postProvBefore ? "SET" : "CLEAR", postProvAfter ? "SET" : "CLEAR");
    logf("  boot setup pending: %s -> %s", bootSetupBefore ? "SET" : "CLEAR", bootSetupAfter ? "SET" : "CLEAR");

    if (!namePendingBefore && !postProvBefore && !bootSetupBefore)
      logLine("  no changes");

    return;
  }

  // MONITOR
  if (!strcmp(argv[0], "mon"))
  {
    logLine("----- MON -----");
    logf("INF:    %d", getInf());
    logf("HP:     %d", pet.health);
    logf("Hunger: %d", pet.hunger);
    logf("Mood:   %d", pet.happiness);
    logf("Energy: %d", pet.energy);
    logLine("-------------");
    return;
  }

  // EXIT
  if (!strcmp(argv[0], "exit"))
  {
    consoleClose();
    return;
  }

  // -------------------------
  // ASSET / SYSTEM COMMANDS
  // -------------------------
#if !PUBLIC_BUILD
  if (!strcmp(argv[0], "otach"))
  {
    if (argc < 2)
    {
      logLine("Usage: otach public|dev");
      return;
    }

    if (!g_sdReady)
    {
      logLine("SD not ready");
      return;
    }

    AssetOtaConfig cfg{};
    assetOtaConfigDefaults(cfg);

    // Try to load existing config (optional)
    assetOtaConfigLoad(&cfg);

    if (!strcmp(argv[1], "dev"))
    {
      cfg.channel = (uint8_t)AssetOtaChannel::DEV;
      logLine("[OK] OTA channel set to DEV");
    }
    else if (!strcmp(argv[1], "public"))
    {
      cfg.channel = (uint8_t)AssetOtaChannel::PUBLIC;
      logLine("[OK] OTA channel set to PUBLIC");
    }
    else
    {
      logLine("Usage: otach public|dev");
      return;
    }

    if (!assetOtaConfigSave(cfg))
    {
      logLine("FAILED to save config");
      return;
    }

    logLine("[OK] config saved");
    logLine("Reboot required to apply");
    return;
  }

  if (!strcmp(argv[0], "otadev"))
  {
    if (!g_sdReady)
    {
      logLine("SD not ready");
      return;
    }

    AssetOtaConfig cfg{};
    assetOtaConfigDefaults(cfg);
    assetOtaConfigLoad(&cfg);

    cfg.channel = (uint8_t)AssetOtaChannel::DEV;

    logf("[OK] manifest: %s", assetOtaManifestUrlForChannel((AssetOtaChannel)cfg.channel));

    if (!assetOtaConfigSave(cfg))
    {
      logLine("FAILED to save config");
      return;
    }

    requestAssetProvisionOnNextBoot();

    logLine("[OK] switched to DEV channel");
    logLine("[OK] asset provisioning requested");
    logLine("[OK] rebooting...");

    delay(100);
    ESP.restart();
    return;
  }
#endif

  if (!strcmp(argv[0], "assetflag"))
  {
    if (!consoleRequireSupportMode())
      return;

    if (argc == 1)
    {
      logf("asset provision boot flag: %s", assetProvisionBootRequested() ? "SET" : "CLEAR");
      return;
    }

    if (!strcmp(argv[1], "clear"))
    {
      clearAssetProvisionBootRequest();
      logLine("[OK] asset provision boot flag cleared");
      return;
    }

    if (!strcmp(argv[1], "set"))
    {
      requestAssetProvisionOnNextBoot();
      logLine("[OK] asset provision boot flag set");
      return;
    }

    logLine("Usage: assetflag [clear|set]");
    return;
  }

  if (!strcmp(argv[0], "repair"))
  {
    if (!consoleRequireSupportMode())
      return;

    if (argc == 1 || (argc >= 2 && !strcmp(argv[1], "assets")))
    {
      consoleRequestAssetRepair();
      return;
    }

    logLine("Usage: repair [assets]");
    return;
  }

  if (!strcmp(argv[0], "rescue"))
  {
    if (!consoleRequireSupportMode())
      return;

    if (argc == 1 || (argc >= 2 && !strcmp(argv[1], "ota")))
    {
      consoleArmReprovisionRecovery(); // dev path
      return;
    }

    logLine("Usage: rescue [ota]");
    return;
  }

  if (!strcmp(argv[0], "fwmark"))
  {
    if (!consoleRequireSupportMode())
      return;

    if (argc == 1 || !strcmp(argv[1], "show"))
    {
      String stored;
      const bool ok = bootFirmwareMarkerRead(stored);

      logf("fw current: %s", bootCurrentBuildId());
      logf("fw stored:  %s", ok ? (stored.length() ? stored.c_str() : "(none)") : "(read failed)");
      logf("asset flag: %s", assetProvisionBootRequested() ? "SET" : "CLEAR");
      return;
    }

    if (!strcmp(argv[1], "clear"))
    {
      if (!bootFirmwareMarkerClear())
      {
        logLine("FAILED to clear firmware marker");
        return;
      }

      logLine("[OK] firmware marker cleared");
      logLine("[OK] reboot to simulate fresh flash");
      return;
    }

    if (!strcmp(argv[1], "reprovision"))
    {
      consoleArmReprovisionRecovery();
      return;
    }

#if !PUBLIC_BUILD
    if (!strcmp(argv[1], "set"))
    {
      if (argc < 3)
      {
        logLine("Usage: fwmark set <id>");
        return;
      }

      char idBuf[64];
      idBuf[0] = '\0';

      int pos = 0;
      for (int i = 2; i < argc && pos < (int)sizeof(idBuf) - 1; i++)
      {
        if (i > 2 && pos < (int)sizeof(idBuf) - 1)
          idBuf[pos++] = ' ';

        for (const char *p = argv[i]; *p && pos < (int)sizeof(idBuf) - 1; ++p)
          idBuf[pos++] = *p;
      }
      idBuf[pos] = '\0';

      if (!bootFirmwareMarkerWrite(idBuf))
      {
        logLine("FAILED to write firmware marker");
        return;
      }

      logf("[OK] firmware marker set: %s", idBuf);
      return;
    }
#endif

    logLine("Usage:");
    logLine("  fwmark");
    logLine("  fwmark show");
    logLine("  fwmark clear");
    logLine("  fwmark reprovision");
#if !PUBLIC_BUILD
    logLine("  fwmark set <id>");
#endif
    return;
  }

  if (!strcmp(argv[0], "nvsclear"))
  {
    if (!consoleRequireSupportMode())
      return;

    logLine("[WARN] Erasing NVS partition...");
    delay(50);

    esp_err_t err = nvs_flash_erase();

    if (err != ESP_OK)
    {
      logf("[ERR] nvs_flash_erase failed: %s", esp_err_to_name(err));
      return;
    }

    logLine("[OK] NVS erased");
    logLine("[OK] rebooting...");
    delay(150);

    ESP.restart();
    return;
  }

  if (!strcmp(argv[0], "reboot"))
  {
    logLine("[OK] rebooting...");
    delay(100);
    ESP.restart();
    return;
  }

  logf("Unknown command: %s", argv[0]);
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------
void consoleOpen()
{
  g_consoleOpen = true;
  g_consoleJustOpened = true;

#if defined(PUBLIC_BUILD) && PUBLIC_BUILD
  g_consoleSupportMode = false;
#endif

  requestFullUIRedraw();
  requestUIRedraw();

  consoleClear();

  logLine("");
  logLine("[Raising Hell Console]");
  logLine("Type 'help' for commands.");
}

void consoleClose()
{
  g_consoleOpen = false;

  requestFullUIRedraw();
  requestUIRedraw();

  logLine("[Console closed]");
}

bool consoleIsOpen() { return g_consoleOpen; }

void consoleClear()
{
  g_lineCount = 0;
  g_scrollOffset = 0;
  for (int i = 0; i < MAX_LINES; i++)
    g_lines[i][0] = '\0';
  resetLine();
}

int consoleGetLineCount() { return g_lineCount; }

const char *consoleGetLine(int idx)
{
  if (idx < 0 || idx >= g_lineCount)
    return "";
  return g_lines[idx];
}

const char *consoleGetInputLine() { return g_buf; }

int consoleGetScrollOffset() { return g_scrollOffset; }

int consoleGetMaxVisibleLines()
{
  // Keep this aligned with graphics_console_screens.cpp current layout.
  // Current Cardputer console body fits about 9 rows.
  return 9;
}

static int consoleGetMaxScrollOffsetInternal(int maxLinesVisible)
{
  if (maxLinesVisible < 1)
    maxLinesVisible = 1;

  const int maxOffset = g_lineCount - maxLinesVisible;
  return (maxOffset > 0) ? maxOffset : 0;
}

int consoleGetFirstVisibleLine(int maxLinesVisible)
{
  if (maxLinesVisible < 1)
    maxLinesVisible = 1;

  int first = g_lineCount - maxLinesVisible - g_scrollOffset;
  if (first < 0)
    first = 0;

  const int maxFirst = (g_lineCount > maxLinesVisible) ? (g_lineCount - maxLinesVisible) : 0;
  if (first > maxFirst)
    first = maxFirst;

  return first;
}

bool consoleIsScrolledUp() { return g_scrollOffset > 0; }

static void consoleClampScroll()
{
  const int maxOffset = consoleGetMaxScrollOffsetInternal(consoleGetMaxVisibleLines());

  if (g_scrollOffset < 0)
    g_scrollOffset = 0;
  if (g_scrollOffset > maxOffset)
    g_scrollOffset = maxOffset;
}

static void consoleSnapToBottom()
{
  if (g_scrollOffset != 0)
  {
    g_scrollOffset = 0;
    requestUIRedraw();
  }
}

static bool consoleScrollUpOne()
{
  const int maxOffset = consoleGetMaxScrollOffsetInternal(consoleGetMaxVisibleLines());
  if (g_scrollOffset >= maxOffset)
    return false;

  g_scrollOffset++;
  requestUIRedraw();
  return true;
}

static bool consoleScrollDownOne()
{
  if (g_scrollOffset <= 0)
    return false;

  g_scrollOffset--;
  requestUIRedraw();
  return true;
}

// -----------------------------------------------------------------------------
// Input handling
// -----------------------------------------------------------------------------
void consoleUpdate(InputState &in)
{
  if (!g_consoleOpen)
    return;

  // Swallow the first tick after opening so the "enter/select" that opened
  // the console can't also immediately submit a blank line.
  if (g_consoleJustOpened)
  {
    while (in.kbHasEvent())
      (void)in.kbPop();
    g_consoleJustOpened = false;
    requestUIRedraw(); // ensure the console frame appears immediately
    return;
  }

  bool touched = false;

  while (in.kbHasEvent())
  {
    KeyEvent e = in.kbPop();
    uint8_t code = e.code;

    const bool isEnter = (code == (uint8_t)RH_KEY_ENTER) || (code == '\n') || (code == '\r');

    const bool isBackspace = (code == (uint8_t)RH_KEY_BACKSPACE) || (code == '\b') || (code == 127);

    // -------------------------------------------------
    // Up/down behavior in console:
    //   empty input -> scrollback
    //   typed input -> command history
    // -------------------------------------------------
    if (code == (uint8_t)';')
    {
      if (g_len == 0)
      {
        if (consoleScrollUpOne())
          touched = true;
      }
      else
      {
        if (histPrev())
          touched = true;
      }
      continue;
    }
    if (code == (uint8_t)'.')
    {
      if (g_len == 0)
      {
        if (consoleScrollDownOne())
          touched = true;
      }
      else
      {
        if (histNext())
          touched = true;
      }
      continue;
    }

    // ENTER submits the current line
    if (isEnter)
    {
      g_buf[g_len] = '\0';

      consoleSnapToBottom();

      char echo[70];
      snprintf(echo, sizeof(echo), "> %s", g_buf);
      logLine(echo);

      histPush(g_buf);

      char line[64];
      strncpy(line, g_buf, sizeof(line) - 1);
      line[sizeof(line) - 1] = '\0';

      execLine(line);
      resetLine();

      touched = true;
      continue;
    }

    // BACKSPACE edits the current line
    if (isBackspace)
    {
      if (g_len > 0)
      {
        g_len--;
        g_buf[g_len] = '\0';
        histCancelNav();
        touched = true;
      }
      continue;
    }

    // Ignore other special tokens (FN/SHIFT/etc.)
    // Use RH_ constants so PlatformIO builds don't depend on legacy KEY_* defines.
    if (code >= (uint8_t)RH_KEY_BACKSPACE)
      continue;

    // Printable ASCII
    char c = (char)code;
    if (c >= 32 && c <= 126)
    {
      if (g_len < sizeof(g_buf) - 1)
      {
        if (g_scrollOffset > 0)
          consoleSnapToBottom();

        g_buf[g_len++] = c;
        g_buf[g_len] = '\0';
        histCancelNav();
        touched = true;
        if (touched)
        {
          requestUIRedraw();
        }
      }
    }
  }
}