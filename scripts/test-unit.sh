#!/usr/bin/env bash
set -euo pipefail

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
