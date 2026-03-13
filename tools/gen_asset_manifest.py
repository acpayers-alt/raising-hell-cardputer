#!/usr/bin/env python3

import argparse
import hashlib
import json
from pathlib import Path
from datetime import datetime, timezone


def sha256_file(path: Path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def repo_root():
    """
    Determine repo root from script location.
    tools/gen_asset_manifest.py -> repo root
    """
    return Path(__file__).resolve().parent.parent


def build_manifest(asset_root: Path, version: str, channel: str):
    files = []

    for f in sorted(asset_root.rglob("*")):
        if not f.is_file():
            continue

        rel = f.relative_to(asset_root.parent).as_posix()

        files.append(
            {
                "path": rel,
                "size": f.stat().st_size,
            }
        )

    manifest = {
        "packVersion": version,
        "pack_version": version,  # backward compatibility
        "channel": channel,
        "generatedAt": datetime.now(timezone.utc).isoformat(),
        "fileCount": len(files),
        "files": files,
    }

    return manifest


def main():

    root = repo_root()

    parser = argparse.ArgumentParser(description="Generate Raising Hell asset manifest")

    parser.add_argument(
        "--version",
        required=True,
        help="Asset pack version (example: 0.0.2)",
    )

    parser.add_argument(
        "--channel",
        default="public",
        help="Manifest channel (default: public)",
    )

    parser.add_argument(
        "--assets",
        default=root / "assets/public/assets/raising_hell",
        help="Asset root directory",
    )

    parser.add_argument(
        "--output",
        default=root / "assets/public/manifest-public.json",
        help="Output manifest path",
    )

    args = parser.parse_args()

    asset_root = Path(args.assets).resolve()
    output = Path(args.output).resolve()

    if not asset_root.exists():
        raise SystemExit(f"ERROR: Asset root not found: {asset_root}")

    print("Repo root:", root)
    print("Asset root:", asset_root)

    manifest = build_manifest(asset_root, args.version, args.channel)

    output.parent.mkdir(parents=True, exist_ok=True)

    with output.open("w") as f:
        json.dump(manifest, f, indent=2)

    print("\nManifest written to:", output)
    print("Files:", manifest["fileCount"])
    print("Version:", args.version)


if __name__ == "__main__":
    main()
