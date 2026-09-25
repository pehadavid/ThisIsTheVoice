#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds Steinberg's VST3 validator from the VST3 SDK (MIT licence) and runs it on the
# VST3 plugin. Only the modules the validator needs are fetched; the editorhost
# example (which needs GTK) is left out.
#
# Usage: scripts/ci/vst3-validator.sh [build-dir]   (default: build/release)

set -euo pipefail

BUILD_DIR="$(cd "${1:-build/release}" && pwd)"
SDK="$BUILD_DIR/tools/vst3sdk"
VALIDATOR="$SDK/build/bin/Release/validator"

if [[ ! -x "$VALIDATOR" ]]; then
  rm -rf "$SDK"
  git clone -q --depth 1 https://github.com/steinbergmedia/vst3sdk.git "$SDK"
  git -C "$SDK" submodule update -q --init --depth 1 base pluginterfaces public.sdk cmake
  editorhost="$SDK/public.sdk/samples/vst-hosting/editorhost/CMakeLists.txt"
  [[ -f "$editorhost" ]] && mv "$editorhost" "$editorhost.disabled"
  cmake -S "$SDK" -B "$SDK/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DSMTG_ENABLE_VSTGUI_SUPPORT=OFF -DSMTG_ENABLE_VST3_PLUGIN_EXAMPLES=OFF \
        -DSMTG_ENABLE_VST3_HOSTING_EXAMPLES=ON -DSMTG_RUN_VST_VALIDATOR=OFF > /dev/null
  cmake --build "$SDK/build" --target validator > /dev/null
fi

log="$BUILD_DIR/vst3-validator.log"
"$VALIDATOR" "$BUILD_DIR/bin/ThisIsTheVoice.vst3" > "$log" 2>&1 || true
grep -v '^\[dpf\]' "$log" | grep -E "Result:|ERROR" || true
grep -q "Result: .* 0 tests failed" "$log"
