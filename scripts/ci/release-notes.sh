#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Prints the Markdown notes attached to a release: which file to download for each
# system and how to install it.
#
# Usage: scripts/ci/release-notes.sh <file-version> [dev]
#   file-version   the version in the file names (e.g. 0.9.0 or 0.9.0-dev.42)
#   dev            adds the warning of a development build

set -euo pipefail

label="${1:?usage: release-notes.sh <file-version> [dev]}"
name="ThisIsTheVoice-$label"

if [[ "${2:-}" == "dev" ]]; then
  echo "> **Development build** from the \`dev\` branch ($GITHUB_SHA). It is replaced at every push to \`dev\` and may be unstable; stable versions are the other releases."
  echo
fi

echo "## Downloads"
echo
echo "| System | File | Installs |"
echo "| --- | --- | --- |"
echo "| Linux x86_64 | \`$name-linux-x86_64.tar.gz\` | extract, then \`./install.sh\` (current user) or \`./install.sh --system\` |"
echo "| macOS 10.15+ (Intel and Apple Silicon) | \`$name-macos.zip\` | extract, then in Terminal: \`bash install.sh\` (CLAP, VST3 and AU for the current user; \`--system\` for every user) |"
echo "| macOS, installer alternative | \`$name-macos.pkg\` | CLAP, VST3 and AU for every user |"
echo "| Windows 10+ x64 | \`$name-windows-x64-setup.exe\` | CLAP, VST3 and the standalone application |"
echo
echo "In a host that supports CLAP, pick the CLAP version."
echo
echo "The macOS and Windows files are not signed by an identified developer. On macOS, the zip and its install script need nothing more (the script clears the download quarantine); the .pkg must be allowed in System Settings > Privacy & Security > Open Anyway. On Windows, SmartScreen shows More info, then Run anyway."
