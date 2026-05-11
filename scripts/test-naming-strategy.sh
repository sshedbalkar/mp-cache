#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

report_dir="${1:-.tmp/test-reports}"
mkdir -p "$report_dir"

summary_report="$report_dir/naming-strategy-report.md"
violations_report="$report_dir/naming-strategy-violations.txt"
cmd_violation_output=""
script_violation_output=""

capture_violations() {
  local pattern="$1"
  shift

  rg -n --color never "$pattern" "$@" || true
}

cmd_violation_output="$(
  capture_violations \
    'FILE \*stream\b|const char \*(path|body|token|command)\b|char (path|body)\[[^]]*\]|int index\b' \
    cmd/mp-cache-server/main.c \
    cmd/mp-cachectl/main.c
)"

script_violation_output="$(
  capture_violations \
    '^[[:space:]]*(local[[:space:]]+)?(path|checks|total|present|score|started_here|dry_run|packages|artifact|from_env|to_env|expected|detected|pid|attempts)=|^(mp_repo_root|mp_die|mp_assert_env|mp_test_health|mp_doctor)\(\)' \
    scripts/*.sh \
    scripts/lib/local-env.sh
)"

printf '%s\n%s\n' "$cmd_violation_output" "$script_violation_output" | sed '/^$/d' >"$violations_report"

if [ -s "$violations_report" ]; then
  {
    printf '# Naming Strategy Report\n\n'
    printf '| Field | Value |\n'
    printf '|:------|:------|\n'
    printf '| Rule | `docs/naming-strategy.md` |\n'
    printf '| Scope | `cmd/` and `scripts/` |\n'
    printf '| Status | FAIL |\n\n'
    printf 'Low-signal standalone names found:\n\n'
    printf '```text\n'
    cat "$violations_report"
    printf '```\n'
  } > "$summary_report"
  printf 'naming strategy check failed:\n' >&2
  cat "$violations_report" >&2
  exit 1
fi

{
  printf '# Naming Strategy Report\n\n'
  printf '| Field | Value |\n'
  printf '|:------|:------|\n'
  printf '| Rule | `docs/naming-strategy.md` |\n'
  printf '| Scope | `cmd/` and `scripts/` |\n'
  printf '| Status | PASS |\n'
} > "$summary_report"

rm -f "$violations_report"
printf 'naming strategy check passed\n'
