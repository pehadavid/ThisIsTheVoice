#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Runs the plugin validators on a build, on Linux, macOS or Windows (Git Bash):
#   clap-validator on the CLAP plugin, pluginval (strictness 10) on the VST3 plugin.
# The tools are downloaded into <build-dir>/tools with the GitHub CLI (gh), which
# needs GH_TOKEN in CI.
#
# Usage: scripts/ci/validate.sh [build-dir]   (default: build/release)

set -euo pipefail

BUILD_DIR="${1:-build/release}"
TOOLS="$BUILD_DIR/tools"
CLAP_VALIDATOR_VERSION="0.4.1"
PLUGINVAL_VERSION="v1.0.4"
mkdir -p "$TOOLS"

case "$(uname -s)" in
  Linux)  os=linux; clap_asset='*ubuntu-22.04.zip'; pluginval_asset='pluginval_Linux.zip'; exe='' ;;
  Darwin) os=macos; clap_asset='*macos-universal.zip'; pluginval_asset='pluginval_macOS.zip'; exe='' ;;
  MINGW*|MSYS*|CYGWIN*) os=windows; clap_asset='*windows.zip'; pluginval_asset='pluginval_Windows.zip'; exe='.exe' ;;
  *) echo "Unsupported system: $(uname -s)" >&2; exit 1 ;;
esac

# clap-validator: the archive may hold the binary or a tarball with it.
if [[ ! -x "$TOOLS/clap/clap-validator$exe" ]]; then
  rm -rf "$TOOLS/clap" && mkdir -p "$TOOLS/clap"
  gh release download "$CLAP_VALIDATOR_VERSION" -R free-audio/clap-validator -p "$clap_asset" -D "$TOOLS/clap"
  (cd "$TOOLS/clap" && unzip -oq ./*.zip && for t in ./*.tar.gz; do [[ -e "$t" ]] && tar xzf "$t"; done; true)
  found="$(find "$TOOLS/clap" -name "clap-validator$exe" -type f | head -1)"
  [[ -n "$found" ]] || { echo "clap-validator binary not found in the release archive" >&2; exit 1; }
  [[ "$found" == "$TOOLS/clap/clap-validator$exe" ]] || mv "$found" "$TOOLS/clap/clap-validator$exe"
  chmod +x "$TOOLS/clap/clap-validator$exe"
fi

if [[ ! -d "$TOOLS/pluginval" ]]; then
  mkdir -p "$TOOLS/pluginval"
  gh release download "$PLUGINVAL_VERSION" -R Tracktion/pluginval -p "$pluginval_asset" -D "$TOOLS/pluginval"
  (cd "$TOOLS/pluginval" && unzip -oq ./*.zip)
fi
case "$os" in
  macos) pluginval="$TOOLS/pluginval/pluginval.app/Contents/MacOS/pluginval" ;;
  *) pluginval="$TOOLS/pluginval/pluginval$exe" ;;
esac

echo "== clap-validator"
"$TOOLS/clap/clap-validator$exe" validate "$BUILD_DIR/bin/ThisIsTheVoice.clap"

echo "== pluginval (VST3, strictness 10)"
"$pluginval" --strictness-level 10 --validate-in-process --skip-gui-tests "$BUILD_DIR/bin/ThisIsTheVoice.vst3"
