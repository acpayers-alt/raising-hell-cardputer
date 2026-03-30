#!/usr/bin/env bash
set -euo pipefail

# Promote a tested dev asset pack to public.
#
# Rule:
#   same bytes = same version
#
# This script assumes:
# - dev assets already exist locally at assets/dev/assets/raising_hell
# - the tested pack has already been uploaded to:
#     r2:raising-hell-assets/assets/<VERSION>/
# - you want public manifest to point at that exact same pack
#
# Usage:
#   ./promote_assets.sh 1.1.5
#
# Optional:
#   REMOTE=r2:raising-hell-assets ./promote_assets.sh 1.1.5

VERSION="${1:?usage: ./promote_assets.sh <version>}"
REMOTE="${REMOTE:-r2:raising-hell-assets}"

DEV_ASSET_ROOT="assets/dev/assets/raising_hell"
PUBLIC_MANIFEST_LOCAL="assets/public/manifest-public.json"
REMOTE_PUBLIC_MANIFEST="${REMOTE}/manifest-public.json"
REMOTE_PACK_PATH="${REMOTE}/assets/${VERSION}"

echo "==> Promoting tested asset pack version: ${VERSION}"
echo "==> Remote: ${REMOTE}"
echo "==> Expected remote pack: ${REMOTE_PACK_PATH}"
echo

if [[ ! -d "${DEV_ASSET_ROOT}" ]]; then
  echo "ERROR: local dev asset root not found: ${DEV_ASSET_ROOT}" >&2
  exit 1
fi

echo "==> Verifying remote pack exists..."
rclone ls "${REMOTE_PACK_PATH}" >/dev/null

echo "==> Generating public manifest from current tested asset tree..."
python3 tools/gen_asset_manifest.py \
  --version "${VERSION}" \
  --channel public \
  --assets "${DEV_ASSET_ROOT}" \
  --output "${PUBLIC_MANIFEST_LOCAL}"

echo "==> Previewing manifest header..."
python3 - <<'PY' "${PUBLIC_MANIFEST_LOCAL}"
import json, sys
p = sys.argv[1]
with open(p, "r", encoding="utf-8") as f:
    data = json.load(f)
print("packVersion =", data.get("packVersion", data.get("pack_version")))
print("channel     =", data.get("channel"))
print("files       =", len(data.get("files", [])))
PY

echo
echo "==> Uploading public manifest..."
rclone copy "${PUBLIC_MANIFEST_LOCAL}" "${REMOTE}/"

echo "==> Done."
echo
echo "Public manifest now points at asset pack version ${VERSION}."
echo "This is correct as long as public is meant to use the exact same bytes as dev."
