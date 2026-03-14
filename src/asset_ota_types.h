#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <vector>

enum class AssetOtaChannel : uint8_t {
  PUBLIC = 0,
  DEV    = 1
};

enum class AssetOtaStatus : uint8_t {
  IDLE = 0,
  CHECKING,
  DOWNLOADING,
  INSTALLING,
  SUCCESS,
  FAILED
};

enum class AssetOtaError : uint8_t {
  NONE = 0,
  WIFI_DISABLED,
  WIFI_NOT_CONNECTED,
  SD_NOT_READY,
  CONFIG_IO,
  STATE_IO,
  HTTP_FAIL,
  JSON_FAIL,
  HASH_MISMATCH,
  SIZE_MISMATCH,
  RENAME_FAIL,
  BAD_PATH,
  NO_MANIFEST,
  STAGING_FAIL,
  WRITE_FAIL
};

struct AssetManifestFile
{
  char path[128];
  char sha256[65];
  uint32_t size;
};

struct AssetManifestData {
  String packVersion;
  String channel;
  std::vector<AssetManifestFile> files;

  void clear()
  {
    packVersion = "";
    channel = "";
    files.clear();
  }
};

struct AssetOtaConfig {
  uint32_t magic = 0x41544346UL; // ATCF
  uint16_t version = 1;
  uint8_t autoCheckEnabled = 0;
  uint8_t channel = (uint8_t)AssetOtaChannel::PUBLIC;
};

struct AssetOtaState {
  uint32_t magic = 0x41545354UL; // ATST
  uint16_t version = 1;
  uint8_t inProgress = 0;
  uint8_t status = (uint8_t)AssetOtaStatus::IDLE;
  uint16_t currentFileIndex = 0;
  uint16_t totalFileCount = 0;
  uint8_t lastError = (uint8_t)AssetOtaError::NONE;
  char targetPackVersion[24] = {0};
};