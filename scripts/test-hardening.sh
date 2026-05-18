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
      Optional report directory. Default: .tmp/test-reports.
EOF
  exit 0
fi

cd "$(dirname "$0")/.."

report_dir="${1:-.tmp/test-reports}"
mkdir -p "$report_dir"

./scripts/check-logging.sh "$report_dir"
./scripts/test-sanitizers.sh "$report_dir"
