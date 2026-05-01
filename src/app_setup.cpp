#include "app_setup.h"

// -----------------------------------------------------------------------------
// Raising Hell — Cardputer ADV Edition
// -----------------------------------------------------------------------------

// --- Standard / core ----------------------------------------------------------
#include <Arduino.h>
#include <stdint.h>

// --- ESP / system -------------------------------------------------------------
#include "esp_core_dump.h"
#include "esp_err.h"
#include "esp_log.h"
#include <esp_heap_caps.h>
#include <esp_system.h>

// --- Hardware / platform ------------------------------------------------------
#include "M5Cardputer.h"
#include <EEPROM.h>
#include <SD.h>
#include <WiFi.h>

// --- Core app systems ---------------------------------------------------------
#include "app_state.h"
#include "boot_pipeline.h"
#include "runtime_log.h"
#include "support_logging_state.h"
#include "system_status_state.h"
#include "version.h"

// --- Display / input / UX -----------------------------------------------------
#include "auto_screen.h"
#include "brightness_state.h"
#include "controls_help_state.h"
#include "display.h"
#include "display_state.h"
#include "input.h"
#include "input_activity_state.h"

// --- UI / flow ----------------------------------------------------------------
#include "evolution_flow.h"
#include "flow_power_menu.h"
#include "hatching_flow.h"
#include "menu_actions.h"
#include "ui_runtime.h"

// --- Gameplay / pet systems ---------------------------------------------------
#include "activity.h"
#include "inventory.h"
#include "inventory_state.h"
#include "mini_games.h"
#include "motion.h"
#include "pet.h"
#include "pet_defs.h"

// --- Save / persistence -------------------------------------------------------
#include "eeprom_addrs.h"
#include "save_manager.h"
#include "time_persist.h"

// --- Time / networking --------------------------------------------------------
#include "time_state.h"
#include "timezone.h"
#include "wifi_power.h"
#include "wifi_time.h"

// --- Storage / assets ---------------------------------------------------------
#include "asset_ota.h"
#include "asset_ota_config.h"
#include "sdcard.h"

// --- Rendering / animation ----------------------------------------------------
#include "anim_engine.h"
#include "graphics.h"

// --- Audio / feedback ---------------------------------------------------------
#include "led_status.h"
#include "sound.h"

// --- Misc states --------------------------------------------------------------
#include "console.h"
#include "debug.h"
#include "name_entry_state.h"
#include "power_button.h"
#include "settings_state.h"
#include "sleep_state.h"

//-- Battery Constants
static constexpr int kBootBatteryEnterGateMv = 3200;
static constexpr int kBootBatteryResumeMv = 3250;
static constexpr uint32_t kBootBatteryStableMs = 1500;
static constexpr uint32_t kBootBatteryPollMs = 250;
static constexpr int kBootBatteryTargetPercent = 5;
static constexpr uint8_t kBootChargeGateDimBacklight = 35;

void updateBattery();

static void clearStaleCoreDumpIfNeeded()
{
  esp_err_t chk = esp_core_dump_image_check();

  if (chk == ESP_OK)
  {
    Serial.println("[BOOT][COREDUMP] valid coredump present; erasing stale dump");
    esp_err_t er = esp_core_dump_image_erase();
    Serial.printf("[BOOT][COREDUMP] erase result=%d\n", (int)er);
    return;
  }

  if (chk == ESP_ERR_NOT_FOUND)
    return;

  // Some builds/partition layouts report an invalid coredump area every boot.
  // Do not spam public logs unless support logging is enabled.
  if (supportLoggingEnabled())
  {
    Serial.printf("[BOOT][COREDUMP] corrupt/invalid coredump detected err=%d; erasing\n", (int)chk);
    esp_err_t er = esp_core_dump_image_erase();
    Serial.printf("[BOOT][COREDUMP] erase result=%d\n", (int)er);
  }
}

static void serialBootHandshake(uint32_t waitMs)
{
// CDC can take a moment; don't hang forever (battery / no host).
#if defined(ARDUINO_USB_CDC_ON_BOOT)
// Only available on newer cores; guard to avoid compile break
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  Serial.setTxTimeoutMs(10);
#endif
#endif

  const uint32_t t0 = millis();
  while (!Serial && (millis() - t0) < waitMs)
  {
    delay(10);
  }

  // One-shot banner only.
  clearStaleCoreDumpIfNeeded();
}

void appSetup()
{
  Serial.begin(115200);
  runtimeLogInit();
  bootTime = millis();
  supportLoggingBegin();

  // Give USB stack a moment, then do a bounded handshake.
  delay(50);
  serialBootHandshake(2500);

  if (supportLoggingEnabled())
  {
    Serial.printf("[PSRAM] size=%u free=%u\n", (unsigned)ESP.getPsramSize(), (unsigned)ESP.getFreePsram());

    Serial.printf("[HEAP] free=%u largest=%u\n", (unsigned)ESP.getFreeHeap(),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  }

// DO NOT use 0 here; on some ESP32 CDC builds this can cause "no output ever".
// Keep it small so we still don't block hard when host isn't ready.
#if defined(ARDUINO_USB_CDC_ON_BOOT)
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  Serial.setTxTimeoutMs(10);
#endif
#endif

  // Give the USB stack a moment to come up.
  delay(50);

  // Best-effort "wait for port open" (bounded, non-hanging).
  {
    const uint32_t t0 = millis();
    while (!Serial && (millis() - t0) < 800)
    {
      delay(10);
    }
  }

  auto bootPrintln = [&](const char *s)
  {
    if (Serial)
      Serial.println(s);
  };
  auto bootPrintf = [&](const char *fmt, auto... args)
  {
    if (Serial)
      Serial.printf(fmt, args...);
  };

  auto resetReasonStr = [&](esp_reset_reason_t r) -> const char *
  {
    switch (r)
    {
    case ESP_RST_POWERON:
      return "POWERON";
    case ESP_RST_EXT:
      return "EXT";
    case ESP_RST_SW:
      return "SW";
    case ESP_RST_PANIC:
      return "PANIC";
    case ESP_RST_INT_WDT:
      return "INT_WDT";
    case ESP_RST_TASK_WDT:
      return "TASK_WDT";
    case ESP_RST_WDT:
      return "WDT";
    case ESP_RST_BROWNOUT:
      return "BROWNOUT";
    case ESP_RST_DEEPSLEEP:
      return "DEEPSLEEP";
    default:
      return "OTHER";
    }
  };

  {
    const esp_reset_reason_t rr = esp_reset_reason();
    if (supportLoggingEnabled())
      bootPrintf("[BOOT] reset=%s (%d)\n", resetReasonStr(rr), (int)rr);
  }

  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);

  // Let tasks settle before anything else touches SPI / WiFi / SD.
  delay(50);

  initMotion();
  powerButtonInit();

  M5Cardputer.update();
  updateBattery();

  {
    const esp_reset_reason_t rr = esp_reset_reason();

    const bool brownoutReset = (rr == ESP_RST_BROWNOUT);
    const bool externalPowerAtBoot = displayUsbPowerLikely();
    const bool criticalAtBoot =
        (!externalPowerAtBoot && batteryVoltageMv > 0 && batteryVoltageMv < kBootBatteryEnterGateMv);

    if (brownoutReset || criticalAtBoot)
    {
      Serial.printf("[BAT][BOOT] entering charge gate mv=%d pct=%d usb=%d\n", batteryVoltageMv, batteryPercent,
                    (int)usbPowered);

      // Make sure screen is on
      SET_SCREEN_POWER(true);
      displayInit();

      // Start dim while gated on low battery.
      setBacklight(kBootChargeGateDimBacklight);

      uint32_t safeSince = 0;

      for (;;)
      {
        M5Cardputer.update();
        updateBattery();

        const bool usbNow = displayUsbPowerLikely();
        const uint8_t normalBacklight = (uint8_t)brightnessValues[brightnessLevel];
        const uint8_t desiredBacklight = usbNow ? normalBacklight : kBootChargeGateDimBacklight;

        setBacklight(desiredBacklight);

        const bool safeNow = (batteryVoltageMv >= kBootBatteryResumeMv);

        if (safeNow)
        {
          if (safeSince == 0)
            safeSince = millis();
        }
        else
        {
          safeSince = 0;
        }

        const bool ready = (safeSince != 0) && (millis() - safeSince >= 1500);

        // --- draw simple charging screen (buffered, no flicker) ---
        spr.fillScreen(TFT_BLACK);
        spr.setTextDatum(MC_DATUM);
        spr.setTextFont(2);
        spr.setTextSize(1);

        spr.setTextColor(TFT_RED, TFT_BLACK);
        spr.drawString("LOW BATTERY", SCREEN_W / 2, 32);

        spr.setTextColor(TFT_WHITE, TFT_BLACK);

        char buf[48];
        snprintf(buf, sizeof(buf), "%d%%", batteryPercent);
        spr.drawString(buf, SCREEN_W / 2, 56);

        // Battery bar
        const int barW = 140;
        const int barH = 14;
        const int barX = (SCREEN_W - barW) / 2;
        const int barY = 72;

        int pct = batteryPercent;
        if (pct < 0)
          pct = 0;
        if (pct > 100)
          pct = 100;

        spr.drawRect(barX, barY, barW, barH, TFT_WHITE);
        spr.drawRect(barX + barW, barY + 4, 3, barH - 8, TFT_WHITE);

        const int innerW = barW - 2;
        int fillW = (innerW * pct) / 100;
        if (fillW < 0)
          fillW = 0;
        if (fillW > innerW)
          fillW = innerW;

        uint16_t fillColor = TFT_RED;
        if (pct >= 60)
          fillColor = TFT_GREEN;
        else if (pct >= 25)
          fillColor = TFT_YELLOW;

        if (fillW > 0)
        {
          spr.fillRect(barX + 1, barY + 1, fillW, barH - 2, fillColor);
        }

        if (ready)
        {
          spr.setTextColor(TFT_GREEN, TFT_BLACK);
          spr.drawString("Starting...", SCREEN_W / 2, 104);
        }

        else if (usbNow)
        {
          spr.setTextColor(TFT_YELLOW, TFT_BLACK);
          spr.drawString("Charging...", SCREEN_W / 2, 104);
          spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
          spr.drawString("Waiting for safe voltage", SCREEN_W / 2, 124);
        }
        else
        {
          spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
          spr.drawString("Plug in USB", SCREEN_W / 2, 104);
          spr.drawString("to continue boot", SCREEN_W / 2, 124);
        }

        spr.pushSprite(0, 0);

        if (ready)
        {
          Serial.printf("[BAT][BOOT] charge gate cleared mv=%d -> continuing boot\n", batteryVoltageMv);

          // Restore brightness
          setBacklight((uint8_t)brightnessValues[brightnessLevel]);
          break;
        }

        delay(kBootBatteryPollMs);
      }
    }
  }

  inputForceClear();
  clearInputLatch();

  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.wakeup();

  displayInit();

  // Arm debug AFTER boot.
  g_debugArmMs = millis() + 2500;

  ui_setBootSplashActive(true);
  requestUIRedraw();

  inputInit();
  EEPROM.begin(512);

  initSound();

  applyBrightnessLevel(brightnessLevel);
  randomSeed((uint32_t)esp_random());

  g_app.uiState = UIState::BOOT;
  g_app.currentTab = Tab::TAB_PET;

  SET_SCREEN_POWER(true);
  requestUIRedraw();
  renderUI();

  // Delay SD/WiFi first attempts more when USB is present
  const uint32_t now = millis();
  const bool usbOpen = (bool)Serial;
  bootPipelineKick(now, usbOpen);

  assetOtaInit();

  {
    const AssetOtaConfig &cfg = assetOtaGetConfig();
    const AssetOtaChannel ch = (AssetOtaChannel)cfg.channel;
    const char *manifestUrl = assetOtaManifestUrlForChannel(ch);

    Serial.printf("[BUILD] flavor=%s fw=%s defaultOta=%s selectedOta=%s manifest=%s\n",
#if defined(PUBLIC_BUILD) && PUBLIC_BUILD
                  "PUBLIC",
#else
                  "DEV",
#endif
                  RH_VERSION_STRING,
#if defined(PUBLIC_BUILD) && PUBLIC_BUILD
                  "PUBLIC",
#else
                  "DEV",
#endif
                  (ch == AssetOtaChannel::DEV) ? "DEV" : "PUBLIC", manifestUrl ? manifestUrl : "(null)");
  }

  if (supportLoggingEnabled())
    bootPrintln("[BOOT] setup complete");
}
