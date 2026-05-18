#!/usr/bin/env bash
set -euo pipefail

# Builds and runs the local-debug CTest unit suite.
#
# Usage examples:
#   ./scripts/test-unit.sh
#   ./scripts/test-unit.sh .tmp/test-reports

if [ "${1:-}" = "--help" ]; then
  cat <<'EOF'
Usage: ./scripts/test-unit.sh [report-dir]

Builds and runs the local-debug CTest unit suite.

Arguments:
  report-dir
      Optional report directory. Default: .tmp/test-reports.
EOF
  exit 0
fi

cd "$(dirname "$0")/.."

report_dir="${1:-.tmp/test-reports}"
mkdir -p "$report_dir"

ctest_report="$report_dir/ctest.txt"
summary_report="$report_dir/unit-test-report.md"

cmake --fresh --preset local-debug
cmake --build --preset local-debug
ctest --test-dir build/local-debug --output-on-failure >"$ctest_report"

{
  printf '# Unit Test Report\n\n'
  printf '| Field | Value |\n'
  printf '|:------|:------|\n'
  printf '| Command | `ctest --test-dir build/local-debug --output-on-failure` |\n'
  printf '| Status | PASS |\n\n'
  printf 'Detailed output: `%s`\n' "$ctest_report"
} > "$summary_report"
