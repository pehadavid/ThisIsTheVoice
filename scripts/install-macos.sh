#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds this checkout (universal: Intel and Apple Silicon) and installs the plugin
# for the current user:
#   AU   -> ~/Library/Audio/Plug-Ins/Components
#   VST3 -> ~/Library/Audio/Plug-Ins/VST3
#   CLAP -> ~/Library/Audio/Plug-Ins/CLAP
# then validates the Audio Unit with Apple's auval.
#
# Usage: scripts/install-macos.sh [options]
#   --formats LIST   comma-separated subset of au,vst3,clap (default: all)
#   --skip-tests     do not run the engine tests before installing
#   --skip-auval     do not run auval after installing the AU
#   --uninstall      remove the installed files and exit
#   -h, --help       show this help
#
# Requires Xcode or the Command Line Tools, and CMake.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT/build/macos"
NAME="ThisIsTheVoice"
PLUGINS="$HOME/Library/Audio/Plug-Ins"
# Must match DISTRHO_PLUGIN_AU_TYPE, DISTRHO_PLUGIN_UNIQUE_ID and DISTRHO_PLUGIN_BRAND_ID.
AU_TYPE="aufx" AU_SUBTYPE="TiTV" AU_MANUFACTURER="Phdv"

FORMATS="au,vst3,clap"
RUN_TESTS=1
RUN_AUVAL=1
UNINSTALL=0

usage() { sed -n '4,19p' "$0" | sed 's/^# \{0,1\}//'; }

while [[ $# -gt 0 ]]; do
  case "$1" in
    --formats) FORMATS="${2:?--formats needs a list}"; shift 2 ;;
    --skip-tests) RUN_TESTS=0; shift ;;
    --skip-auval) RUN_AUVAL=0; shift ;;
    --uninstall) UNINSTALL=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

[[ "$(uname -s)" == "Darwin" ]] || { echo "This script runs on macOS; use scripts/install-linux.sh on Linux." >&2; exit 1; }

# format -> "build output|install directory"
target_for() {
  case "$1" in
    au) echo "$NAME.component|$PLUGINS/Components" ;;
    vst3) echo "$NAME.vst3|$PLUGINS/VST3" ;;
    clap) echo "$NAME.clap|$PLUGINS/CLAP" ;;
    *) return 1 ;;
  esac
}

IFS=',' read -r -a SELECTED <<< "$FORMATS"
for f in "${SELECTED[@]}"; do
  target_for "$f" > /dev/null || { echo "Unknown format: $f (expected au, vst3, clap)" >&2; exit 2; }
done

if [[ $UNINSTALL -eq 1 ]]; then
  for f in "${SELECTED[@]}"; do
    IFS='|' read -r output dir <<< "$(target_for "$f")"
    if [[ -e "$dir/$output" ]]; then
      rm -rf "${dir:?}/$output"
      echo "Removed $dir/$output"
    fi
  done
  killall -9 AudioComponentRegistrar 2> /dev/null || true
  exit 0
fi

if [[ ! -f "$ROOT/external/DPF/CMakeLists.txt" || ! -d "$ROOT/external/DPF/dgl/src/pugl-upstream/include" ]]; then
  echo "Fetching submodules..."
  git -C "$ROOT" submodule update --init --recursive
fi

GENERATOR=()
command -v ninja > /dev/null && GENERATOR=(-G Ninja)
if [[ -f "$BUILD_DIR/CMakeCache.txt" ]]; then
  cached="$(sed -n 's/^CMAKE_GENERATOR:INTERNAL=//p' "$BUILD_DIR/CMakeCache.txt")"
  wanted="${GENERATOR[1]:-Unix Makefiles}"
  [[ "$cached" == "$wanted" ]] || GENERATOR=(-G "$cached")
fi

echo "Configuring and building ($BUILD_DIR)..."
# ${arr[@]+...}: bash 3.2 (the macOS default) rejects empty arrays under set -u.
cmake -S "$ROOT" -B "$BUILD_DIR" ${GENERATOR[@]+"${GENERATOR[@]}"} -DCMAKE_BUILD_TYPE=Release > /dev/null
cmake --build "$BUILD_DIR" --parallel

if [[ $RUN_TESTS -eq 1 ]]; then
  echo "Running engine tests..."
  ctest --test-dir "$BUILD_DIR" --output-on-failure > /dev/null || {
    echo "Tests failed; nothing installed. Run ctest --test-dir $BUILD_DIR --output-on-failure for details." >&2
    exit 1
  }
fi

for f in "${SELECTED[@]}"; do
  IFS='|' read -r output dir <<< "$(target_for "$f")"
  src="$BUILD_DIR/bin/$output"
  [[ -e "$src" ]] || { echo "Missing build output: $src" >&2; exit 1; }
  mkdir -p "$dir"
  dest="$dir/$output"
  rm -rf "$dest.tmp.$$"
  cp -R "$src" "$dest.tmp.$$"
  # Ad-hoc signature: Apple Silicon refuses to load unsigned code. Distribution needs
  # a Developer ID signature and notarization instead.
  codesign --force --deep --sign - "$dest.tmp.$$" > /dev/null
  rm -rf "$dest"
  mv "$dest.tmp.$$" "$dest"
  echo "Installed $f: $dest"
done

for f in "${SELECTED[@]}"; do
  [[ "$f" == "au" ]] || continue
  # Make the system rescan Audio Units.
  killall -9 AudioComponentRegistrar 2> /dev/null || true
  if [[ $RUN_AUVAL -eq 1 ]]; then
    echo "Validating the Audio Unit (auval)..."
    if auval -v "$AU_TYPE" "$AU_SUBTYPE" "$AU_MANUFACTURER" > "$BUILD_DIR/auval.log" 2>&1; then
      echo "auval: PASSED (log: $BUILD_DIR/auval.log)"
    else
      echo "auval: FAILED, see $BUILD_DIR/auval.log" >&2
      tail -20 "$BUILD_DIR/auval.log" >&2
      exit 1
    fi
  fi
done

version="$(git -C "$ROOT" describe --always --dirty 2> /dev/null || echo unknown)"
echo
echo "This Is The Voice ($version) installed."
echo "Logic Pro: Settings > Plug-in Manager, then Reset & Rescan Selection if the plugin was already listed."
