#include "photo_capture.h"

#include <Arduino.h>
#include <SD.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "display.h"
#include "pet.h"
#include "save_manager.h"
#include "time_persist.h"

extern Pet pet;
extern bool g_sdReady;

static uint32_t s_crcTable[256];
static bool s_crcTableReady = false;

static void photoInitCrcTable()
{
  if (s_crcTableReady)
    return;

  for (uint32_t i = 0; i < 256; ++i)
  {
    uint32_t c = i;
    for (int k = 0; k < 8; ++k)
      c = (c & 1) ? (0xEDB88320UL ^ (c >> 1)) : (c >> 1);
    s_crcTable[i] = c;
  }

  s_crcTableReady = true;
}

static void photoCrcUpdate(uint32_t &crc, const uint8_t *data, size_t len)
{
  photoInitCrcTable();

  for (size_t i = 0; i < len; ++i)
    crc = s_crcTable[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
}

static bool photoWriteAll(File &f, const uint8_t *data, size_t len) { return f.write(data, len) == len; }

static bool photoWriteU32BE(File &f, uint32_t v)
{
  uint8_t b[4] = {
      (uint8_t)((v >> 24) & 0xFF),
      (uint8_t)((v >> 16) & 0xFF),
      (uint8_t)((v >> 8) & 0xFF),
      (uint8_t)(v & 0xFF),
  };

  return photoWriteAll(f, b, sizeof(b));
}

static bool photoWriteChunk(File &f, const char type[4], const uint8_t *data, uint32_t len)
{
  if (!photoWriteU32BE(f, len))
    return false;

  uint32_t crc = 0xFFFFFFFFUL;

  const uint8_t *typeBytes = (const uint8_t *)type;
  photoCrcUpdate(crc, typeBytes, 4);

  if (!photoWriteAll(f, typeBytes, 4))
    return false;

  if (len > 0 && data)
  {
    photoCrcUpdate(crc, data, len);
    if (!photoWriteAll(f, data, len))
      return false;
  }

  crc ^= 0xFFFFFFFFUL;
  return photoWriteU32BE(f, crc);
}

static void photoAdlerUpdate(uint32_t &a, uint32_t &b, const uint8_t *data, size_t len)
{
  static constexpr uint32_t MOD_ADLER = 65521UL;

  for (size_t i = 0; i < len; ++i)
  {
    a += data[i];
    if (a >= MOD_ADLER)
      a -= MOD_ADLER;

    b += a;
    if (b >= MOD_ADLER)
      b %= MOD_ADLER;
  }
}

static bool photoWriteIdatByte(File &f, uint32_t &crc, uint8_t v)
{
  photoCrcUpdate(crc, &v, 1);
  return f.write(&v, 1) == 1;
}

static bool photoWriteIdatData(File &f, uint32_t &crc, const uint8_t *data, size_t len)
{
  photoCrcUpdate(crc, data, len);
  return photoWriteAll(f, data, len);
}

static bool photoWritePngFromCanvas(const char *path)
{
  File f = SD.open(path, FILE_WRITE);
  if (!f)
  {
    Serial.printf("[PHOTO] failed: open path=%s\n", path);
    return false;
  }

  const uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  if (!photoWriteAll(f, signature, sizeof(signature)))
  {
    f.close();
    return false;
  }

  uint8_t ihdr[13] = {};
  ihdr[0] = (uint8_t)((SCREEN_W >> 24) & 0xFF);
  ihdr[1] = (uint8_t)((SCREEN_W >> 16) & 0xFF);
  ihdr[2] = (uint8_t)((SCREEN_W >> 8) & 0xFF);
  ihdr[3] = (uint8_t)(SCREEN_W & 0xFF);
  ihdr[4] = (uint8_t)((SCREEN_H >> 24) & 0xFF);
  ihdr[5] = (uint8_t)((SCREEN_H >> 16) & 0xFF);
  ihdr[6] = (uint8_t)((SCREEN_H >> 8) & 0xFF);
  ihdr[7] = (uint8_t)(SCREEN_H & 0xFF);
  ihdr[8] = 8;  // bit depth
  ihdr[9] = 2;  // color type: truecolor RGB
  ihdr[10] = 0; // compression
  ihdr[11] = 0; // filter
  ihdr[12] = 0; // interlace

  if (!photoWriteChunk(f, "IHDR", ihdr, sizeof(ihdr)))
  {
    f.close();
    return false;
  }

  static constexpr uint16_t kRowBytes = 1 + (SCREEN_W * 3);
  uint8_t row[kRowBytes];

  // One uncompressed DEFLATE block per PNG scanline.
  // This is larger than compressed PNG, but very simple, reliable, and streamable.
  const uint32_t idatLen = 2 + (SCREEN_H * (5 + kRowBytes)) + 4;

  if (!photoWriteU32BE(f, idatLen))
  {
    f.close();
    return false;
  }

  uint32_t crc = 0xFFFFFFFFUL;
  const uint8_t typeBytes[4] = {'I', 'D', 'A', 'T'};
  photoCrcUpdate(crc, typeBytes, 4);

  if (!photoWriteAll(f, typeBytes, 4))
  {
    f.close();
    return false;
  }

  // zlib header: deflate, fastest/no compression.
  const uint8_t zlibHeader[2] = {0x78, 0x01};
  if (!photoWriteIdatData(f, crc, zlibHeader, sizeof(zlibHeader)))
  {
    f.close();
    return false;
  }

  uint32_t adlerA = 1;
  uint32_t adlerB = 0;

  for (int y = 0; y < SCREEN_H; ++y)
  {
    row[0] = 0; // PNG filter type 0: None

    for (int x = 0; x < SCREEN_W; ++x)
    {
      const uint16_t px = (uint16_t)spr.readPixel(x, y);

      const uint8_t r = (uint8_t)(((px >> 11) & 0x1F) * 255 / 31);
      const uint8_t g = (uint8_t)(((px >> 5) & 0x3F) * 255 / 63);
      const uint8_t b = (uint8_t)((px & 0x1F) * 255 / 31);

      const int o = 1 + (x * 3);
      row[o + 0] = r;
      row[o + 1] = g;
      row[o + 2] = b;
    }

    photoAdlerUpdate(adlerA, adlerB, row, kRowBytes);

    const uint16_t len = kRowBytes;
    const uint16_t nlen = ~len;
    const bool finalBlock = (y == SCREEN_H - 1);

    const uint8_t blockHeader[5] = {
        (uint8_t)(finalBlock ? 0x01 : 0x00), (uint8_t)(len & 0xFF),
        (uint8_t)((len >> 8) & 0xFF),        (uint8_t)(nlen & 0xFF),
        (uint8_t)((nlen >> 8) & 0xFF),
    };

    if (!photoWriteIdatData(f, crc, blockHeader, sizeof(blockHeader)))
    {
      f.close();
      return false;
    }

    if (!photoWriteIdatData(f, crc, row, kRowBytes))
    {
      f.close();
      return false;
    }
  }

  const uint32_t adler = (adlerB << 16) | adlerA;
  uint8_t adlerBytes[4] = {
      (uint8_t)((adler >> 24) & 0xFF),
      (uint8_t)((adler >> 16) & 0xFF),
      (uint8_t)((adler >> 8) & 0xFF),
      (uint8_t)(adler & 0xFF),
  };

  if (!photoWriteIdatData(f, crc, adlerBytes, sizeof(adlerBytes)))
  {
    f.close();
    return false;
  }

  crc ^= 0xFFFFFFFFUL;

  if (!photoWriteU32BE(f, crc))
  {
    f.close();
    return false;
  }

  if (!photoWriteChunk(f, "IEND", nullptr, 0))
  {
    f.close();
    return false;
  }

  f.close();
  return true;
}

static void photoSanitizeName(const char *in, char *out, size_t outSize)
{
  if (!out || outSize == 0)
    return;

  const char *src = (in && in[0]) ? in : "Bub";

  size_t w = 0;
  for (size_t r = 0; src[r] && w + 1 < outSize; ++r)
  {
    const char c = src[r];

    if (isalnum((unsigned char)c))
      out[w++] = c;
    else if (c == '-' || c == '_')
      out[w++] = c;
    else if (c == ' ')
      out[w++] = '_';
  }

  if (w == 0)
  {
    strncpy(out, "Bub", outSize - 1);
    out[outSize - 1] = '\0';
    return;
  }

  out[w] = '\0';
}

static bool photoEnsureDir()
{
  if (!g_sdReady)
    return false;

  if (!SD.exists("/raising_hell"))
  {
    if (!SD.mkdir("/raising_hell"))
      return false;
  }

  if (!SD.exists("/raising_hell/photos"))
  {
    if (!SD.mkdir("/raising_hell/photos"))
      return false;
  }

  return true;
}

static void photoBuildPath(char *out, size_t outSize)
{
  char safeName[PET_NAME_MAX + 1];
  photoSanitizeName(pet.name, safeName, sizeof(safeName));

  time_t now = time(nullptr);
  tm tmNow = {};
  localtime_r(&now, &tmNow);

  if (timeIsValid())
  {
    snprintf(out, outSize, "/raising_hell/photos/%s_%04d-%02d-%02d_%02d%02d%02d.png", safeName, tmNow.tm_year + 1900,
             tmNow.tm_mon + 1, tmNow.tm_mday, tmNow.tm_hour, tmNow.tm_min, tmNow.tm_sec);
  }
  else
  {
    snprintf(out, outSize, "/raising_hell/photos/%s_%lu.png", safeName, (unsigned long)millis());
  }
}

bool photoCaptureCurrentScreen()
{
  if (!photoEnsureDir())
    return false;

  char path[96];
  photoBuildPath(path, sizeof(path));

  if (SD.exists(path))
  {
    char safeName[PET_NAME_MAX + 1];
    photoSanitizeName(pet.name, safeName, sizeof(safeName));

    snprintf(path, sizeof(path), "/raising_hell/photos/%s_%lu.png", safeName, (unsigned long)millis());
  }

  Serial.printf("[PHOTO] saving %s\n", path);

  const bool ok = photoWritePngFromCanvas(path);

  Serial.printf("[PHOTO] save %s path=%s\n", ok ? "ok" : "failed", path);
  return ok;
}