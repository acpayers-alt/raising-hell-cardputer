#include "sdcard.h" // if you have one; otherwise omit

#include <Arduino.h>

#include "savegame.h" // SavePayload, SaveHeader, RH_SAVE_MAGIC, RH_SAVE_VERSION

#include "debug.h"    // DBG_ON / DBGLN_ON macros (or your real debug macro header)
#include "graphics.h" // invalidateBackgroundCache (if that's where it is)
#include <SD.h>
#include <SPI.h>

bool g_sdReady = false;
bool sdReady() { return g_sdReady; }
static bool s_sdFirstInit = true;
static uint32_t s_sdBootStartMs = 0;

// Cardputer-Adv microSD pins (M5 docs):
// microSD: CS=G12 MOSI=G14 CLK=G40 MISO=G39
static constexpr int SD_SPI_SCK_PIN = 40;
static constexpr int SD_SPI_MISO_PIN = 39;
static constexpr int SD_SPI_MOSI_PIN = 14;
static constexpr int SD_SPI_CS_PIN = 12;

// Dedicated SPI so display stack doesn't interfere
static SPIClass sdSPI(FSPI);

// Paths
static const char *SAVE_DIR = "/raising_hell/save";
static const char *SAVE_MAIN = "/raising_hell/save/raising_hell.sav";
static const char *SAVE_TMP = "/raising_hell/save/raising_hell.tmp";

// ------------------------------------------------------------
// Small CRC32 (standard Ethernet polynomial)
// ------------------------------------------------------------
static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len)
{
  crc = ~crc;
  for (size_t i = 0; i < len; i++)
  {
    crc ^= data[i];
    for (int k = 0; k < 8; k++)
    {
      uint32_t mask = -(crc & 1u);
      crc = (crc >> 1) ^ (0xEDB88320u & mask);
    }
  }
  return ~crc;
}

static bool ensureDir(const char *path)
{
  if (SD.exists(path))
    return true;

  // SD.mkdir creates one level at a time
  if (!SD.exists("/raising_hell"))
    SD.mkdir("/raising_hell");
  if (!SD.exists("/raising_hell/save"))
    SD.mkdir("/raising_hell/save");

  return SD.exists(path);
}

// ------------------------------------------------------------
// initSD()
// ------------------------------------------------------------
bool initSD()
{
  // Keep all prints gated (serial-pressure safe)
  DBGLN_ON("[SD] initSD()");

  if (s_sdBootStartMs == 0)
    s_sdBootStartMs = millis();

  s_sdInitAttemptCount++;
  const uint32_t attemptNum = s_sdInitAttemptCount;
  const uint32_t t0 = millis();

  Serial.printf("[SD] init attempt %lu start (%lu ms since boot)\n", (unsigned long)attemptNum,
                (unsigned long)(millis() - s_sdBootStartMs));

  // Ensure CS is deasserted
  pinMode(SD_SPI_CS_PIN, OUTPUT);
  digitalWrite(SD_SPI_CS_PIN, HIGH);

  // Try multiple clocks: start fast, fall back to "always works" speeds.
  // 20MHz can be flaky on some boots/cards; 4MHz is a common safe baseline.
  static const uint32_t kHzList[] = {20000000UL, 8000000UL, 4000000UL, 1000000UL};

  // Make sure prior SD state is torn down (important across retries)
#if defined(ESP32)
  SD.end();
#endif

  // Fully restart the dedicated SPI bus
  sdSPI.end();
  delay(5);

  // Re-init bus pins
  sdSPI.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);
  delay(10);

  // A small CS pulse can help some cards wake cleanly
  digitalWrite(SD_SPI_CS_PIN, LOW);
  delay(2);
  digitalWrite(SD_SPI_CS_PIN, HIGH);

  // Give the card/socket a real settle window after bus re-init.
  // This is especially useful on the first boot after flashing.
  if (s_sdFirstInit)
  {
    delay(120); // give extra margin on first boot after flash/reset
    s_sdFirstInit = false;
  }
  else
  {
    delay(20); // normal settle for subsequent attempts
  }

  for (size_t i = 0; i < (sizeof(kHzList) / sizeof(kHzList[0])); i++)
  {
    const uint32_t hz = kHzList[i];

    // Tear down SD state between attempts
#if defined(ESP32)
    SD.end();
#endif
    delay(2);

    // Ensure CS high before begin
    digitalWrite(SD_SPI_CS_PIN, HIGH);
    delay(2);

    bool ok = SD.begin(SD_SPI_CS_PIN, sdSPI, hz);
    g_sdReady = ok;

    Serial.printf("[SD] SD.begin(%lu Hz) -> %d\n", (unsigned long)hz, (int)ok);

    if (!ok)
    {
      // brief settle before next attempt
      delay(20);
      continue;
    }

    const uint32_t callMs = millis() - t0;
    const uint32_t bootMs = (s_sdBootStartMs > 0) ? (millis() - s_sdBootStartMs) : 0;

    Serial.printf("[SD] init attempt %lu OK in %lu ms (call) / %lu ms since boot at %lu Hz cardType=%d sizeMB=%lu\n",
                  (unsigned long)attemptNum, (unsigned long)callMs, (unsigned long)bootMs, (unsigned long)hz,
                  (int)SD.cardType(), (unsigned long)(SD.cardSize() / (1024ULL * 1024ULL)));

    if (!ensureDir(SAVE_DIR))
    {
      Serial.printf("[SD] ERROR: could not create %s\n", SAVE_DIR);
      g_sdReady = false;
#if defined(ESP32)
      SD.end();
#endif
      return false;
    }

    // SD is ready. If UI rendered before SD came up, background caches may be
    // placeholder-filled. Force the next render to rebuild backgrounds.
    invalidateBackgroundCache();

    return true;
  }

  // All attempts failed
  g_sdReady = false;
  return false;
}

bool remountSD()
{
  Serial.println("[SD] remountSD()");

  g_sdReady = false;

#if defined(ESP32)
  SD.end();
#endif

  sdSPI.end();

  pinMode(SD_SPI_CS_PIN, OUTPUT);
  digitalWrite(SD_SPI_CS_PIN, HIGH);

  // Give the card/socket more time to settle after hot insert.
  delay(250);

  const bool ok = initSD();
  g_sdReady = ok;

  Serial.printf("[SD] remountSD result=%d\n", ok ? 1 : 0);
  return ok;
}

bool remountSDWithRetry(uint8_t attempts)
{
  if (attempts == 0)
    attempts = 1;

  for (uint8_t i = 0; i < attempts; ++i)
  {
    Serial.printf("[SD] remount attempt %u/%u\n", (unsigned)(i + 1), (unsigned)attempts);

    if (remountSD())
      return true;

    delay(300);
  }

  return false;
}

// ------------------------------------------------------------
// sdWriteSave()
// ------------------------------------------------------------
bool sdWriteSave(const SavePayload &payload)
{
  if (!g_sdReady)
    return false;

  if (!ensureDir(SAVE_DIR))
  {
    DBGLN_ON("[SD SAVE] missing save dir");
    return false;
  }

  // Build header
  SaveHeader hdr{};
  hdr.magic = RH_SAVE_MAGIC;
  hdr.version = RH_SAVE_VERSION;
  hdr.headerSize = (uint16_t)sizeof(SaveHeader);
  hdr.payloadSize = (uint32_t)sizeof(SavePayload);
  hdr.crc32 = crc32_update(0, (const uint8_t *)&payload, sizeof(SavePayload));

  // Write temp file
  File f = SD.open(SAVE_TMP, FILE_WRITE);
  if (!f)
  {
    DBG_ON("[SD SAVE] open tmp FAILED: %s\n", SAVE_TMP);
    return false;
  }

  size_t w1 = f.write((const uint8_t *)&hdr, sizeof(hdr));
  size_t w2 = f.write((const uint8_t *)&payload, sizeof(payload));
  f.flush();
  f.close();

  if (w1 != sizeof(hdr) || w2 != sizeof(payload))
  {
    DBG_ON("[SD SAVE] write FAILED w1=%u/%u w2=%u/%u\n", (unsigned)w1, (unsigned)sizeof(hdr), (unsigned)w2,
           (unsigned)sizeof(payload));
    SD.remove(SAVE_TMP);
    return false;
  }

  // Replace main
  SD.remove(SAVE_MAIN);
  if (!SD.rename(SAVE_TMP, SAVE_MAIN))
  {
    DBGLN_ON("[SD SAVE] rename FAILED tmp->main");
    SD.remove(SAVE_TMP);
    return false;
  }

  return true;
}

// ------------------------------------------------------------
// sdLoadSave()
// ------------------------------------------------------------
bool sdLoadSave(SavePayload &out)
{
  if (!g_sdReady)
    return false;

  if (!SD.exists(SAVE_MAIN))
  {
    DBG_ON("[SD LOAD] missing: %s\n", SAVE_MAIN);
    return false;
  }

  File f = SD.open(SAVE_MAIN, FILE_READ);
  if (!f)
  {
    DBGLN_ON("[SD LOAD] open FAILED");
    return false;
  }

  SaveHeader hdr{};
  if (f.read((uint8_t *)&hdr, sizeof(hdr)) != (int)sizeof(hdr))
  {
    DBGLN_ON("[SD LOAD] header read FAILED");
    f.close();
    return false;
  }

  // Validate header
  if (hdr.magic != RH_SAVE_MAGIC)
  {
    DBG_ON("[SD LOAD] BAD MAGIC: 0x%08lX\n", (unsigned long)hdr.magic);
    f.close();
    return false;
  }
  if (hdr.version != RH_SAVE_VERSION)
  {
    DBG_ON("[SD LOAD] BAD VERSION: %u\n", (unsigned)hdr.version);
    f.close();
    return false;
  }
  if (hdr.headerSize != sizeof(SaveHeader))
  {
    DBG_ON("[SD LOAD] BAD headerSize: %u expected %u\n", (unsigned)hdr.headerSize, (unsigned)sizeof(SaveHeader));
    f.close();
    return false;
  }
  if (hdr.payloadSize != sizeof(SavePayload))
  {
    DBG_ON("[SD LOAD] BAD payloadSize: %lu expected %lu\n", (unsigned long)hdr.payloadSize,
           (unsigned long)sizeof(SavePayload));
    f.close();
    return false;
  }

  // Read payload
  if (f.read((uint8_t *)&out, sizeof(out)) != (int)sizeof(out))
  {
    DBGLN_ON("[SD LOAD] payload read FAILED");
    f.close();
    return false;
  }
  f.close();

  // Validate payload's embedded magic/version too
  if (out.magic != SAVE_MAGIC || out.version != SAVE_VERSION)
  {
    DBG_ON("[SD LOAD] BAD payload magic/version: magic=0x%08lX ver=%u\n", (unsigned long)out.magic,
           (unsigned)out.version);
    return false;
  }

  // Validate CRC
  uint32_t crc = crc32_update(0, (const uint8_t *)&out, sizeof(out));
  if (crc != hdr.crc32)
  {
    DBG_ON("[SD LOAD] BAD CRC: got 0x%08lX expected 0x%08lX\n", (unsigned long)crc, (unsigned long)hdr.crc32);
    return false;
  }

  DBGLN_ON("[SD LOAD] OK");
  return true;
}

// ------------------------------------------------------------
// listDir() (debug helper)
// ------------------------------------------------------------
void listDir(const char *path)
{
  if (!g_sdReady)
  {
    DBGLN_ON("[SD] listDir skipped (SD not ready)");
    return;
  }

  File d = SD.open(path);
  if (!d || !d.isDirectory())
  {
    DBG_ON("[SD] Can't open dir %s\n", path);
    return;
  }

  DBG_ON("[SD] contents of %s\n", path);

  while (true)
  {
    File f = d.openNextFile();
    if (!f)
      break;

    DBG_ON("  %s (%lu bytes)\n", f.name(), (unsigned long)f.size());
    f.close();
  }

  d.close();
}
