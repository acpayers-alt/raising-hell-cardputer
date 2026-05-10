#include "boot_firmware_marker.h"

#include "support_logging_state.h"
#include <Arduino.h>
#include <Preferences.h>

#include "asset_provision_request.h"
#include "version.h"

namespace
{
static constexpr const char *kBootPrefsNs = "rh_boot";
static constexpr const char *kLastSeenBuildKey = "fw_id";
} // namespace

const char *bootCurrentBuildId() { return RH_BUILD_ID_STRING; }

bool bootFirmwareMarkerRead(String &outBuildId)
{
  outBuildId = "";

  Preferences prefs;

  // On a fresh install/upgrade path, the namespace may not exist yet.
  // Treat that as "no firmware seen" instead of skipping the upgrade check.
  if (!prefs.begin(kBootPrefsNs, true))
    return true;

  outBuildId = prefs.getString(kLastSeenBuildKey, "");
  prefs.end();
  return true;
}

bool bootFirmwareMarkerWrite(const char *buildId)
{
  Preferences prefs;
  if (!prefs.begin(kBootPrefsNs, false))
    return false;

  prefs.putString(kLastSeenBuildKey, buildId ? buildId : "");
  prefs.end();
  return true;
}

bool bootFirmwareMarkerClear()
{
  Preferences prefs;
  if (!prefs.begin(kBootPrefsNs, false))
    return false;

  prefs.remove(kLastSeenBuildKey);
  prefs.end();
  return true;
}

bool bootMarkFirmwareSeenAndRequestProvisionIfChanged()
{
  String lastSeenBuildId;
  if (!bootFirmwareMarkerRead(lastSeenBuildId))
  {
    if (supportLoggingEnabled())
      Serial.println("[BOOT][FW] prefs read failed; skipping firmware-change check");
    return false;
  }

  const String currentBuildId = RH_BUILD_ID_STRING;
  const bool changed = (lastSeenBuildId != currentBuildId);

  if (changed)
  {
    Serial.printf("[BOOT][FW] build changed old='%s' new='%s'\n",
                  lastSeenBuildId.length() ? lastSeenBuildId.c_str() : "(none)", currentBuildId.c_str());

    if (!bootFirmwareMarkerWrite(currentBuildId.c_str()))
    {
      Serial.println("[BOOT][FW] failed to store current build id; skipping optional asset provision request");
      return false;
    }

    requestAssetProvisionOnNextBoot();
  }
  else
  {
    if (supportLoggingEnabled())
      Serial.printf("[BOOT][FW] build unchanged id='%s'\n", currentBuildId.c_str());
  }

  return changed;
}