#!/usr/bin/env bash
set -euo pipefail

# Verifies service logging stays on the native mp_logger backend.
#
# Usage examples:
#   ./scripts/check-logging.sh
#   ./scripts/check-logging.sh .tmp/test-reports

cd "$(dirname "$0")/.."

report_dir="${1:-.tmp/test-reports}"
mkdir -p "$report_dir"

report_path="$report_dir/logging-report.md"
service_pattern='\b(perror|puts|printf|vprintf|dprintf|vdprintf|syslog)\b|v?fprintf[[:space:]]*\([[:space:]]*(stderr|stdout)[[:space:]]*,'
allowed_runtime_pattern='config path is required|config file %s not found; using defaults and writing a bootstrap template|failed to write bootstrap template to %s|config load failed for %s with status %s|failed to prepare runtime directories|failed to initialize mp_logger'
violations=""

service_matches="$(
  rg -n --color never \
    --glob '!internal/config/config.c' \
    --glob '!internal/platform/fs.c' \
    --glob '!internal/runtime/runtime.c' \
    "$service_pattern" \
    internal || true
)"

if [ -n "$service_matches" ]; then
  violations="$service_matches"
fi

runtime_matches="$(rg -n --color never 'fprintf[[:space:]]*\([[:space:]]*stderr[[:space:]]*,' internal/runtime/runtime.c || true)"
if [ -n "$runtime_matches" ]; then
  unexpected_runtime="$(printf '%s\n' "$runtime_matches" | rg -v "$allowed_runtime_pattern" || true)"
  if [ -n "$unexpected_runtime" ]; then
    if [ -n "$violations" ]; then
      violations="${violations}"$'\n'"${unexpected_runtime}"
    else
      violations="$unexpected_runtime"
    fi
  fi
fi

if [ -n "$violations" ]; then
  {
    printf '# Logging Backend Report\n\n'
    printf '| Field | Value |\n'
    printf '|:------|:------|\n'
    printf '| Scope | `internal/` runtime and service code |\n'
    printf '| Required backend | `native/mp_logger` |\n'
    printf '| Status | FAIL |\n\n'
    printf 'Unexpected direct logging sites:\n\n'
    printf '```text\n%s\n```\n' "$violations"
  } > "$report_path"
  printf 'direct runtime/service logging outside mp_logger is not allowed:\n%s\n' "$violations" >&2
  exit 1
fi

{
  printf '# Logging Backend Report\n\n'
  printf '| Field | Value |\n'
  printf '|:------|:------|\n'
  printf '| Scope | `internal/` runtime and service code |\n'
  printf '| Required backend | `native/mp_logger` |\n'
  printf '| Status | PASS |\n'
} > "$report_path"

printf 'logging backend check passed\n'
