// -----------------------------------------------------------------------------
// asset_ota.h
// Asset OTA interface
// -----------------------------------------------------------------------------
#pragma once

#include <FS.h>
#include <SD.h>

#include "asset_ota_types.h"

struct AssetOtaProgress
{
  const char *stage;
  uint16_t current;
  uint16_t total;

  uint32_t bytesCurrent;   // current file downloaded bytes
  uint32_t bytesTotal;     // current file total size
};

const AssetOtaProgress &assetOtaGetProgress();

// -----------------------------------------------------------------------------
// Lifecycle
// -----------------------------------------------------------------------------
void assetOtaInit();
void assetOtaTick();
void assetOtaResetState();

// -----------------------------------------------------------------------------
// Execution / checks
// -----------------------------------------------------------------------------
bool assetOtaCheckNow(String *outMessage = nullptr);
bool assetOtaRunInWorkerTask(String *outMessage);

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------
const AssetOtaConfig &assetOtaGetConfig();
bool assetOtaSetAutoCheckEnabled(bool en);
bool assetOtaSetChannel(AssetOtaChannel ch);

// -----------------------------------------------------------------------------
// Status / progress
// -----------------------------------------------------------------------------
const char *assetOtaInstalledVersion();

AssetOtaStatus assetOtaStatus();
AssetOtaError assetOtaLastError();

const char *assetOtaStatusString();
const char *assetOtaLastErrorString();

uint16_t assetOtaCurrentFileIndex();
uint16_t assetOtaTotalFileCount();

bool assetOtaDidReleaseGraphics();
bool assetOtaDidInstallFiles();

// -----------------------------------------------------------------------------
// Confirmation / UI flow
// -----------------------------------------------------------------------------
bool assetOtaConfirmActive();
void assetOtaSetConfirmActive(bool v);

// -----------------------------------------------------------------------------
// Worklist helpers
// -----------------------------------------------------------------------------
bool assetOtaWorklistOpenRead(File *outFile);
bool assetOtaWorklistReadNext(File &inFile, AssetManifestFile *outFile);
bool assetOtaWorklistClear();

const char *assetOtaWorklistPath();