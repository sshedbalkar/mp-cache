#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
. ./scripts/lib/local-env.sh

started_here=0

mp_build_server "arch-cachyos"
./scripts/test-unit.sh .tmp/test-reports
./scripts/check-standards.sh .tmp/test-reports 85
./scripts/check-context.sh

if ! mp_is_server_running; then
  mp_start_server
  started_here=1
fi

mp_test_health

if [ "$started_here" -eq 1 ]; then
  mp_stop_server
fi
