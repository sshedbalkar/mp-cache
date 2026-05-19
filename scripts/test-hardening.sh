#!/usr/bin/env bash
set -euo pipefail

# Runs hardening-oriented checks and writes validation reports.
#
# Usage examples:
#   ./scripts/test-hardening.sh
#   ./scripts/test-hardening.sh .tmp/test-reports

if [ "${1:-}" = "--help" ]; then
  cat <<'EOF'
Usage: ./scripts/test-hardening.sh [report-dir]

Runs hardening-oriented checks and writes validation reports.

Arguments:
  report-dir
      Optional report directory. Default: MP_SCRIPT_DEFAULT_TEST_REPORT_DIR from configs/scripts/defaults.env.
EOF
  exit 0
fi

cd "$(dirname "$0")/.."
. ./scripts/lib/script-config-env.sh

report_dir="${1:-$(mp_script_repo_path "$MP_SCRIPT_DEFAULT_TEST_REPORT_DIR")}"
mkdir -p "$report_dir"

./scripts/check-logging.sh "$report_dir"
./scripts/test-sanitizers.sh "$report_dir"
