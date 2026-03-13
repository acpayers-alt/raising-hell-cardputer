#include "boot_flags_runtime.h"

#include <EEPROM.h>

// Pick a byte that is not already used by your project.
// If you already have a central EEPROM layout, move this there.
static constexpr int EEPROM_ADDR_ASSET_PROVISION_REQ = 96;
static constexpr uint8_t BOOTFLAG_CLEAR = 0;
static constexpr uint8_t BOOTFLAG_SET = 1;

static bool s_bootFlagsInit = false;

static void ensureBootFlagsInit()
{
  if (s_bootFlagsInit)
    return;

  EEPROM.begin(512);
  s_bootFlagsInit = true;
}

bool bootAssetProvisionRequested()
{
  ensureBootFlagsInit();
  return EEPROM.read(EEPROM_ADDR_ASSET_PROVISION_REQ) == BOOTFLAG_SET;
}

void setBootAssetProvisionRequested(bool v)
{
  ensureBootFlagsInit();
  EEPROM.write(EEPROM_ADDR_ASSET_PROVISION_REQ, v ? BOOTFLAG_SET : BOOTFLAG_CLEAR);
  EEPROM.commit();
}