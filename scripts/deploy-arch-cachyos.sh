#!/usr/bin/env bash
set -euo pipefail

# Deploys mp-cache using the local Arch Linux or CachyOS host profile.
#
# Usage examples:
#   ./scripts/deploy-arch-cachyos.sh
#   MP_ALLOW_ENV_MISMATCH=1 ./scripts/deploy-arch-cachyos.sh

if [ "${1:-}" = "--help" ]; then
  cat <<'EOF'
Usage: ./scripts/deploy-arch-cachyos.sh

Deploys mp-cache using the local Arch Linux or CachyOS host profile.

Environment:
  MP_ALLOW_ENV_MISMATCH
      Set to 1 to bypass host profile detection.
EOF
  exit 0
fi

cd "$(dirname "$0")/.."
. ./scripts/lib/local-env.sh

mp_deploy_server "arch-cachyos"
