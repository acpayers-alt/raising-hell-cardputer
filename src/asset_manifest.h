#pragma once

#include "asset_ota_types.h"

bool assetManifestNormalizePath(const String &inPath, String *outRelPath);

bool assetManifestLoadLocal(AssetManifestData *out);
bool assetManifestSaveLocal(const AssetManifestData &manifest);
bool assetManifestDownloadRemote(const char *url, AssetManifestData *out);

void assetManifestBuildDiff(const AssetManifestData &localManifest,
                            const AssetManifestData &remoteManifest,
                            std::vector<AssetManifestFile> &outChangedFiles);