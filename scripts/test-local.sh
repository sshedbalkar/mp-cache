#!/usr/bin/env bash
set -euo pipefail

# Runs the default local validation suite plus a live health smoke test.
#
# Usage examples:
#   ./scripts/test-local.sh
#   MP_AUTO_RESET_LOCAL_STATE_ON_LOAD_FAILURE=0 ./scripts/test-local.sh

if [ "${1:-}" = "--help" ]; then
  cat <<'EOF'
Usage: ./scripts/test-local.sh

Runs the default local validation suite plus a live health smoke test.

Environment:
  MP_AUTO_RESET_LOCAL_STATE_ON_LOAD_FAILURE
      Set to 0 to fail instead of rotating incompatible local state.
EOF
  exit 0
fi

cd "$(dirname "$0")/.."
. ./scripts/lib/local-env.sh

server_started_by_script=0
local_report_dir="$(mp_script_repo_path "$MP_SCRIPT_DEFAULT_TEST_REPORT_DIR")"

mp_build_server
./scripts/test-unit.sh "$local_report_dir"
./scripts/test-naming-strategy.sh "$local_report_dir"
./scripts/check-standards.sh "$local_report_dir" "$MP_SCRIPT_DEFAULT_STANDARDS_MINIMUM_SCORE"
./scripts/test-hardening.sh "$local_report_dir"
./scripts/check-context.sh

if ! mp_is_server_running; then
  mp_start_server
  server_started_by_script=1
fi

mp_test_health_endpoint

if [ "$server_started_by_script" -eq 1 ]; then
  mp_stop_server
fi
