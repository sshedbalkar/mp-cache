#!/usr/bin/env bash
set -euo pipefail

# Runs the local validation suite for an Arch Linux or CachyOS host.
#
# Usage examples:
#   ./scripts/test-arch-cachyos.sh
#   MP_ALLOW_ENV_MISMATCH=1 ./scripts/test-arch-cachyos.sh

cd "$(dirname "$0")/.."
. ./scripts/lib/local-env.sh

mp_assert_target_env_id "arch-cachyos"
./scripts/test-local.sh
