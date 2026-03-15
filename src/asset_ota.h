#pragma once

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

