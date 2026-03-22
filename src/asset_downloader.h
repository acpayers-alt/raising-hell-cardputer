#pragma once

#include "asset_ota_types.h"

bool assetDownloadToStaging(const String &fileUrl,
    const AssetManifestFile &file,
    String *stagingPath,
    String *err);