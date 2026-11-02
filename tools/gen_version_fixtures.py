#!/usr/bin/env python3
"""Generate a small, deterministic Mojang manifest fixture for offline tests."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def sample_manifest() -> dict:
    versions = [
        {
            "id": "1.21.4",
            "type": "release",
            "url": "https://piston-meta.mojang.com/v1/packages/fixture/release.json",
            "time": "2024-12-03T09:41:15+00:00",
            "releaseTime": "2024-12-03T09:41:15+00:00",
            "sha1": "1" * 40,
            "complianceLevel": 1,
        },
        {
            "id": "24w14a",
            "type": "snapshot",
            "url": "https://piston-meta.mojang.com/v1/packages/fixture/snapshot.json",
            "time": "2024-04-03T12:00:00+00:00",
            "releaseTime": "2024-04-03T12:00:00+00:00",
            "sha1": "2" * 40,
            "complianceLevel": 1,
        },
    ]
    return {
        "latest": {"release": "1.21.4", "snapshot": "24w14a"},
        "versions": versions,
    }


def write_fixture(output: Path) -> Path:
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(sample_manifest(), indent=2) + "\n", encoding="utf-8")
    return output


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "output", nargs="?", type=Path,
        default=Path(__file__).resolve().parents[1] / "core/tests/fixtures/version-manifest.json",
    )
    args = parser.parse_args()
    output = write_fixture(args.output.resolve())
    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
