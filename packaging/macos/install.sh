#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Installs This Is The Voice from this release archive, without the .pkg installer.
# Run it from Terminal (Gatekeeper does not block a script started with bash):
#
#   cd ~/Downloads/ThisIsTheVoice-<version>-macos
#   bash install.sh
#
# Usage: bash install.sh [--system] [--uninstall]
#   (default)     for the current user: ~/Library/Audio/Plug-Ins (no password needed)
#   --system      for every user: /Library/Audio/Plug-Ins (asks for your password)
#   --uninstall   remove what the same mode installed
#   -h, --help    show this help
#
# The plugins are not signed by an identified developer: the script removes the
# quarantine flag macOS puts on downloaded files (the reason a host would refuse to
# load them) and keeps the ad-hoc signature Apple Silicon requires.

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
NAME="ThisIsTheVoice"
SYSTEM=0
UNINSTALL=0

usage() { sed -n '4,19p' "$0" | sed 's/^# \{0,1\}//'; }

while [ $# -gt 0 ]; do
  case "$1" in
    --system) SYSTEM=1; shift ;;
    --uninstall) UNINSTALL=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

[ "$(uname -s)" = "Darwin" ] || { echo "This script installs the macOS version." >&2; exit 1; }

if [ $SYSTEM -eq 1 ]; then
  BASE="/Library/Audio/Plug-Ins"
  SUDO="sudo"
  [ "$(id -u)" -eq 0 ] && SUDO=""
else
  BASE="$HOME/Library/Audio/Plug-Ins"
  SUDO=""
fi

# bundle|folder
for entry in "$NAME.clap|CLAP" "$NAME.vst3|VST3" "$NAME.component|Components"; do
  bundle="${entry%%|*}"
  dir="$BASE/${entry##*|}"
  if [ $UNINSTALL -eq 1 ]; then
    if [ -e "$dir/$bundle" ]; then
      $SUDO rm -rf "${dir:?}/$bundle"
      echo "Removed $dir/$bundle"
    fi
    continue
  fi
  [ -e "$HERE/$bundle" ] || { echo "Missing $bundle next to this script" >&2; exit 1; }
  $SUDO mkdir -p "$dir"
  $SUDO rm -rf "$dir/$bundle"
  # ditto keeps the bundle and its signature intact.
  $SUDO ditto "$HERE/$bundle" "$dir/$bundle"
  $SUDO xattr -dr com.apple.quarantine "$dir/$bundle" 2> /dev/null || true
  if ! codesign --verify --deep "$dir/$bundle" > /dev/null 2>&1; then
    $SUDO codesign --force --deep --sign - "$dir/$bundle" > /dev/null
  fi
  echo "Installed $dir/$bundle"
done

# Make macOS rescan Audio Units.
killall -9 AudioComponentRegistrar > /dev/null 2>&1 || true

[ $UNINSTALL -eq 1 ] && exit 0
echo
echo "Done. Rescan plug-ins in your host (or restart it) to find This Is The Voice."
echo "Logic Pro: Settings > Plug-in Manager, select This Is The Voice, then Reset & Rescan Selection."
