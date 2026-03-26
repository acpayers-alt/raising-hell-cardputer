#pragma once

#include <FS.h>
#include <SD.h>
#include "asset_ota_types.h"

void assetOtaInit();
void assetOtaTick();

bool assetOtaCheckNow(String *outMessage = nullptr);

const AssetOtaConfig &assetOtaGetConfig();
bool assetOtaSetAutoCheckEnabled(bool en);
bool assetOtaSetChannel(AssetOtaChannel ch);

const char *assetOtaInstalledVersion();
AssetOtaStatus assetOtaStatus();
AssetOtaError assetOtaLastError();
const char *assetOtaStatusString();
const char *assetOtaLastErrorString();

uint16_t assetOtaCurrentFileIndex();
uint16_t assetOtaTotalFileCount();

bool assetOtaDidReleaseGraphics();

bool assetOtaConfirmActive();
void assetOtaSetConfirmActive(bool v);

bool assetOtaRunInWorkerTask(String *outMessage);

bool assetOtaWorklistOpenRead(File *outFile);
bool assetOtaWorklistReadNext(File &inFile, AssetManifestFile *outFile);
bool assetOtaWorklistAppend(const AssetManifestFile &f);
bool assetOtaWorklistClear();

const char *assetOtaWorklistPath();