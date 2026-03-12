#include "console.h"

#include "app_state.h"
#include "currency.h"
#include "debug.h"
#include "graphics.h"
#include "input.h"
#include "led_status.h"
#include "pet.h"
#include "pet_age.h"
#include "save_manager.h"
#include "ui_runtime.h"
#include "user_toggles_state.h"
#include "wifi_power.h"
#include "wifi_store.h"
#include "wifi_time.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <FS.h>
#include <SD.h>
#include "sdcard.h"
#include "settings_state.h"

// -----------------------------------------------------------------------------
// Console state
// -----------------------------------------------------------------------------
static bool g_consoleOpen = false;
static bool g_consoleJustOpened = false;

// Current editable input line
static char g_buf[64];
static uint8_t g_len = 0;

// Scrollback (fixed ring)
static constexpr int MAX_LINES = 16;
static char g_lines[MAX_LINES][64];
static int g_lineCount = 0;

// Command history (fixed ring)
static const int CONSOLE_HIST_MAX = 12;
static const int CONSOLE_HIST_LINE_MAX = 64;

static char g_hist[CONSOLE_HIST_MAX][CONSOLE_HIST_LINE_MAX];
static int g_histCount = 0; // number of valid entries (<= MAX)
static int g_histHead = 0;  // next write index (ring)
static int g_histNav = -1;  // -1 = not navigating; otherwise 0..histCount-1 (relative age)

static char g_histDraft[CONSOLE_HIST_LINE_MAX]; // what user typed before navigating
static bool g_histHasDraft = false;

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------
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
  case PET_KAIJU:
    return "kaiju";
  case PET_ELDRITCH:
    return "eldritch";
  case PET_ALIEN:
    return "alien";
  case PET_ANUBIS:
    return "anubis";
  case PET_AXOLOTL:
    return "axolotl";
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
  if (!strcmp(s, "kaiju"))
  {
    out = PET_KAIJU;
    return true;
  }
  if (!strcmp(s, "eldritch"))
  {
    out = PET_ELDRITCH;
    return true;
  }
  if (!strcmp(s, "alien"))
  {
    out = PET_ALIEN;
    return true;
  }
  if (!strcmp(s, "anubis"))
  {
    out = PET_ANUBIS;
    return true;
  }
  if (!strcmp(s, "axolotl"))
  {
    out = PET_AXOLOTL;
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
    logLine("  giveinf <amount>    add Inferium");
    logLine("  mon                 show stats");
    logLine("  clear               clear console");
    logLine("  exit                close console");
    logLine("  sethunger <0-100>   set hunger");
    logLine("  setmood   <0-100>   set mood");
    logLine("  setrest   <0-100>   set energy");
    logLine("  sethealth <0-100>   set health");
    logLine("  ledtest             cycle LED colors (~5s)");
    logLine("  wifi                 show wifi status + saved ssid");
    logLine("  wifi off|on           toggle wifi power");
    logLine("  wifi <ssid> <pass>    save + connect");
    logLine("  wifi clear            clear saved creds");
    logLine("  reset_settings      delete settings.bin");    logLine("  newpet!             OVERWRITE save + start a new pet");
    logLine("  age                 show birth epoch + age string");
    logLine("  name <pet name>      set pet name");
    logLine("  pet                 show current pet type");
    logLine("  pet cycle|devil|kaiju|eldritch|alien   set pet type");
    logLine("  givexp <amount>     give the pet XP (levels up if needed)");
    logLine("  setlevel <level>    set pet level (resets XP progress)");
    logLine("  setevo <0-3|baby|teen|adult|elder>  force evo stage");
    logLine("  killpet             instantly kill pet (test death/resurrection)");

    return;
  }

  // NEW PET (destructive)
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

  // CLEAR
  if (!strcmp(argv[0], "clear"))
  {
    consoleClear();
    return;
  }

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

  // GIVE INF
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

  // GIVE XP
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

  // SET LEVEL
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

  // -------------------------------------------------
  // SET EVO STAGE
  // Usage: setevo <0-3|baby|teen|adult|elder>
  // -------------------------------------------------
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
  }

  // -------------------------------------------------
  // SET HUNGER
  // -------------------------------------------------
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

  // -------------------------------------------------
  // SET MOOD (happiness)
  // -------------------------------------------------
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

  // -------------------------------------------------
  // SET REST (energy)
  // -------------------------------------------------
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

  // -------------------------------------------------
  // SET HEALTH
  // -------------------------------------------------
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

  // -------------------------------------------------
  // KILL PET (force death flow immediately)
  // -------------------------------------------------
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

    // wifi off
    if (!strcmp(argv[1], "off"))
    {
      wifiSetEnabled(false);
      applyWifiPower(false);
      wifiConsoleDisconnect(false);
      logLine("[OK] WiFi OFF");
      saveManagerMarkDirty(); // optional
      return;
    }

    // wifi on
    if (!strcmp(argv[1], "on"))
    {
      wifiSetEnabled(true);
      applyWifiPower(true);

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

    uint32_t birth = saveManagerGetBirthEpoch(); // you'll add this getter

    logf("now:   %lu", (unsigned long)now);
    logf("birth: %lu", (unsigned long)birth);

    AgeParts a = calcAgeParts((time_t)birth, now);
    char s[32];
    formatAgeString(s, sizeof(s), a, false);
    logf("age:   %s", s);
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

  logf("Unknown command: %s", argv[0]);
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------
void consoleOpen()
{
  inputSetTextCapture(true);
  g_textCaptureMode = true;
  g_consoleOpen = true;
  g_consoleJustOpened = true;

  consoleClear();

  logLine("");
  logLine("[Raising Hell Console]");
  logLine("Type 'help' for commands.");
}

void consoleClose()
{
  inputSetTextCapture(false);
  g_textCaptureMode = false; // <-- MUST be here
  g_consoleOpen = false;
  logLine("[Console closed]");
}

bool consoleIsOpen() { return g_consoleOpen; }

void consoleClear()
{
  g_lineCount = 0;
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

    const bool isEnter =
        (code == (uint8_t)RH_KEY_ENTER) || (code == '\n') || (code == '\r');

    const bool isBackspace =
        (code == (uint8_t)RH_KEY_BACKSPACE) || (code == '\b') || (code == 127);

    // -------------------------------------------------
    // Command history navigation (console-only)
    //   ';' = previous
    //   '.' = next
    // -------------------------------------------------
    if (code == (uint8_t)';')
    {
      if (histPrev()) touched = true;
      continue;
    }
    if (code == (uint8_t)'.')
    {
      if (histNext()) touched = true;
      continue;
    }

    // ENTER submits the current line
    if (isEnter)
    {
      g_buf[g_len] = '\0';

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
        g_buf[g_len++] = c;
        g_buf[g_len] = '\0';
        histCancelNav();
        touched = true;
        if (touched) {
          requestUIRedraw();
        }
      }
    }
  }
}