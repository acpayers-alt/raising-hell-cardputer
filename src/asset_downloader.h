#pragma once

#include "asset_ota_types.h"

bool assetDownloadToStaging(const AssetManifestFile &file,
                            String *outStagingPath,
                            String *outErr);