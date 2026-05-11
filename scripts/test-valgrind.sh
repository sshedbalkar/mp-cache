#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

report_dir="${1:-.tmp/test-reports}"
mkdir -p "$report_dir"

summary_report="$report_dir/valgrind-report.md"
detail_report="$report_dir/valgrind.txt"

rm -f "$summary_report" "$detail_report"

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

run_valgrind_for_test_binary() {
  local test_binary="$1"

  valgrind --error-exitcode=1 --leak-check=full "$test_binary" >>"$detail_report" 2>&1
}

if ! run_valgrind_for_test_binary ./build/local-debug/mp_cache_store_tests ||
   ! run_valgrind_for_test_binary ./build/local-debug/mp_cache_security_tests ||
   ! run_valgrind_for_test_binary ./build/local-debug/mp_cache_storage_tests ||
   ! run_valgrind_for_test_binary ./build/local-debug/mp_cache_http_tests; then
  if rg -q "Fatal error at startup|install glibc's debuginfo|Cannot continue -- exiting now" "$detail_report"; then
    {
      printf '# Valgrind Report\n\n'
      printf '| Field | Value |\n'
      printf '|:------|:------|\n'
      printf '| Status | SKIPPED |\n'
      printf '| Reason | Valgrind is installed but unusable on this host; glibc debuginfo or an unstripped dynamic loader is required |\n\n'
      printf 'Detailed output: `%s`\n' "$detail_report"
    } > "$summary_report"
    exit 0
  fi

  {
    printf '# Valgrind Report\n\n'
    printf '| Field | Value |\n'
    printf '|:------|:------|\n'
    printf '| Status | FAIL |\n\n'
    printf 'Detailed output: `%s`\n' "$detail_report"
  } > "$summary_report"
  exit 1
fi

{
  printf '# Valgrind Report\n\n'
  printf '| Field | Value |\n'
  printf '|:------|:------|\n'
  printf '| Status | PASS |\n\n'
  printf 'Detailed output: `%s`\n' "$detail_report"
} > "$summary_report"
