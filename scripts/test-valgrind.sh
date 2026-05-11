#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

report_dir="${1:-.tmp/test-reports}"
mkdir -p "$report_dir"

summary_report="$report_dir/valgrind-report.md"
detail_report="$report_dir/valgrind.txt"

if ! command -v valgrind >/dev/null 2>&1; then
  {
    printf '# Valgrind Report\n\n'
    printf '| Field | Value |\n'
    printf '|:------|:------|\n'
    printf '| Status | SKIPPED |\n'
    printf '| Reason | `valgrind` not installed |\n'
  } > "$summary_report"
  exit 0
fi

cmake --fresh --preset local-debug
cmake --build --preset local-debug

valgrind --error-exitcode=1 --leak-check=full ./build/local-debug/mp_cache_store_tests >"$detail_report" 2>&1
valgrind --error-exitcode=1 --leak-check=full ./build/local-debug/mp_cache_security_tests >>"$detail_report" 2>&1
valgrind --error-exitcode=1 --leak-check=full ./build/local-debug/mp_cache_storage_tests >>"$detail_report" 2>&1
valgrind --error-exitcode=1 --leak-check=full ./build/local-debug/mp_cache_http_tests >>"$detail_report" 2>&1

{
  printf '# Valgrind Report\n\n'
  printf '| Field | Value |\n'
  printf '|:------|:------|\n'
  printf '| Status | PASS |\n\n'
  printf 'Detailed output: `%s`\n' "$detail_report"
} > "$summary_report"
