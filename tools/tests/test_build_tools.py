from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))

import package as packaging  # noqa: E402
import release  # noqa: E402
import gen_version_fixtures  # noqa: E402


class PackagingTests(unittest.TestCase):
    def test_each_platform_has_native_installer_targets(self) -> None:
        self.assertEqual(packaging.electron_targets("windows"), ["--win", "nsis", "portable"])
        self.assertEqual(packaging.electron_targets("macos"), ["--mac", "dmg", "zip"])
        self.assertEqual(packaging.electron_targets("linux"), ["--linux", "tar.gz"])

    def test_core_executable_name_is_platform_specific(self) -> None:
        root = Path("build")
        self.assertEqual(packaging.core_binary(root, "windows"), root / "core/launcher_core_cli.exe")
        self.assertEqual(packaging.core_binary(root, "linux"), root / "core/launcher_core_cli")

    def test_triplets_cover_supported_platforms_and_architectures(self) -> None:
        self.assertEqual(packaging.vcpkg_triplet("windows", "AMD64"), "x64-windows-static")
        self.assertEqual(packaging.vcpkg_triplet("macos", "arm64"), "arm64-osx")
        self.assertEqual(packaging.vcpkg_triplet("linux", "aarch64"), "arm64-linux")

    def test_release_manifest_hashes_installer_files(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            installer = output / "Obsidian-Launcher-test.tar.gz"
            installer.write_bytes(b"packaged-launcher")
            install_script = output / "install-linux.sh"
            install_script.write_text("#!/usr/bin/env bash\n", encoding="utf-8")
            manifest_path = release.write_manifest(output, "linux")
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            self.assertEqual(manifest["platform"], "linux")
            self.assertEqual(manifest["artifacts"][0]["sha256"], release.sha256(installer))
            self.assertEqual(
                {entry["file"] for entry in manifest["artifacts"]},
                {installer.name, install_script.name},
            )
            self.assertTrue((output / "SHA256SUMS-linux.txt").is_file())
            downloads = release.stage_downloads(output)
            self.assertTrue((downloads / installer.name).is_file())
            self.assertTrue((downloads / install_script.name).is_file())
            self.assertTrue((downloads / manifest_path.name).is_file())

    def test_version_fixture_is_a_mojang_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            output = gen_version_fixtures.write_fixture(Path(temporary) / "fixture.json")
            manifest = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(manifest["latest"]["release"], "1.21.4")
            self.assertEqual([entry["type"] for entry in manifest["versions"]], ["release", "snapshot"])

    def test_platform_app_icons_are_present(self) -> None:
        resources = TOOLS.parent / "resources"
        self.assertTrue((resources / "icon.png").read_bytes().startswith(b"\x89PNG\r\n\x1a\n"))
        self.assertEqual((resources / "icon.ico").read_bytes()[:4], b"\0\0\1\0")
        self.assertEqual((resources / "icon.icns").read_bytes()[:4], b"icns")


if __name__ == "__main__":
    unittest.main()
