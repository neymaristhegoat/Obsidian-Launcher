#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
archive=${1:-}

if [[ -z "$archive" ]]; then
  shopt -s nullglob
  archives=("$script_dir"/*.tar.gz)
  if (( ${#archives[@]} != 1 )); then
    printf 'Expected one .tar.gz launcher archive beside this installer; found %d.\n' "${#archives[@]}" >&2
    printf 'Usage: %s /path/to/Obsidian-Launcher.tar.gz\n' "$0" >&2
    exit 1
  fi
  archive=${archives[0]}
fi

if [[ ! -f "$archive" ]]; then
  printf 'Archive not found: %s\n' "$archive" >&2
  exit 1
fi

temporary_dir=$(mktemp -d)
trap 'rm -rf "$temporary_dir"' EXIT
tar -xzf "$archive" -C "$temporary_dir"

asar_path=$(find "$temporary_dir" -type f -path '*/resources/app.asar' -print -quit)
if [[ -z "$asar_path" ]]; then
  printf 'The archive does not contain a packaged Obsidian Launcher app.\n' >&2
  exit 1
fi
app_root=${asar_path%/resources/app.asar}
if [[ ! -x "$app_root/ui" ]]; then
  printf 'The app archive is missing its executable: %s/ui\n' "$app_root" >&2
  exit 1
fi

data_home=${XDG_DATA_HOME:-"$HOME/.local/share"}
bin_home=${XDG_BIN_HOME:-"$HOME/.local/bin"}
install_dir="$data_home/obsidian-launcher"
launcher="$bin_home/obsidian-launcher"
desktop_dir=${XDG_DATA_HOME:-"$HOME/.local/share"}/applications
desktop_file="$desktop_dir/obsidian-launcher.desktop"

mkdir -p "$install_dir" "$bin_home" "$desktop_dir"
cp -a "$app_root/." "$install_dir/"

cat > "$launcher" <<'LAUNCHER'
#!/bin/sh
data_home=${XDG_DATA_HOME:-"$HOME/.local/share"}
exec "$data_home/obsidian-launcher/ui" "$@"
LAUNCHER
chmod 755 "$launcher" "$install_dir/ui"

# Quote the absolute path for the Desktop Entry Exec field.
desktop_launcher=${launcher//\\/\\\\}
desktop_launcher=${desktop_launcher//\"/\\\"}
cat > "$desktop_file" <<DESKTOP
[Desktop Entry]
Type=Application
Name=Obsidian Launcher
Comment=Manage and launch Minecraft instances
Exec="$desktop_launcher" %U
Icon=$install_dir/resources/icon.png
Terminal=false
Categories=Game;
StartupWMClass=obsidian-launcher
DESKTOP
chmod 644 "$desktop_file"

printf 'Installed Obsidian Launcher to %s\n' "$install_dir"
printf 'Launch it from your applications menu or run: %s\n' "$launcher"
