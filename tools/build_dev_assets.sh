#!/usr/bin/env bash
set -euo pipefail

# Build + upload a dev asset pack.
#
# Usage:
#   ./tools/build_dev_assets.sh 1.1.5
#
# Optional:
#   REMOTE=r2:raising-hell-assets ./tools/build_dev_assets.sh 1.1.5
#
# Assumptions:
# - local assets live under: assets/dev/assets/raising_hell
# - manifest generator lives at: tools/gen_asset_manifest.py
# - remote layout is:
#     <REMOTE>/assets/<VERSION>/
#     <REMOTE>/manifest-dev.json

VERSION="${1:?usage: ./tools/build_dev_assets.sh <version>}"
REMOTE="${REMOTE:-r2:raising-hell-assets}"

DEV_ASSET_ROOT="assets/dev/assets/raising_hell"
DEV_ASSET_UPLOAD_ROOT="assets/dev/assets"
DEV_MANIFEST_LOCAL="assets/dev/manifest-dev.json"

REMOTE_PACK_PATH="${REMOTE}/assets/${VERSION}"
REMOTE_DEV_MANIFEST="${REMOTE}/manifest-dev.json"

echo "==> Building dev asset pack version: ${VERSION}"
echo "==> Remote: ${REMOTE}"
echo

if [[ ! -d "${DEV_ASSET_ROOT}" ]]; then
  echo "ERROR: dev asset root not found: ${DEV_ASSET_ROOT}" >&2
  exit 1
fi

if [[ ! -f "tools/gen_asset_manifest.py" ]]; then
  echo "ERROR: manifest generator not found: tools/gen_asset_manifest.py" >&2
  exit 1
fi

echo "==> Generating dev manifest..."
python3 tools/gen_asset_manifest.py \
  --version "${VERSION}" \
  --channel dev \
  --assets "${DEV_ASSET_ROOT}" \
  --output "${DEV_MANIFEST_LOCAL}"

echo "==> Previewing manifest header..."
python3 - <<'PY' "${DEV_MANIFEST_LOCAL}"
import json, sys
p = sys.argv[1]
with open(p, "r", encoding="utf-8") as f:
    data = json.load(f)
print("packVersion =", data.get("packVersion", data.get("pack_version")))
print("channel     =", data.get("channel"))
print("files       =", len(data.get("files", [])))
PY

echo
echo "==> Uploading versioned asset pack to ${REMOTE_PACK_PATH}/ ..."
rclone sync "${DEV_ASSET_UPLOAD_ROOT}" "${REMOTE_PACK_PATH}/" -P

echo "==> Uploading manifest-dev.json ..."
rclone copy "${DEV_MANIFEST_LOCAL}" "${REMOTE}/"

echo "==> Verifying remote pack exists..."
rclone ls "${REMOTE_PACK_PATH}" >/dev/null

echo "==> Done."
echo
echo "Dev manifest now points at version ${VERSION}:"
echo "  ${REMOTE_DEV_MANIFEST}"
