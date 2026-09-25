#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Prints the version of the next release, from the base version in CMakeLists.txt and
# the release tags that already exist:
#   - the base version, if no release is at or above it yet (first release, or a
#     manual minor/major bump of TITV_BASE_VERSION);
#   - otherwise the latest released version with its patch number raised by one.
#
# Usage: scripts/ci/version.sh [tag...]
#   With no arguments the tags are read from the origin remote (v<major>.<minor>.<patch>).

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
base="$(sed -n 's/^set(TITV_BASE_VERSION \([0-9][0-9.]*\)).*/\1/p' "$ROOT/CMakeLists.txt")"
[[ "$base" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "Bad TITV_BASE_VERSION in CMakeLists.txt: '$base'" >&2; exit 1; }

if [[ $# -gt 0 ]]; then
  tags=("$@")
else
  tags=()
  while read -r _ ref; do
    tags+=("${ref#refs/tags/}")
  done < <(git -C "$ROOT" ls-remote --tags --refs origin 'v*')
fi

latest=""
for tag in ${tags[@]+"${tags[@]}"}; do
  v="${tag#v}"
  [[ "$v" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || continue
  if [[ -z "$latest" ]] || [[ "$(printf '%s\n%s\n' "$latest" "$v" | sort -V | tail -1)" == "$v" ]]; then
    latest="$v"
  fi
done

# The base wins while no release is at or above it.
if [[ -z "$latest" ]] || [[ "$(printf '%s\n%s\n' "$base" "$latest" | sort -V | tail -1)" == "$base" && "$base" != "$latest" ]]; then
  echo "$base"
else
  IFS=. read -r major minor patch <<< "$latest"
  echo "$major.$minor.$((patch + 1))"
fi
