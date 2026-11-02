# Obsidian Launcher

Obsidian Launcher is an Electron desktop app with a C++ launcher core. Build the installer for the computer you are using with:

```sh
python tools/package.py
```

The installer and checksum files are written to `artifacts/<platform>/download/`. On Linux, the download contains a `.tar.gz` bundle and `install-linux.sh`. Extract both into the same folder, then run `./install-linux.sh`; it installs the app for the current user and adds an applications menu entry without requiring FUSE or root access. On Windows the build produces an installer and portable `.exe`; on macOS it produces a `.dmg` and `.zip`.

Native desktop packaging must run on its target operating system. The [Package desktop apps workflow](.github/workflows/package.yml) builds Linux, Windows, and macOS artifacts on their respective runners. Start it from GitHub Actions, or push a `v*` tag.

Run the test and build checks with:

```sh
python tools/test.py
```

Use `python tools/test.py --python-only` for the Python tooling tests, or `python tools/test.py --skip-frontend` to check Python and native core behavior without rebuilding the UI.

`python tools/gen_icons.py` regenerates the PNG, ICO, and ICNS app icons from `resources/logo.svg` (requires `rsvg-convert`).

## User data

The launcher keeps Minecraft data in a platform data directory named `obsidian-data`:

- Windows: `%LOCALAPPDATA%\obsidian-data`
- macOS: `~/Library/Application Support/obsidian-data`
- Linux: `$XDG_DATA_HOME/obsidian-data`, defaulting to `~/.local/share/obsidian-data`

This directory holds launcher settings, cached manifests, shared game files, logs, and per-instance folders. Each instance has an `instance.json` and a `.minecraft` directory for mods, saves, resource packs, shaders, configuration, and logs. Existing profile files are migrated into this layout on first launch.

Refresh tokens are stored in encrypted `accounts.json` using Electron `safeStorage` (Keychain on macOS, DPAPI on Windows, and a Secret Service or wallet provider on Linux). On Linux systems without an OS keyring, account tokens are not persisted.
