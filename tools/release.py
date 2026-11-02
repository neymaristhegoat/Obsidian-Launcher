#!/usr/bin/env python3
"""Create checksums and a small manifest for locally built installers."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DELIVERABLE_SUFFIXES = {
    "windows": {".exe", ".zip"},
    "macos": {".dmg", ".zip"},
    "linux": {"install-linux.sh"},
}


def is_deliverable(file: Path, target: str) -> bool:
    if target == "linux":
        return file.name == "install-linux.sh" or file.name.endswith(".tar.gz")
    return file.suffix.lower() in DELIVERABLE_SUFFIXES[target]


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def write_manifest(artifact_dir: Path, target: str) -> Path:
    package = json.loads((ROOT / "ui" / "package.json").read_text(encoding="utf-8"))
    files = sorted(
        file for file in artifact_dir.iterdir()
        if file.is_file() and is_deliverable(file, target)
    )
    if not files:
        raise RuntimeError(f"No installer files were created in {artifact_dir}")

    entries = [{"file": file.name, "size": file.stat().st_size, "sha256": sha256(file)} for file in files]
    (artifact_dir / f"SHA256SUMS-{target}.txt").write_text(
        "".join(f"{item['sha256']}  {item['file']}\n" for item in entries), encoding="utf-8"
    )
    manifest = {
        "product": "Obsidian Launcher",
        "version": package["version"],
        "platform": target,
        "artifacts": entries,
    }
    output = artifact_dir / f"release-manifest-{target}.json"
    output.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return output


def stage_downloads(artifact_dir: Path) -> Path:
    manifests = sorted(artifact_dir.glob("release-manifest-*.json"))
    if not manifests:
        raise RuntimeError(f"No release manifest found in {artifact_dir}")
    manifest_path = manifests[-1]
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    downloads = artifact_dir / "download"
    downloads.mkdir(exist_ok=True)
    names = [entry["file"] for entry in manifest["artifacts"]]
    names.extend([manifest_path.name, f"SHA256SUMS-{manifest['platform']}.txt"])
    for name in names:
        source = artifact_dir / name
        if not source.is_file():
            raise RuntimeError(f"Release file is missing: {source}")
        shutil.copy2(source, downloads / name)
    return downloads


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifact_dir", type=Path)
    parser.add_argument("--platform", choices=("windows", "macos", "linux"), required=True)
    args = parser.parse_args()
    write_manifest(args.artifact_dir.resolve(), args.platform)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())