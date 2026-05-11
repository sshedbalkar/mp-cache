#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
. ./scripts/lib/local-env.sh

mp_assert_target_env_id "arch-cachyos"
./scripts/test-local.sh
