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
      Optional report directory. Default: MP_SCRIPT_DEFAULT_TEST_REPORT_DIR from configs/scripts/defaults.env.
EOF
  exit 0
fi

cd "$(dirname "$0")/.."
. ./scripts/lib/script-config-env.sh

report_dir="${1:-$(mp_script_repo_path "$MP_SCRIPT_DEFAULT_TEST_REPORT_DIR")}"
mkdir -p "$report_dir"

ctest_report="$report_dir/ctest.txt"
summary_report="$report_dir/unit-test-report.md"

cmake --fresh --preset "$MP_SCRIPT_DEFAULT_LOCAL_BUILD_PRESET"
cmake --build --preset "$MP_SCRIPT_DEFAULT_LOCAL_BUILD_PRESET"
ctest --test-dir "$(mp_script_repo_path "$MP_SCRIPT_DEFAULT_LOCAL_BUILD_DIR")" --output-on-failure >"$ctest_report"

{
  printf '# Unit Test Report\n\n'
  printf '| Field | Value |\n'
  printf '|:------|:------|\n'
  printf '| Command | `ctest --test-dir %s --output-on-failure` |\n' "$MP_SCRIPT_DEFAULT_LOCAL_BUILD_DIR"
  printf '| Status | PASS |\n\n'
  printf 'Detailed output: `%s`\n' "$ctest_report"
} > "$summary_report"
