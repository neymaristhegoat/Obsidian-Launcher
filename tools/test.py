#!/usr/bin/env python3
"""Run Python tooling checks, native core tests, and the frontend production build."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from package import ensure_vcpkg  # noqa: E402


def run(command: list[str], *, cwd: Path = ROOT) -> None:
    print("+", subprocess.list2cmdline(command), flush=True)
    env = os.environ.copy()
    env.setdefault("VCPKG_ROOT", str(ROOT / "third_party" / "vcpkg"))
    subprocess.run(command, cwd=cwd, env=env, check=True)


def core_integration_smoke() -> None:
    executable = ROOT / "build" / "core" / ("launcher_core_cli.exe" if sys.platform == "win32" else "launcher_core_cli")
    if not executable.is_file():
        raise RuntimeError(f"Core executable is missing: {executable}")

    profile = {
        "id": "tools-test-001",
        "name": "Integration Test",
        "version": "1.21.4",
        "createdAt": 1733220000000,
        "loader": "vanilla",
        "memory": 4,
        "javaPath": "",
    }
    requests = [
        {"jsonrpc": "2.0", "id": 1, "method": "core.ping"},
        {"jsonrpc": "2.0", "id": 2, "method": "config.read"},
        {"jsonrpc": "2.0", "id": 3, "method": "config.write", "params": {"settings": {"theme": "dark", "memory": 6, "javaPath": ""}}},
        {"jsonrpc": "2.0", "id": 4, "method": "config.read"},
        {"jsonrpc": "2.0", "id": 5, "method": "instances.save", "params": {"profile": profile}},
        {"jsonrpc": "2.0", "id": 6, "method": "instances.list"},
    ]
    with tempfile.TemporaryDirectory(prefix="obsidian-launcher-test-") as temporary:
        root = Path(temporary)
        old_mods = root / "instances" / profile["id"] / "mods"
        old_mods.mkdir(parents=True)
        (old_mods / "legacy-mod.jar").write_bytes(b"legacy")
        process = subprocess.run(
            [str(executable), "--data-dir", str(root)],
            input="".join(json.dumps(request) + "\n" for request in requests),
            text=True,
            capture_output=True,
            check=True,
        )
        responses = {item["id"]: item for item in map(json.loads, process.stdout.splitlines())}
        if responses[1]["result"] != {"pong": True}:
            raise RuntimeError("Core RPC ping failed")
        if responses[2]["result"].get("settings") is not None:
            raise RuntimeError("New config did not start with the migration marker")
        if responses[3]["result"]["settings"] != {"theme": "dark", "memory": 6, "javaPath": ""}:
            raise RuntimeError("Launcher settings did not round-trip through config.json")
        if responses[5]["result"] != profile or responses[6]["result"] != [profile]:
            raise RuntimeError("Instance profile did not round-trip through instance.json")

        profile_root = root / "instances" / "integration-test-tools-te"
        expected_dirs = ["java", "meta", "libraries", "assets/indexes", "assets/objects",
                         "versions", "natives", "logs", "instances"]
        for folder in expected_dirs:
            if not (root / folder).is_dir():
                raise RuntimeError(f"Data directory was not created: {folder}")
        if not (root / "config.json").is_file() or not (profile_root / "instance.json").is_file():
            raise RuntimeError("Expected launcher or instance configuration file is missing")
        for folder in ("mods", "saves", "resourcepacks", "shaderpacks", "config", "logs"):
            if not (profile_root / ".minecraft" / folder).is_dir():
                raise RuntimeError(f"Instance game directory was not created: .minecraft/{folder}")
        if not (profile_root / ".minecraft" / "mods" / "legacy-mod.jar").is_file():
            raise RuntimeError("Legacy profile files were not migrated into .minecraft")
        deletion = subprocess.run(
            [str(executable), "--data-dir", str(root)],
            input=json.dumps({"jsonrpc": "2.0", "id": 7, "method": "instances.delete",
                              "params": {"instanceId": profile["id"]}}) + "\n",
            text=True,
            capture_output=True,
            check=True,
        )
        result = json.loads(deletion.stdout.splitlines()[0])
        if result.get("result", {}).get("ok") is not True or profile_root.exists():
            raise RuntimeError("Instance deletion did not remove its data directory")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--python-only", action="store_true", help="Run only the Python tooling unit tests")
    parser.add_argument("--skip-frontend", action="store_true")
    args = parser.parse_args()

    run([sys.executable, "-m", "unittest", "discover", "-s", "tools/tests", "-v"])
    if args.python_only:
        return 0

    env = os.environ.copy()
    ensure_vcpkg(env)
    env.setdefault("VCPKG_ROOT", str(ROOT / "third_party" / "vcpkg"))
    def run_native(command: list[str]) -> None:
        print("+", subprocess.list2cmdline(command), flush=True)
        subprocess.run(command, cwd=ROOT, env=env, check=True)

    run_native(["cmake", "--preset", "default"])
    run_native(["cmake", "--build", "build", "--config", "Debug"])
    run_native(["ctest", "--test-dir", "build", "--output-on-failure"])
    core_integration_smoke()

    if not args.skip_frontend:
        run(["pnpm", "install", "--frozen-lockfile"], cwd=ROOT / "ui")
        run(["pnpm", "run", "build"], cwd=ROOT / "ui")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, subprocess.CalledProcessError, RuntimeError) as error:
        print(f"Test run failed: {error}", file=sys.stderr)
        raise SystemExit(1)
