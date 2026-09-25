#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds a release archive for Linux from an existing build:
#   dist/ThisIsTheVoice-<version>-linux-<arch>.tar.gz
# holding the VST3, CLAP and LV2 plugins, the standalone application, install.sh,
# README.md and LICENSE.
#
# Usage: scripts/package-linux.sh [build-dir]   (default: build/release)
# TITV_PACKAGE_VERSION overrides the version in the file name (e.g. 0.9.0-dev.42).

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$(cd "${1:-$ROOT/build/release}" && pwd)"
NAME="ThisIsTheVoice"
VERSION="$(sed -n 's/^project(ThisIsTheVoice VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"
ARCH="$(uname -m)"
PACKAGE="$NAME-${TITV_PACKAGE_VERSION:-$VERSION}-linux-$ARCH"
DIST="$ROOT/dist"
STAGE="$DIST/$PACKAGE"

[[ -n "$VERSION" ]] || { echo "Could not read the version from CMakeLists.txt" >&2; exit 1; }
for item in "$NAME.vst3" "$NAME.clap" "$NAME.lv2" "$NAME"; do
  [[ -e "$BUILD_DIR/bin/$item" ]] || { echo "Missing $BUILD_DIR/bin/$item: build first" >&2; exit 1; }
done

rm -rf "$STAGE"
mkdir -p "$STAGE"
cp -a "$BUILD_DIR/bin/$NAME.vst3" "$BUILD_DIR/bin/$NAME.clap" "$BUILD_DIR/bin/$NAME.lv2" "$BUILD_DIR/bin/$NAME" "$STAGE/"
cp "$ROOT/packaging/linux/install.sh" "$STAGE/"
chmod +x "$STAGE/install.sh"
cp "$ROOT/README.md" "$ROOT/LICENSE" "$STAGE/"
cp "$ROOT/external/DPF/LICENSE" "$STAGE/LICENSE-DPF"

# Strip debug symbols from the binaries to keep the archive small.
find "$STAGE" -type f \( -name '*.so' -o -name '*.clap' -o -name "$NAME" \) -exec strip --strip-unneeded {} + 2> /dev/null || true

tar -C "$DIST" -czf "$DIST/$PACKAGE.tar.gz" "$PACKAGE"
rm -rf "$STAGE"
echo "$DIST/$PACKAGE.tar.gz"
