#!/usr/bin/env bash
set -euo pipefail

# Builds and runs the unit tests under the ASan/UBSan preset.
#
# Usage examples:
#   ./scripts/test-sanitizers.sh
#   ./scripts/test-sanitizers.sh .tmp/test-reports

if [ "${1:-}" = "--help" ]; then
  cat <<'EOF'
Usage: ./scripts/test-sanitizers.sh [report-dir]

Builds and runs the unit tests under the ASan/UBSan preset.

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

ctest_report="$report_dir/asan-ubsan-ctest.txt"
summary_report="$report_dir/sanitizers-report.md"

cmake --fresh --preset "$MP_SCRIPT_DEFAULT_SANITIZER_BUILD_PRESET"
cmake --build --preset "$MP_SCRIPT_DEFAULT_SANITIZER_BUILD_PRESET"
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir "$(mp_script_repo_path "$MP_SCRIPT_DEFAULT_SANITIZER_BUILD_DIR")" --output-on-failure >"$ctest_report"

{
  printf '# Sanitizers Report\n\n'
  printf '| Field | Value |\n'
  printf '|:------|:------|\n'
  printf '| Preset | `%s` |\n' "$MP_SCRIPT_DEFAULT_SANITIZER_BUILD_PRESET"
  printf '| Leak Check | disabled in sandboxed runs |\n'
  printf '| Status | PASS |\n\n'
  printf 'Detailed output: `%s`\n' "$ctest_report"
} > "$summary_report"
