#!/usr/bin/env bash
set -euo pipefail

# Builds the default local debug server and refreshes the local artifact.
#
# Usage examples:
#   ./scripts/build-local.sh
#   ./scripts/build-local.sh --skip-version-increment
#   ./scripts/deploy-local.sh

if [ "${1:-}" = "--help" ]; then
  cat <<'EOF'
Usage: ./scripts/build-local.sh [--skip-version-increment]

Builds the configured local preset and refreshes the configured local artifact.

Options:
  --skip-version-increment
      Use the build_version already present in the server config file.
EOF
  exit 0
fi

cd "$(dirname "$0")/.."
. ./scripts/lib/local-env.sh
. ./scripts/lib/version-env.sh

skip_build_version_increment=0

while [ "$#" -gt 0 ]; do
  case "$1" in
    --skip-version-increment|--no-version-increment)
      skip_build_version_increment=1
      ;;
    *)
      printf 'unknown option: %s\n' "$1" >&2
      printf 'usage: ./scripts/build-local.sh [--skip-version-increment]\n' >&2
      exit 1
      ;;
  esac
  shift
done

mp_build_server
if [ "$skip_build_version_increment" -eq 1 ]; then
  printf 'build version: %s\n' "$(mp_read_build_version_from_config "$MP_CONFIG_PATH")"
else
  printf 'build version: %s\n' "$(mp_increment_config_build_version "$MP_CONFIG_PATH")"
fi
./scripts/create-build-artifact.sh
