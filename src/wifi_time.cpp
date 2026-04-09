#include "wifi_time.h"
#include "app_state.h"
#include "debug.h"
#include "input_activity_state.h"
#include "time_persist.h"
#include "time_state.h"
#include "timezone.h"
#include "wifi_power.h"
#include "wifi_store.h"

#include <Arduino.h>
#include <WiFi.h>
#include <cstring>
#include <time.h>

static bool s_waitSntpBeforeSync = true;
static uint32_t s_consoleConnectStartMs = 0;
static bool s_wifiEventHooked = false;

// Enable toggle
bool wifiIsEnabled();
void wifiSetEnabled(bool en);
bool wifiGetEnabled();

// Status for UI
int wifiRssi();
bool timeIsSynced();

bool wifiIsConnectedNow() { return (WiFi.status() == WL_CONNECTED); }

// ---- State ----
static bool s_wifiConnected = false;
static int s_rssi = -127;

static bool s_timeSynced = false;
static uint32_t s_lastWifiAttemptMs = 0;
static uint32_t s_lastRssiMs = 0;
static uint32_t s_lastTimeCheckMs = 0;

static constexpr uint32_t WIFI_RETRY_MS = 5000;
static constexpr uint32_t RSSI_POLL_MS = 1000;

// ---- Event -> Tick handoff ----
static volatile bool s_evtGotIp = false;
static volatile bool s_evtDisc = false;

// Cache these for printing from tick
static IPAddress s_lastIp;
static IPAddress s_lastDns;

// ---- SNTP start control ----
static bool s_sntpStartedThisConnect = false;
static uint32_t s_sntpStartAtMs = 0;
static uint32_t s_sntpStartedAtMs = 0;

static bool s_wifiEnabled = false;
static char s_consoleSsidBuf[33] = {0};
static char s_consoleIpBuf[32] = {0};

// Forward declarations
static void tryWiFiConnect();
static void onWiFiEvent(WiFiEvent_t event);

static void startSntpOnce()
{
  if (s_sntpStartedThisConnect)
    return;

  applyTimezoneIndex(tzIndex);

  configTime(0, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");

  applyTimezoneIndex(tzIndex);

  s_sntpStartedThisConnect = true;
  s_waitSntpBeforeSync = false;

  DBGLN_ON("[TIME] SNTP started");
}

bool wifiIsEnabled() { return s_wifiEnabled; }
bool wifiGetEnabled() { return s_wifiEnabled; }

void wifiConsoleBeginConnect(const char *ssid, const char *pass)
{
  if (!ssid)
    ssid = "";
  if (!pass)
    pass = "";

  wifiSetEnabled(true);

  s_wifiConnected = false;
  s_timeSynced = false;
  s_rssi = -127;

  s_evtGotIp = false;
  s_evtDisc = false;

  s_sntpStartedThisConnect = false;
  s_sntpStartAtMs = 0;
  s_sntpStartedAtMs = 0;

  s_waitSntpBeforeSync = true;
  s_consoleConnectStartMs = millis();

  strncpy(s_consoleSsidBuf, ssid, sizeof(s_consoleSsidBuf) - 1);
  s_consoleSsidBuf[sizeof(s_consoleSsidBuf) - 1] = '\0';

  if (!s_wifiEventHooked)
  {
    WiFi.onEvent(onWiFiEvent);
    s_wifiEventHooked = true;
  }

  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  WiFi.mode(WIFI_STA);

  // Keep the driver alive; just clear current association/scan state.
  WiFi.disconnect(false, false);
  WiFi.scanDelete();
  delay(50);

  Serial.printf("[WIFI] begin connect ssid='%s' mode=%d status=%d\n",
                ssid, (int)WiFi.getMode(), (int)WiFi.status());

  WiFi.begin(ssid, pass);
  s_lastWifiAttemptMs = millis();
}

void wifiConsoleDisconnect(bool eraseCreds)
{
  WiFi.disconnect(false, eraseCreds);
  WiFi.scanDelete();

  s_wifiConnected = false;
  s_timeSynced = false;
  s_rssi = -127;

  s_evtGotIp = false;
  s_evtDisc = false;

  s_sntpStartedThisConnect = false;
  s_sntpStartAtMs = 0;
  s_sntpStartedAtMs = 0;

  s_waitSntpBeforeSync = true;
  s_consoleConnectStartMs = 0;

  if (eraseCreds)
  {
    wifiStoreClear();
    s_consoleSsidBuf[0] = '\0';
  }
}

void wifiStartSntpNow()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("[TIME] wifiStartSntpNow skipped: not connected");
    return;
  }

  s_timeSynced = false;
  s_rssi = WiFi.RSSI();

  configTime(0, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");
  applyTimezoneIndex(tzIndex);

  s_sntpStartedThisConnect = true;
  s_waitSntpBeforeSync = false;
  s_sntpStartAtMs = 0;
  s_sntpStartedAtMs = millis();
}

const char *wifiConsoleIpString()
{
  if (!wifiIsConnectedNow())
  {
    s_consoleIpBuf[0] = '\0';
    return s_consoleIpBuf;
  }

  IPAddress ip = WiFi.localIP();
  snprintf(s_consoleIpBuf, sizeof(s_consoleIpBuf), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  return s_consoleIpBuf;
}

const char *wifiConsoleSsid()
{
  if (wifiIsConnectedNow())
  {
    String s = WiFi.SSID();
    strncpy(s_consoleSsidBuf, s.c_str(), sizeof(s_consoleSsidBuf) - 1);
    s_consoleSsidBuf[sizeof(s_consoleSsidBuf) - 1] = '\0';
  }
  return s_consoleSsidBuf;
}

int wifiConsoleRssi()
{
  if (!wifiIsConnectedNow())
    return 0;
  return WiFi.RSSI();
}

void wifiSetEnabled(bool en)
{
  s_wifiEnabled = en;

  if (!s_wifiEnabled)
  {
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);

    s_wifiConnected = false;
    s_timeSynced = false;
    s_rssi = -127;

    s_sntpStartedThisConnect = false;
    s_sntpStartAtMs = 0;
    s_sntpStartedAtMs = 0;

    s_evtGotIp = false;
    s_evtDisc = false;

    s_waitSntpBeforeSync = true;
  }
  else
  {
    WiFi.mode(WIFI_STA);
  }
}

uint32_t wifiConsoleConnectAgeMs()
{
  if (s_consoleConnectStartMs == 0)
    return 0;
  return (uint32_t)(millis() - s_consoleConnectStartMs);
}

int wifiConsoleStatus() { return (int)WiFi.status(); }

const char *wifiConsoleStatusString()
{
  switch (WiFi.status())
  {
  case WL_IDLE_STATUS:
    return "IDLE";
  case WL_NO_SSID_AVAIL:
    return "NO SSID";
  case WL_SCAN_COMPLETED:
    return "SCAN DONE";
  case WL_CONNECTED:
    return "CONNECTED";
  case WL_CONNECT_FAILED:
    return "AUTH FAILED";
  case WL_CONNECTION_LOST:
    return "CONNECTION LOST";
  case WL_DISCONNECTED:
    return "DISCONNECTED";
  default:
    return "UNKNOWN";
  }
}

// WiFi event handler (Arduino-ESP32)
// IMPORTANT: no Serial/configTime here.
static void onWiFiEvent(WiFiEvent_t event)
{
  switch (event)
  {
  case ARDUINO_EVENT_WIFI_STA_GOT_IP:
    s_lastIp = WiFi.localIP();
    s_lastDns = WiFi.dnsIP();
    s_evtGotIp = true;
    break;

  case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
    s_evtDisc = true;
    break;

  default:
    break;
  }
}

static void tryWiFiConnect()
{
#if defined(WIFI_SSID) && defined(WIFI_PASS)
  if (strlen(WIFI_SSID) == 0)
  {
    DBGLN_ON("[WIFI] SSID empty, not connecting");
    return;
  }

  DBG_ON("[WIFI] begin ssid='%s' len=%d\n", WIFI_SSID, (int)strlen(WIFI_SSID));
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  DBGLN_ON("[WIFI] WiFi.begin() called");
#endif
}

void wifiTimeInit()
{
  if (!s_wifiEnabled)
    return;

  DBGLN_ON("[WIFI] wifiTimeInit()");

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  WiFi.setSleep(false);

  if (!s_wifiEventHooked)
  {
    WiFi.onEvent(onWiFiEvent);
    s_wifiEventHooked = true;
  }

  s_lastWifiAttemptMs = 0;
  s_lastRssiMs = 0;
  s_lastTimeCheckMs = 0;

  s_evtGotIp = false;
  s_evtDisc = false;

  s_sntpStartedThisConnect = false;
  s_sntpStartAtMs = 0;
  s_sntpStartedAtMs = 0;

  s_waitSntpBeforeSync = true;

  s_wifiConnected = (WiFi.status() == WL_CONNECTED);

  if (s_wifiConnected)
  {
    s_timeSynced = false;
    s_sntpStartedThisConnect = false;
    s_sntpStartAtMs = millis() + 750;
    s_rssi = WiFi.RSSI();
  }
  else
  {
    s_rssi = -127;
  }

  tryWiFiConnect();
  s_lastWifiAttemptMs = millis();
}

void wifiTimeTick()
{
  if (!s_wifiEnabled)
    return;

  const uint32_t now = millis();
  const bool interactive = ((uint32_t)(now - g_lastInputActivityMs) < 250UL);

  static uint32_t s_lastStatusCheckMs = 0;
  static uint32_t s_lastSntpLogMs = 0;

  const uint32_t STATUS_CHECK_MS = 500;
  const uint32_t TIME_CHECK_MS = 1000;
  const uint32_t SNTP_START_DELAY_MS = 750;

  if (s_evtDisc)
  {
    s_evtDisc = false;

    s_wifiConnected = false;
    s_timeSynced = false;
    s_rssi = -127;

    s_sntpStartedThisConnect = false;
    s_sntpStartAtMs = 0;
    s_sntpStartedAtMs = 0;

    s_waitSntpBeforeSync = true;

    DBGLN_ON("[WIFI] DISCONNECTED event");
  }

  if (s_evtGotIp)
  {
    s_evtGotIp = false;

    s_wifiConnected = true;
    s_timeSynced = false;
    s_rssi = -127;

    s_sntpStartedThisConnect = false;
    s_sntpStartAtMs = now + SNTP_START_DELAY_MS;
    s_sntpStartedAtMs = 0;

    s_waitSntpBeforeSync = true;

    DBG_ON("[WIFI] GOT_IP %s\n", s_lastIp.toString().c_str());
    DBG_ON("[WIFI] DNS %s\n", s_lastDns.toString().c_str());

    if (now - s_lastSntpLogMs > 750)
    {
      DBGLN_ON("[TIME] SNTP pending");
      s_lastSntpLogMs = now;
    }
  }

  if (!interactive && (now - s_lastStatusCheckMs >= STATUS_CHECK_MS))
  {
    s_lastStatusCheckMs = now;

    const bool statusConnected = (WiFi.status() == WL_CONNECTED);
    if (statusConnected && !s_wifiConnected)
    {
      s_wifiConnected = true;
      s_timeSynced = false;
      s_rssi = -127;

      s_sntpStartedThisConnect = false;
      s_sntpStartAtMs = now + SNTP_START_DELAY_MS;
      s_sntpStartedAtMs = 0;

      s_waitSntpBeforeSync = true;

      if (now - s_lastSntpLogMs > 750)
      {
        DBGLN_ON("[TIME] SNTP pending");
        s_lastSntpLogMs = now;
      }
    }
    else if (!statusConnected && s_wifiConnected)
    {
      s_wifiConnected = false;
      s_timeSynced = false;
      s_rssi = -127;

      s_sntpStartedThisConnect = false;
      s_sntpStartAtMs = 0;
      s_sntpStartedAtMs = 0;

      s_waitSntpBeforeSync = true;
    }
  }

  const bool reallyConnected = (WiFi.status() == WL_CONNECTED);

  if (!reallyConnected)
  {
    if (!interactive && (now - s_lastWifiAttemptMs >= WIFI_RETRY_MS))
    {
      s_lastWifiAttemptMs = now;
      tryWiFiConnect();
    }
    return;
  }

  if (!interactive && !s_sntpStartedThisConnect && s_sntpStartAtMs != 0 && (int32_t)(now - s_sntpStartAtMs) >= 0)
  {
    startSntpOnce();
    if (s_sntpStartedThisConnect)
      s_sntpStartedAtMs = now;
  }

  if (!interactive && (now - s_lastRssiMs >= RSSI_POLL_MS))
  {
    s_lastRssiMs = now;
    s_rssi = WiFi.RSSI();
  }

  if (!s_timeSynced && !interactive && (now - s_lastTimeCheckMs >= TIME_CHECK_MS))
  {
    s_lastTimeCheckMs = now;

    time_t t = time(nullptr);
    if (t > 1704067200)
    {
      s_timeSynced = true;
      applyTimezoneIndex(tzIndex);
      Serial.println("[TIME] SYNCED");
    }
  }
}

bool wifiIsConnected() { return WiFi.status() == WL_CONNECTED; }
int wifiRssi() { return s_rssi; }

bool timeIsSynced()
{
  time_t t = time(nullptr);
  return s_timeSynced || (t > 1704067200);
}

bool timeIsNtpSyncedStrict()
{
  return s_timeSynced;
}