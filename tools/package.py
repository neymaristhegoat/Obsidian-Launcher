#!/usr/bin/env python3
"""Build a native Obsidian Launcher installer for the current operating system."""

from __future__ import annotations

import argparse
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
UI = ROOT / "ui"
SUPPORTED = {"windows", "macos", "linux"}


def host_platform() -> str:
    if sys.platform == "win32":
        return "windows"
    if sys.platform == "darwin":
        return "macos"
    if sys.platform.startswith("linux"):
        return "linux"
    raise RuntimeError(f"Unsupported build host: {sys.platform}")


def vcpkg_triplet(target: str, machine: str | None = None) -> str:
    machine = (machine or platform.machine()).lower()
    arch = "arm64" if machine in {"aarch64", "arm64"} else "x64"
    if target == "windows":
        return f"{arch}-windows-static"
    if target == "macos":
        return f"{arch}-osx"
    if target == "linux":
        return f"{arch}-linux"
    raise ValueError(f"Unsupported platform: {target}")


def electron_targets(target: str) -> list[str]:
    targets = {
        "windows": ["--win", "nsis", "portable"],
        "macos": ["--mac", "dmg", "zip"],
        "linux": ["--linux", "tar.gz"],
    }
    try:
        return targets[target]
    except KeyError as error:
        raise ValueError(f"Unsupported platform: {target}") from error


def core_binary(build_dir: Path, target: str) -> Path:
    return build_dir / "core" / ("launcher_core_cli.exe" if target == "windows" else "launcher_core_cli")


def run(command: list[str], *, cwd: Path = ROOT, env: dict[str, str] | None = None) -> None:
    print("+", subprocess.list2cmdline(command), flush=True)
    subprocess.run(command, cwd=cwd, env=env, check=True)


def ensure_vcpkg(env: dict[str, str]) -> None:
    vcpkg = ROOT / "third_party" / "vcpkg"
    if not (vcpkg / "scripts" / "buildsystems" / "vcpkg.cmake").is_file():
        raise RuntimeError("vcpkg submodule is missing; initialize repository submodules first")
    env.setdefault("VCPKG_ROOT", str(vcpkg))
    executable = vcpkg / ("vcpkg.exe" if os.name == "nt" else "vcpkg")
    if executable.is_file():
        return
    bootstrap = vcpkg / ("bootstrap-vcpkg.bat" if os.name == "nt" else "bootstrap-vcpkg.sh")
    if os.name == "nt":
        run(["cmd", "/c", str(bootstrap), "-disableMetrics"], env=env)
    else:
        run(["bash", str(bootstrap), "-disableMetrics"], env=env)


def build(target: str, *, install_dependencies: bool = True) -> Path:
    host = host_platform()
    if target != host:
        raise RuntimeError(
            f"Cannot build {target} from {host}. Run this script on each OS; "
            "the GitHub Actions release workflow does that automatically."
        )
    for tool in ("cmake", "pnpm"):
        if shutil.which(tool) is None:
            raise RuntimeError(f"Required build tool not found on PATH: {tool}")

    env = os.environ.copy()
    ensure_vcpkg(env)
    env["VCPKG_TARGET_TRIPLET"] = vcpkg_triplet(target)

    run(["cmake", "--preset", "default", "-DCMAKE_BUILD_TYPE=Release",
         f"-DVCPKG_TARGET_TRIPLET={env['VCPKG_TARGET_TRIPLET']}"], env=env)
    run(["cmake", "--build", "build", "--config", "Release", "--target", "launcher_core_cli"], env=env)
    binary = core_binary(ROOT / "build", target)
    if not binary.is_file():
        raise RuntimeError(f"Core executable was not produced: {binary}")
    from test import core_integration_smoke

    core_integration_smoke()

    if install_dependencies:
        run(["pnpm", "install", "--frozen-lockfile"], cwd=UI, env=env)
    run(["pnpm", "run", "build"], cwd=UI, env=env)

    artifact_dir = ROOT / "artifacts" / target
    artifact_dir.mkdir(parents=True, exist_ok=True)
    for pattern in ("*.AppImage", "*.pacman", "*.deb", "*.dmg", "*.exe", "*.zip", "*.tar.gz",
                    "install-linux.sh",
                    "SHA256SUMS-*.txt", "release-manifest-*.json"):
        for stale_file in artifact_dir.glob(pattern):
            stale_file.unlink()
    shutil.rmtree(artifact_dir / "download", ignore_errors=True)
    env["OBSIDIAN_ARTIFACT_DIR"] = os.path.relpath(artifact_dir, UI)
    run(["pnpm", "exec", "electron-builder", "--config", "electron-builder.config.cjs",
         "--publish", "never", *electron_targets(target)], cwd=UI, env=env)

    if target == "linux":
        shutil.copy2(ROOT / "tools" / "install-linux.sh", artifact_dir / "install-linux.sh")

    from release import stage_downloads, write_manifest

    write_manifest(artifact_dir, target)
    downloads = stage_downloads(artifact_dir)
    print(f"\nBuild artifacts: {downloads}")
    return downloads


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platform", choices=sorted(SUPPORTED), default=host_platform())
    parser.add_argument("--skip-install", action="store_true", help="Do not run pnpm install")
    args = parser.parse_args()
    try:
        build(args.platform, install_dependencies=not args.skip_install)
    except (OSError, subprocess.CalledProcessError, RuntimeError, ValueError) as error:
        print(f"Build failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
