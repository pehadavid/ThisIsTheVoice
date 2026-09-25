#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Installs This Is The Voice from this release archive.
#
# Usage: ./install.sh [--system] [--uninstall]
#   (default)     for the current user: ~/.vst3, ~/.clap, ~/.lv2, ~/.local/bin
#   --system      for every user: /usr/local/lib/{vst3,clap,lv2}, /usr/local/bin (uses sudo)
#   --uninstall   remove what the same mode installed
#   -h, --help    show this help

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NAME="ThisIsTheVoice"
SYSTEM=0
UNINSTALL=0

usage() { sed -n '4,11p' "$0" | sed 's/^# \{0,1\}//'; }

while [[ $# -gt 0 ]]; do
  case "$1" in
    --system) SYSTEM=1; shift ;;
    --uninstall) UNINSTALL=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

if [[ $SYSTEM -eq 1 ]]; then
  PREFIX=/usr/local
  DIRS=("$PREFIX/lib/vst3" "$PREFIX/lib/clap" "$PREFIX/lib/lv2" "$PREFIX/bin")
  SUDO=sudo
  [[ $(id -u) -eq 0 ]] && SUDO=
else
  DIRS=("$HOME/.vst3" "$HOME/.clap" "$HOME/.lv2" "$HOME/.local/bin")
  SUDO=
fi
ITEMS=("$NAME.vst3" "$NAME.clap" "$NAME.lv2" "$NAME")

for i in "${!ITEMS[@]}"; do
  item="${ITEMS[$i]}"
  dir="${DIRS[$i]}"
  if [[ $UNINSTALL -eq 1 ]]; then
    if [[ -e "$dir/$item" ]]; then
      $SUDO rm -rf "${dir:?}/$item"
      echo "Removed $dir/$item"
    fi
    continue
  fi
  [[ -e "$HERE/$item" ]] || { echo "Missing $item in this archive" >&2; exit 1; }
  $SUDO mkdir -p "$dir"
  # Replace in one rename: a running host keeps the files it has open.
  $SUDO rm -rf "$dir/$item.tmp.$$"
  $SUDO cp -a "$HERE/$item" "$dir/$item.tmp.$$"
  [[ -e "$dir/$item" ]] && $SUDO rm -rf "${dir:?}/$item"
  $SUDO mv "$dir/$item.tmp.$$" "$dir/$item"
  echo "Installed $dir/$item"
done

[[ $UNINSTALL -eq 1 ]] && exit 0
echo
echo "Done. Rescan plug-ins in your host (or restart it) to find This Is The Voice."
