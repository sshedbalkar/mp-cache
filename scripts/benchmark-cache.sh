#!/usr/bin/env bash
set -euo pipefail

# Runs the cache benchmark and writes detailed and summary reports.
#
# Usage examples:
#   ./scripts/benchmark-cache.sh
#   ./scripts/benchmark-cache.sh .tmp/test-reports

cd "$(dirname "$0")/.."

report_dir="${1:-.tmp/test-reports}"
mkdir -p "$report_dir"

detail_report="$report_dir/cache-benchmark.txt"
summary_report="$report_dir/cache-benchmark-report.md"

cmake --fresh --preset local-debug
cmake --build --preset local-debug
./build/local-debug/mp_cache_benchmark >"$detail_report"

{
  printf '# Cache Benchmark Report\n\n'
  printf '| Field | Value |\n'
  printf '|:------|:------|\n'
  printf '| Binary | `mp_cache_benchmark` |\n'
  printf '| Output | `%s` |\n' "$(cat "$detail_report")"
} > "$summary_report"
