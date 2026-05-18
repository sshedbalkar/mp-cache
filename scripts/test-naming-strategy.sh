#!/usr/bin/env bash
set -euo pipefail

# Verifies naming-strategy rules for cmd, scripts, and internal code.
#
# Usage examples:
#   ./scripts/test-naming-strategy.sh
#   ./scripts/test-naming-strategy.sh .tmp/test-reports

if [ "${1:-}" = "--help" ]; then
  cat <<'EOF'
Usage: ./scripts/test-naming-strategy.sh [report-dir]

Verifies naming-strategy rules for cmd, scripts, and internal code.

Arguments:
  report-dir
      Optional report directory. Default: .tmp/test-reports.
EOF
  exit 0
fi

cd "$(dirname "$0")/.."

report_dir="${1:-.tmp/test-reports}"
mkdir -p "$report_dir"

summary_report="$report_dir/naming-strategy-report.md"
violations_report="$report_dir/naming-strategy-violations.txt"
cmd_violation_output=""
script_violation_output=""
internal_violation_output=""

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

internal_violation_output="$(
  capture_violations \
    'char (target|path|query|authorization)\[[^]]*\];|char \*body;|const char \*(path|body|token|text)\b|request->(path|body|query|target|authorization)\b|response->body\b|export_result\.path\b|out_result->path\b|char \*out_(token|body|text)\b|int \*out_status_code\b|uint8_t \*\*out_value\b|size_t \*out_value_length\b|uint8_t key\[MP_CACHE_TOKEN_HASH_SIZE\]' \
    internal/cache/cache.c \
    internal/cache/cache.h \
    internal/config/config.c \
    internal/config/config.h \
    internal/crypto/crypto.c \
    internal/crypto/crypto.h \
    internal/httpserver/http_server.c \
    internal/httpserver/http_server.h \
    internal/observability/log.c \
    internal/observability/log.h \
    internal/platform/fs.c \
    internal/platform/fs.h \
    internal/runtime/runtime.c \
    internal/runtime/runtime.h \
    internal/security/security.c \
    internal/security/security.h \
    internal/storage/storage.c \
    internal/storage/storage.h
)"

printf '%s\n%s\n%s\n' "$cmd_violation_output" "$script_violation_output" "$internal_violation_output" | sed '/^$/d' >"$violations_report"

if [ -s "$violations_report" ]; then
  {
    printf '# Naming Strategy Report\n\n'
    printf '| Field | Value |\n'
    printf '|:------|:------|\n'
    printf '| Rule | `docs/naming-strategy.md` |\n'
    printf '| Scope | `cmd/`, `scripts/`, and `internal/` |\n'
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
  printf '| Scope | `cmd/`, `scripts/`, and `internal/` |\n'
  printf '| Status | PASS |\n'
} > "$summary_report"

rm -f "$violations_report"
printf 'naming strategy check passed\n'
