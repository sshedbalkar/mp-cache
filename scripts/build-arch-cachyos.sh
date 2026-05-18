#!/usr/bin/env bash
set -euo pipefail

# Builds mp-cache for the local Arch Linux or CachyOS host profile.
#
# Usage examples:
#   ./scripts/build-arch-cachyos.sh
#   MP_ALLOW_ENV_MISMATCH=1 ./scripts/build-arch-cachyos.sh

if [ "${1:-}" = "--help" ]; then
  cat <<'EOF'
Usage: ./scripts/build-arch-cachyos.sh

Builds mp-cache for the local Arch Linux or CachyOS host profile.

Environment:
  MP_ALLOW_ENV_MISMATCH
      Set to 1 to bypass host profile detection.
EOF
  exit 0
fi

cd "$(dirname "$0")/.."
. ./scripts/lib/local-env.sh

mp_build_server "arch-cachyos"
