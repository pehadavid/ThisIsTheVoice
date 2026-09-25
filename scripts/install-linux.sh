#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds this checkout and installs the plugin for the current user:
#   VST3 -> ~/.vst3   CLAP -> ~/.clap   LV2 -> ~/.lv2   standalone -> ~/.local/bin
#
# Usage: scripts/install-linux.sh [options]
#   --formats LIST   comma-separated subset of vst3,clap,lv2,standalone (default: all)
#   --link           symlink the build outputs instead of copying them: every later
#                    build is picked up by the DAW without reinstalling
#   --skip-tests     do not run the engine tests before installing
#   --uninstall      remove the installed files and exit
#   -h, --help       show this help

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT/build/release"
NAME="ThisIsTheVoice"

FORMATS="vst3,clap,lv2,standalone"
LINK=0
RUN_TESTS=1
UNINSTALL=0

usage() { sed -n '4,14p' "$0" | sed 's/^# \{0,1\}//'; }

while [[ $# -gt 0 ]]; do
  case "$1" in
    --formats) FORMATS="${2:?--formats needs a list}"; shift 2 ;;
    --link) LINK=1; shift ;;
    --skip-tests) RUN_TESTS=0; shift ;;
    --uninstall) UNINSTALL=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

# format -> "build output|install directory"
declare -A TARGETS=(
  [vst3]="$NAME.vst3|$HOME/.vst3"
  [clap]="$NAME.clap|$HOME/.clap"
  [lv2]="$NAME.lv2|$HOME/.lv2"
  [standalone]="$NAME|$HOME/.local/bin"
)

IFS=',' read -r -a SELECTED <<< "$FORMATS"
for f in "${SELECTED[@]}"; do
  [[ -n "${TARGETS[$f]:-}" ]] || { echo "Unknown format: $f (expected vst3, clap, lv2, standalone)" >&2; exit 2; }
done

if [[ $UNINSTALL -eq 1 ]]; then
  for f in "${SELECTED[@]}"; do
    IFS='|' read -r output dir <<< "${TARGETS[$f]}"
    if [[ -e "$dir/$output" || -L "$dir/$output" ]]; then
      rm -rf "${dir:?}/$output"
      echo "Removed $dir/$output"
    fi
  done
  exit 0
fi

if [[ ! -f "$ROOT/external/DPF/CMakeLists.txt" || ! -d "$ROOT/external/DPF/dgl/src/pugl-upstream/include" ]]; then
  echo "Fetching submodules..."
  git -C "$ROOT" submodule update --init --recursive
fi

GENERATOR=()
command -v ninja > /dev/null && GENERATOR=(-G Ninja)

# A build directory configured with another generator cannot be reused.
if [[ -f "$BUILD_DIR/CMakeCache.txt" ]]; then
  cached="$(sed -n 's/^CMAKE_GENERATOR:INTERNAL=//p' "$BUILD_DIR/CMakeCache.txt")"
  wanted="${GENERATOR[1]:-Unix Makefiles}"
  [[ "$cached" == "$wanted" ]] || GENERATOR=(-G "$cached")
fi

echo "Configuring and building ($BUILD_DIR)..."
cmake -S "$ROOT" -B "$BUILD_DIR" "${GENERATOR[@]}" -DCMAKE_BUILD_TYPE=Release > /dev/null
cmake --build "$BUILD_DIR" --parallel

if [[ $RUN_TESTS -eq 1 ]]; then
  echo "Running engine tests..."
  ctest --test-dir "$BUILD_DIR" --output-on-failure > /dev/null || {
    echo "Tests failed; nothing installed. Run ctest --test-dir $BUILD_DIR --output-on-failure for details." >&2
    exit 1
  }
fi

for f in "${SELECTED[@]}"; do
  IFS='|' read -r output dir <<< "${TARGETS[$f]}"
  src="$BUILD_DIR/bin/$output"
  [[ -e "$src" ]] || { echo "Missing build output: $src" >&2; exit 1; }
  mkdir -p "$dir"
  dest="$dir/$output"

  if [[ $LINK -eq 1 ]]; then
    ln -sfn "$src" "$dest.tmp.$$"
  else
    cp -a "$src" "$dest.tmp.$$"
  fi
  # Replace in one rename: a running DAW keeps the old files it has open, and a
  # plugin scan never sees a half-copied bundle.
  rm -rf "$dest.old.$$"
  [[ -e "$dest" || -L "$dest" ]] && mv "$dest" "$dest.old.$$"
  mv "$dest.tmp.$$" "$dest"
  rm -rf "$dest.old.$$"
  echo "Installed $f: $dest$([[ $LINK -eq 1 ]] && echo " -> $src")"
done

version="$(git -C "$ROOT" describe --always --dirty 2> /dev/null || echo unknown)"
echo
echo "This Is The Voice ($version) installed."
echo "In Bitwig Studio: Settings > Locations > Plug-ins, keep 'Use Bitwig default locations' on"
echo "(it scans ~/.vst3 and ~/.clap), then rescan or restart Bitwig if the plugin is already loaded."
