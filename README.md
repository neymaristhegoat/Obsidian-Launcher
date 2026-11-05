# 🌑 Obsidian Launcher

**A modern, cross-platform Minecraft: Java Edition launcher.**

Obsidian Launcher is an open-source Minecraft launcher built with Electron and a native C++ core. Designed to make managing Minecraft installations simple, it brings version management, modloader installation, and independent game instances together in one desktop application.

Choose your Minecraft version, install your preferred modloader, manage your instances, and launch the game.

<p align="center">
  <a href="https://github.com/neymaristhegoat/obsidian-launcher/releases">
    <img src="https://img.shields.io/github/v/release/neymaristhegoat/obsidian-launcher?style=for-the-badge&color=7c3aed" alt="Latest Release">
  </a>
  <a href="https://github.com/neymaristhegoat/obsidian-launcher/blob/main/LICENSE">
    <img src="https://img.shields.io/github/license/neymaristhegoat/obsidian-launcher?style=for-the-badge" alt="License">
  </a>
  <a href="https://github.com/neymaristhegoat/obsidian-launcher">
    <img src="https://img.shields.io/github/stars/neymaristhegoat/obsidian-launcher?style=for-the-badge" alt="GitHub Stars">
  </a>
</p>

---

## ✨ Features

- **Minecraft Version Management** — Browse and select available Minecraft versions.
- **Fabric Support** — Install Fabric for compatible Minecraft versions.
- **Forge Support** — Install Forge for compatible Minecraft versions.
- **Instance Management** — Create and manage separate Minecraft installations.
- **Automatic Downloads** — Download the files required to install and launch Minecraft.
- **Independent Game Data** — Keep mods, saves, resource packs, shaders, and configurations organized by instance.
- **Cross-Platform** — Designed for Windows, Linux, and macOS.
- **Native Performance** — Electron frontend powered by a C++20 launcher core.
- **Release Verification** — SHA-256 checksums accompany packaged releases.

## 📥 Download

Get the latest version from the GitHub Releases page.

**[→ Download Obsidian Launcher](https://github.com/neymaristhegoat/obsidian-launcher/releases)**

Available release packages:

| Platform | Available packages |
|---|---|
| Windows | Installer and portable executable |
| Linux | `.tar.gz` archive and installation script |
| macOS | `.dmg` and `.zip` |

### Linux Installation

1. Download the Linux archive and `install-linux.sh` from the releases page.
2. Place both files in the same directory.
3. Extract the archive.
4. Open a terminal in that directory.
5. Run:

```bash
chmod +x install-linux.sh
./install-linux.sh
```

The installer installs Obsidian Launcher for your current user and creates an applications menu entry. Root privileges and FUSE are not required.

## 🛠️ Building From Source

Want to compile Obsidian Launcher yourself? You can build it directly from the source code.

### Requirements

- Git
- Python 3
- CMake 3.25 or newer
- A C++20-compatible compiler
- Node.js and npm for frontend development/build dependencies

### 1. Clone the repository

```bash
git clone https://github.com/neymaristhegoat/obsidian-launcher.git
cd obsidian-launcher
```

### 2. Build the application

Run the packaging script:

```bash
python tools/package.py
```

The resulting installers and checksum files will be placed in:

```text
artifacts/<platform>/download/
```

### 3. Run tests

To run the available build and test checks:

```bash
python tools/test.py
```

Additional testing options:

```bash
# Run Python tooling tests only
python tools/test.py --python-only

# Test Python and native core without rebuilding the frontend
python tools/test.py --skip-frontend
```

### Building for Other Platforms

Native desktop packaging should be performed on the target operating system.

Obsidian Launcher includes a GitHub Actions workflow that builds release packages for Windows, Linux, and macOS.

You can trigger the workflow manually through GitHub Actions or push a version tag beginning with `v`.

Example:

```bash
git tag v1.0.0
git push origin v1.0.0
```

## 📁 User Data & Storage

Obsidian Launcher stores its Minecraft data in a dedicated directory named `obsidian-data`.

| Operating System | Data directory |
|---|---|
| Windows | `%LOCALAPPDATA%\obsidian-data` |
| macOS | `~/Library/Application Support/obsidian-data` |
| Linux | `$XDG_DATA_HOME/obsidian-data` (defaults to `~/.local/share/obsidian-data`) |

This directory contains:

- Launcher settings
- Cached Minecraft manifests
- Shared Minecraft game files
- Individual instance directories
- Logs and configuration files
- Account information

Each instance has its own `instance.json` and `.minecraft` directory, allowing saves, mods, resource packs, shaders, and other game data to remain separated.

Existing profile files are migrated to the new data layout on first launch.

### Account Security

Account refresh tokens are stored using Electron's `safeStorage` encryption.

Depending on your operating system, encryption uses:

- **Windows:** DPAPI
- **macOS:** Keychain
- **Linux:** Secret Service or a compatible wallet provider

On Linux systems without an available OS keyring, account tokens are not persisted.

## 🧩 Technology Stack

Obsidian Launcher combines several technologies:

| Technology | Purpose |
|---|---|
| Electron | Desktop application framework |
| C++20 | Native launcher core |
| CMake | Native build system |
| Java | Minecraft runtime |
| Python | Build, packaging, and testing tools |
| GitHub Actions | Automated cross-platform releases |

## 🗺️ Project Structure

```text
obsidian-launcher/
│
├── .github/
│   └── workflows/       # GitHub Actions workflows
│
├── core/                # Native C++ launcher core
├── java/                # Java-related functionality
├── resources/           # Application resources and icons
├── third_party/         # Third-party dependencies
├── tools/               # Build, packaging, and testing tools
├── ui/                  # Electron frontend
│
├── CMakeLists.txt       # CMake configuration
├── CMakePresets.json    # Build presets
├── Makefile
├── vcpkg.json           # C++ dependency configuration
├── LICENSE
└── README.md
```

## 🐛 Bug Reports & Feature Requests

Found a bug, experienced a crash, or have an idea for a new feature?

Please open an issue on GitHub:

**[→ Report a Bug or Request a Feature](https://github.com/neymaristhegoat/obsidian-launcher/issues)**

When reporting a bug, please include:

- Your operating system and version.
- Your Obsidian Launcher version.
- The Minecraft version and modloader involved, if applicable.
- Steps to reproduce the issue.
- Relevant error messages or logs.

## 🤝 Contributing

Contributions are welcome!

If you would like to improve Obsidian Launcher:

1. Fork the repository.
2. Create a new branch for your changes.
3. Make and test your changes.
4. Commit and push your work.
5. Open a pull request.

Please keep contributions focused, tested, and consistent with the existing project structure.

## 📜 License

Obsidian Launcher is distributed under the **Apache License 2.0**.

See the [LICENSE](LICENSE) file for more information.

## ⚠️ Disclaimer

Obsidian Launcher is an independent, open-source project.

It is not affiliated with, endorsed by, or officially associated with Mojang Studios or Microsoft.

Minecraft is a trademark of Mojang Studios and Microsoft.

---

<p align="center">
  <strong>Obsidian Launcher</strong>
  <br>
  Built for Minecraft players who want more control over their installations.
  <br><br>
  <a href="https://github.com/neymaristhegoat/obsidian-launcher">GitHub Repository</a>
  ·
  <a href="https://github.com/neymaristhegoat/obsidian-launcher/releases">Releases</a>
  ·
  <a href="https://github.com/neymaristhegoat/obsidian-launcher/issues">Issues</a>
</p>
