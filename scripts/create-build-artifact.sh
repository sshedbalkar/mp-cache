#!/usr/bin/env bash
set -euo pipefail

# Create the default local build artifact from an already-created build.
#
# This wrapper keeps the artifact location stable for local deployment while
# leaving scripts/package.sh as the single implementation of package layout,
# checksum creation, and build metadata writing.
#
# Defaults:
# - input build directory: build/local-debug
# - output directory: dist/local
# - output archive: dist/local/mp-cache-local.tar.gz
#
# Usage examples:
#   ./scripts/create-build-artifact.sh
#   ./scripts/create-build-artifact.sh [package-dir] [build-dir] [version] [commit] [build-time]

cd "$(dirname "$0")/.."

build_artifact_package_dir="${1:-dist/local}"
build_artifact_build_dir="${2:-build/local-debug}"
build_artifact_version="${3:-local}"
build_artifact_commit="${4:-}"
build_artifact_build_time="${5:-}"

if [ -z "$build_artifact_commit" ]; then
  build_artifact_commit="$(git rev-parse --short HEAD 2>/dev/null || printf 'local')"
fi

if [ -z "$build_artifact_build_time" ]; then
  build_artifact_build_time="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
fi

[ -x "$build_artifact_build_dir/mp-cache-server" ] || {
  printf 'missing executable build file: %s/mp-cache-server\n' "$build_artifact_build_dir" >&2
  exit 1
}
[ -x "$build_artifact_build_dir/mp-cachectl" ] || {
  printf 'missing executable build file: %s/mp-cachectl\n' "$build_artifact_build_dir" >&2
  exit 1
}

./scripts/package.sh \
  "$build_artifact_package_dir" \
  "$build_artifact_build_dir" \
  "$build_artifact_version" \
  "$build_artifact_commit" \
  "$build_artifact_build_time"
