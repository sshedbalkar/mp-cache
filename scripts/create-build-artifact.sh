#!/usr/bin/env bash
set -euo pipefail

# Create the default local build artifact from an already-created build.
#
# This wrapper keeps the local artifact creation path consistent while
# leaving scripts/package.sh as the single implementation of package layout,
# checksum creation, and build metadata writing.
#
# Defaults:
# - input build directory: build/local-debug
# - output directory: dist/local
# - output archive: dist/local/mp-cache-<build_version>.tar.gz
#
# Usage examples:
#   ./scripts/create-build-artifact.sh
#   ./scripts/create-build-artifact.sh dist/local build/local-debug abc123 2026-05-18T12:00:00Z

if [ "${1:-}" = "--help" ]; then
  cat <<'EOF'
Usage: ./scripts/create-build-artifact.sh [package-dir] [build-dir] [commit] [build-time]

Creates the default local build artifact from an already-created build.

Arguments:
  package-dir
      Output package directory. Default: dist/local.
  build-dir
      Build directory containing binaries. Default: build/local-debug.
  commit
      Artifact commit string. Default: current short git SHA or local.
  build-time
      UTC build time. Default: current UTC timestamp.

Environment:
  MP_CONFIG_PATH
      Optional server config path whose service.build_version is used.
EOF
  exit 0
fi

cd "$(dirname "$0")/.."
. ./scripts/lib/version-env.sh

build_artifact_package_dir="${1:-dist/local}"
build_artifact_build_dir="${2:-build/local-debug}"
build_artifact_commit="${3:-}"
build_artifact_build_time="${4:-}"
build_artifact_version="$(mp_read_build_version_from_config "$MP_CONFIG_PATH")"

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
