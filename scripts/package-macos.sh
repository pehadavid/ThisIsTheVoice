#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds the macOS release files from an existing build:
#   dist/ThisIsTheVoice-<version>-macos.zip   plugins + install.sh, run from Terminal
#                                              (needs no identified developer)
#   dist/ThisIsTheVoice-<version>-macos.pkg   installer with one choice per format,
#                                              for every user:
#   AU   -> /Library/Audio/Plug-Ins/Components
#   VST3 -> /Library/Audio/Plug-Ins/VST3
#   CLAP -> /Library/Audio/Plug-Ins/CLAP
#
# Usage: scripts/package-macos.sh [build-dir]   (default: build/macos)
# TITV_PACKAGE_VERSION overrides the version in the file names and titles
# (e.g. 0.9.0-dev.42); the package metadata keeps the numeric version.
#
# Signing is optional and driven by the environment:
#   CODESIGN_IDENTITY    "Developer ID Application: ..." for the plugins (default: ad-hoc)
#   INSTALLER_IDENTITY   "Developer ID Installer: ..." for the package (default: unsigned)
#   NOTARY_PROFILE       notarytool keychain profile: notarize and staple the package
# Without them the package installs fine, but Gatekeeper asks for confirmation
# (right-click > Open) since the developer is not identified.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$(cd "${1:-$ROOT/build/macos}" && pwd)"
NAME="ThisIsTheVoice"
BUNDLE_ID="io.github.pehadavid.thisisthevoice"
VERSION="$(sed -n 's/^project(ThisIsTheVoice VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"
DIST="$ROOT/dist"
WORK="$DIST/macos-pkg"
LABEL="${TITV_PACKAGE_VERSION:-$VERSION}"
OUTPUT="$DIST/$NAME-$LABEL-macos.pkg"

[[ -n "$VERSION" ]] || { echo "Could not read the version from CMakeLists.txt" >&2; exit 1; }

rm -rf "$WORK"
mkdir -p "$WORK"

# format | bundle | install location | title
FORMATS=(
  "au|$NAME.component|/Library/Audio/Plug-Ins/Components|Audio Unit (Logic Pro, GarageBand, ...)"
  "vst3|$NAME.vst3|/Library/Audio/Plug-Ins/VST3|VST3"
  "clap|$NAME.clap|/Library/Audio/Plug-Ins/CLAP|CLAP"
)

choices=""
outline=""
for entry in "${FORMATS[@]}"; do
  IFS='|' read -r format bundle location title <<< "$entry"
  src="$BUILD_DIR/bin/$bundle"
  [[ -e "$src" ]] || { echo "Missing $src: build first" >&2; exit 1; }

  root="$WORK/root-$format"
  mkdir -p "$root"
  cp -R "$src" "$root/"
  if [[ -n "${CODESIGN_IDENTITY:-}" ]]; then
    codesign --force --deep --timestamp --options runtime --sign "$CODESIGN_IDENTITY" "$root/$bundle"
  else
    # Ad-hoc: required for Apple Silicon to load the code at all.
    codesign --force --deep --sign - "$root/$bundle"
  fi

  # Install the bundle where it belongs, even if a copy was moved elsewhere
  # (pkgbuild makes bundles relocatable by default).
  pkgbuild --analyze --root "$root" "$WORK/$format.plist" > /dev/null
  i=0
  while /usr/libexec/PlistBuddy -c "Print :$i" "$WORK/$format.plist" > /dev/null 2>&1; do
    # The key is not always written by --analyze: replace it either way.
    /usr/libexec/PlistBuddy -c "Delete :$i:BundleIsRelocatable" "$WORK/$format.plist" > /dev/null 2>&1 || true
    /usr/libexec/PlistBuddy -c "Add :$i:BundleIsRelocatable bool false" "$WORK/$format.plist"
    i=$((i + 1))
  done

  pkgbuild --root "$root" --component-plist "$WORK/$format.plist" --install-location "$location" \
           --identifier "$BUNDLE_ID.$format" --version "$VERSION" "$WORK/$NAME-$format.pkg" > /dev/null

  choices+="  <choice id=\"$format\" title=\"$title\" start_selected=\"true\">
    <pkg-ref id=\"$BUNDLE_ID.$format\"/>
  </choice>
  <pkg-ref id=\"$BUNDLE_ID.$format\" version=\"$VERSION\">$NAME-$format.pkg</pkg-ref>
"
  outline+="    <line choice=\"$format\"/>
"
done

cp "$ROOT/LICENSE" "$WORK/LICENSE.txt"
cat > "$WORK/distribution.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
  <title>This Is The Voice $LABEL</title>
  <license file="LICENSE.txt"/>
  <options customize="allow" require-scripts="false" hostArchitectures="x86_64,arm64"/>
  <domains enable_localSystem="true"/>
  <volume-check><allowed-os-versions><os-version min="10.15"/></allowed-os-versions></volume-check>
  <choices-outline>
$outline  </choices-outline>
$choices</installer-gui-script>
EOF

sign_args=()
[[ -n "${INSTALLER_IDENTITY:-}" ]] && sign_args=(--sign "$INSTALLER_IDENTITY")
productbuild --distribution "$WORK/distribution.xml" --package-path "$WORK" --resources "$WORK" \
             ${sign_args[@]+"${sign_args[@]}"} "$OUTPUT" > /dev/null

if [[ -n "${NOTARY_PROFILE:-}" ]]; then
  xcrun notarytool submit "$OUTPUT" --keychain-profile "$NOTARY_PROFILE" --wait
  xcrun stapler staple "$OUTPUT"
fi

# Zip: the signed bundles, the install script and the licence.
ZIPDIR="$WORK/zip/$NAME-$LABEL-macos"
mkdir -p "$ZIPDIR"
for entry in "${FORMATS[@]}"; do
  IFS='|' read -r format bundle location title <<< "$entry"
  ditto "$WORK/root-$format/$bundle" "$ZIPDIR/$bundle"
done
cp "$ROOT/packaging/macos/install.sh" "$ZIPDIR/install.sh"
chmod +x "$ZIPDIR/install.sh"
cp "$ROOT/README.md" "$ROOT/LICENSE" "$ZIPDIR/"
cp "$ROOT/external/DPF/LICENSE" "$ZIPDIR/LICENSE-DPF"
ZIP="$DIST/$NAME-$LABEL-macos.zip"
rm -f "$ZIP"
# ditto keeps bundle structure and signatures (plain zip may not).
(cd "$WORK/zip" && ditto -c -k --keepParent "$NAME-$LABEL-macos" "$ZIP")

rm -rf "$WORK"
echo "$ZIP"
echo "$OUTPUT"
