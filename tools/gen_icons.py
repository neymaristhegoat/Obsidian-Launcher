#!/usr/bin/env python3
"""Create PNG, ICO, and ICNS app icons from the launcher SVG on a host with librsvg."""

from __future__ import annotations

import argparse
import shutil
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def render(source: Path, size: int) -> bytes:
    converter = shutil.which("rsvg-convert")
    if converter is None:
        raise RuntimeError("rsvg-convert (librsvg) is required to regenerate app icons")
    return subprocess.run(
        [converter, "-w", str(size), "-h", str(size), str(source)],
        check=True,
        capture_output=True,
    ).stdout


def write_icons(source: Path, output_dir: Path) -> None:
    sizes = (16, 32, 48, 64, 128, 256, 512, 1024)
    images = {size: render(source, size) for size in sizes}
    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / "icon.png").write_bytes(images[512])

    ico_sizes = sizes[:-2]
    offset = 6 + 16 * len(ico_sizes)
    entries = []
    payload = bytearray()
    for size in ico_sizes:
        image = images[size]
        dimension = 0 if size == 256 else size
        entries.append(struct.pack("<BBBBHHII", dimension, dimension, 0, 0, 1, 32, len(image), offset))
        payload.extend(image)
        offset += len(image)
    (output_dir / "icon.ico").write_bytes(struct.pack("<HHH", 0, 1, len(ico_sizes)) + b"".join(entries) + payload)

    chunks = []
    for code, size in (("icp4", 16), ("icp5", 32), ("icp6", 64),
                       ("ic07", 128), ("ic08", 256), ("ic09", 512), ("ic10", 1024)):
        image = images[size]
        chunks.append(code.encode("ascii") + struct.pack(">I", len(image) + 8) + image)
    body = b"".join(chunks)
    (output_dir / "icon.icns").write_bytes(b"icns" + struct.pack(">I", len(body) + 8) + body)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT / "resources/logo.svg")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "resources")
    args = parser.parse_args()
    try:
        write_icons(args.source.resolve(), args.output_dir.resolve())
    except (OSError, subprocess.CalledProcessError, RuntimeError) as error:
        print(f"Icon generation failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
