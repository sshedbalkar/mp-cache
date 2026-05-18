#!/usr/bin/env bash
set -euo pipefail

# Deploys mp-cache using the local Arch Linux or CachyOS host profile.
#
# Usage examples:
#   ./scripts/deploy-arch-cachyos.sh
#   MP_ALLOW_ENV_MISMATCH=1 ./scripts/deploy-arch-cachyos.sh

cd "$(dirname "$0")/.."
. ./scripts/lib/local-env.sh

mp_deploy_server "arch-cachyos"
