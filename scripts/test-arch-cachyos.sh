#!/usr/bin/env bash
set -euo pipefail

# Runs the local validation suite for an Arch Linux or CachyOS host.
#
# Usage examples:
#   ./scripts/test-arch-cachyos.sh
#   MP_ALLOW_ENV_MISMATCH=1 ./scripts/test-arch-cachyos.sh

if [ "${1:-}" = "--help" ]; then
  cat <<'EOF'
Usage: ./scripts/test-arch-cachyos.sh

Runs the local validation suite for an Arch Linux or CachyOS host.

Environment:
  MP_ALLOW_ENV_MISMATCH
      Set to 1 to bypass host profile detection.
EOF
  exit 0
fi

cd "$(dirname "$0")/.."
. ./scripts/lib/local-env.sh

mp_assert_target_env_id "arch-cachyos"
./scripts/test-local.sh
