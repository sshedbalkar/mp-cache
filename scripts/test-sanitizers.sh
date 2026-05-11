#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

report_dir="${1:-.tmp/test-reports}"
mkdir -p "$report_dir"

ctest_report="$report_dir/asan-ubsan-ctest.txt"
summary_report="$report_dir/sanitizers-report.md"

cmake --fresh --preset local-asan-ubsan
cmake --build --preset local-asan-ubsan
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build/local-asan-ubsan --output-on-failure >"$ctest_report"

{
  printf '# Sanitizers Report\n\n'
  printf '| Field | Value |\n'
  printf '|:------|:------|\n'
  printf '| Preset | `local-asan-ubsan` |\n'
  printf '| Leak Check | disabled in sandboxed runs |\n'
  printf '| Status | PASS |\n\n'
  printf 'Detailed output: `%s`\n' "$ctest_report"
} > "$summary_report"
