#pragma once

#include "asset_ota_types.h"

bool assetOtaEnsureCoreDirs();
bool assetOtaEnsureParentDir(const char *fullPath);

const char *assetOtaConfigPath();
const char *assetOtaConfigTmpPath();
const char *assetOtaStatePath();
const char *assetOtaStateTmpPath();
const char *assetOtaLocalManifestPath();
const char *assetOtaLocalManifestTmpPath();
const char *assetOtaStagingRoot();

void assetOtaConfigDefaults(AssetOtaConfig &cfg);
void assetOtaStateDefaults(AssetOtaState &st);

bool assetOtaConfigLoad(AssetOtaConfig *outCfg);
bool assetOtaConfigSave(const AssetOtaConfig &cfg);

bool assetOtaStateLoad(AssetOtaState *outState);
bool assetOtaStateSave(const AssetOtaState &st);

const char *assetOtaManifestUrlForChannel(AssetOtaChannel ch);
const char *assetOtaFallbackManifestUrlForChannel(AssetOtaChannel ch);