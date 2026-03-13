#include "asset_provision_request.h"

#include "boot_flags_runtime.h"

void requestAssetProvisionOnNextBoot()
{
  setBootAssetProvisionRequested(true);
}

void clearAssetProvisionBootRequest()
{
  setBootAssetProvisionRequested(false);
}

bool assetProvisionBootRequested()
{
  return bootAssetProvisionRequested();
}