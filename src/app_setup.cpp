#include "app_setup.h"

// -----------------------------------------------------------------------------
// Raising Hell — Cardputer ADV Edition
// -----------------------------------------------------------------------------
#include <Arduino.h>
#include <stdint.h>
#include "M5Cardputer.h"
#include <WiFi.h>
#include <EEPROM.h>
#include <SD.h>
#include "esp_system.h"
#include "esp_log.h"
#include <esp_heap_caps.h>

#include "motion.h"
#include "ui_runtime.h"
#include "settings_state.h"
#include "display_state.h"
#include "eeprom_addrs.h"
#include "activity.h"
#include "sleep_state.h"
#include "time_state.h"
#include "display.h"
#include "input.h"
#include "pet.h"
#include "inventory.h"
#include "menu_actions.h"
#include "sound.h"
#include "sdcard.h"
#include "graphics.h"
#include "wifi_time.h"
#include "save_manager.h"
#include "time_persist.h"
#include "console.h"
#include "wifi_power.h"
#include "timezone.h"
#include "pet_defs.h"
#include "led_status.h"
#include "anim_engine.h"
#include "app_state.h"
#include "debug.h"
#include "brightness_state.h"
#include "auto_screen.h"
#include "input_activity_state.h"
#include "inventory_state.h"
#include "name_entry_state.h"
#include "controls_help_state.h"
#include "mini_games.h"
#include "boot_pipeline.h"
#include "system_status_state.h"
#include "power_button.h"
#include "hatching_flow.h"
#include "evolution_flow.h"
#include "asset_ota.h"
#include "runtime_log.h"

void updateBattery();

static void serialBootHandshake(uint32_t waitMs)
{
  // CDC can take a moment; don't hang forever (battery / no host).
  Serial.setTxTimeoutMs(10);

  const uint32_t t0 = millis();
  while (!Serial && (millis() - t0) < waitMs) {
    delay(10);
  }

  // If the port opens slightly later, spam a short banner so you still catch it.
  // (Harmless if no host; writes are dropped.)
  const uint32_t t1 = millis();
  while ((millis() - t1) < 300) {
    Serial.println("[BOOT] serial up");
    delay(30);
  }
}

void appSetup() {
  Serial.begin(115200);
  runtimeLogInit();

  // Give USB stack a moment, then do a bounded handshake.
  delay(50);
  serialBootHandshake(2500);
  
  Serial.printf("[PSRAM] size=%u free=%u\n",
    (unsigned)ESP.getPsramSize(),
    (unsigned)ESP.getFreePsram());

  Serial.printf("[HEAP] free=%u largest=%u\n",
    (unsigned)ESP.getFreeHeap(),
    (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

  // DO NOT use 0 here; on some ESP32 CDC builds this can cause "no output ever".
  // Keep it small so we still don't block hard when host isn't ready.
  Serial.setTxTimeoutMs(10);

  // Give the USB stack a moment to come up.
  delay(50);

  // Best-effort "wait for port open" (bounded, non-hanging).
  {
    const uint32_t t0 = millis();
    while (!Serial && (millis() - t0) < 800) {
      delay(10);
    }
  }

  auto bootPrintln = [&](const char* s) {
    if (Serial) Serial.println(s);
  };
  auto bootPrintf = [&](const char* fmt, auto... args) {
    if (Serial) Serial.printf(fmt, args...);
  };

  auto resetReasonStr = [&](esp_reset_reason_t r) -> const char* {
    switch (r) {
      case ESP_RST_POWERON:   return "POWERON";
      case ESP_RST_EXT:       return "EXT";
      case ESP_RST_SW:        return "SW";
      case ESP_RST_PANIC:     return "PANIC";
      case ESP_RST_INT_WDT:   return "INT_WDT";
      case ESP_RST_TASK_WDT:  return "TASK_WDT";
      case ESP_RST_WDT:       return "WDT";
      case ESP_RST_BROWNOUT:  return "BROWNOUT";
      case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
      default:                return "OTHER";
    }
  };

  {
    const esp_reset_reason_t rr = esp_reset_reason();
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

  g_app.uiState    = UIState::BOOT;
  g_app.currentTab = Tab::TAB_PET;

  SET_SCREEN_POWER(true);
  requestUIRedraw();
  renderUI();

  // Delay SD/WiFi first attempts more when USB is present
  const uint32_t now = millis();
  const bool usbOpen = (bool)Serial;
  bootPipelineKick(now, usbOpen);
  
  assetOtaInit();

  bootPrintln("[BOOT] setup complete");
}
