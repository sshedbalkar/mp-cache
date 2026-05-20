#!/usr/bin/env bash
set -euo pipefail

# Verifies durable standards files, centralized constants, and script headers.
#
# Usage examples:
#   ./scripts/check-standards.sh
#   ./scripts/check-standards.sh .tmp/test-reports 90

if [ "${1:-}" = "--help" ]; then
  cat <<'EOF'
Usage: ./scripts/check-standards.sh [report-dir] [minimum-score]

Verifies durable standards files, centralized constants, and script help headers.

Arguments:
  report-dir
      Optional report directory. Default: MP_SCRIPT_DEFAULT_TEST_REPORT_DIR from configs/scripts/defaults.env.
  minimum-score
      Optional required standards score. Default: 85.
EOF
  exit 0
fi

cd "$(dirname "$0")/.."
. ./scripts/lib/script-config-env.sh

report_dir="${1:-$(mp_script_repo_path "$MP_SCRIPT_DEFAULT_TEST_REPORT_DIR")}"
minimum_score="${2:-$MP_SCRIPT_DEFAULT_STANDARDS_MINIMUM_SCORE}"
mkdir -p "$report_dir"

required_standard_paths=(
  README.md
  AGENTS.md
  Makefile
  CMakeLists.txt
  internal/config/constants.h
  docs/naming-strategy.md
  configs/bootstrap.yaml
  configs/build/CMakePresets.json
  configs/deploy/nginx-paths.env
  configs/scripts/defaults.env
  native/mp_logger/configs/logger.bootstrap.ini
  native/mp_logger/configs/scripts/defaults.env
  deploy/development/README.md
  deploy/local/README.md
  deploy/nginx/mp-cache.conf
  deploy/qa/README.md
  deploy/staging/README.md
  deploy/production/README.md
  deploy/systemd/README.md
  deploy/systemd/mp-cache.service
  docs/architecture/mp-cache-architecture.md
  api/http/v1/cache-service.md
  docs/runbooks/deployment.md
  docs/runbooks/local-service-lifecycle.md
  docs/runbooks/manual-api-testing-with-scripted-local-deploy.md
  scripts/benchmark-cache.sh
  scripts/build-local.sh
  scripts/check-logging.sh
  scripts/create-build-artifact.sh
  scripts/deploy-development.sh
  scripts/deploy-local.sh
  scripts/deploy-production.sh
  scripts/deploy-qa.sh
  scripts/deploy-staging.sh
  scripts/restart-development.sh
  scripts/restart-production.sh
  scripts/restart-qa.sh
  scripts/restart-staging.sh
  scripts/lib/script-config-env.sh
  scripts/lib/version-env.sh
  scripts/lib/remote-deploy-env.sh
  scripts/lib/nginx-env.sh
  scripts/stop-development.sh
  scripts/stop-production.sh
  scripts/stop-qa.sh
  scripts/stop-staging.sh
  scripts/test-naming-strategy.sh
  scripts/test-hardening.sh
  scripts/test-local.sh
  scripts/test-local-deployment.sh
  scripts/test-sanitizers.sh
  scripts/test-valgrind.sh
  scripts/write-local-secret-env.sh
  tests/unit/config_tests.c
  tests/unit/cache_tests.c
  tests/unit/security_tests.c
  tests/unit/storage_tests.c
  tests/unit/http_tests.c
  tests/bench/cache_benchmark.c
)

required_standard_path_count="${#required_standard_paths[@]}"
present_standard_path_count=0
for required_path in "${required_standard_paths[@]}"; do
  if [ -e "$required_path" ]; then
    present_standard_path_count=$((present_standard_path_count + 1))
  fi
done

if [ -e configs/logger.bootstrap.ini ]; then
  printf 'mp_logger bootstrap config must live in native/mp_logger/configs/logger.bootstrap.ini, not configs/logger.bootstrap.ini\n' >&2
  exit 1
fi

constant_drift_report="$(rg -n '^#define MP_CACHE_' internal cmd tests 2>/dev/null | grep -v 'internal/config/constants.h' | grep -v '#define MP_CACHE_INTERNAL_' || true)"
error_code_pattern="$(awk '
  /^#define MP_CACHE_HTTP_ERROR_CODE_/ {
    value = $3
    gsub(/"/, "", value)
    printf "%s%s", separator, value
    separator = "|"
  }
' internal/config/constants.h)"
error_code_drift_report=""
if [ -n "$error_code_pattern" ]; then
  error_code_drift_report="$(rg -n "\"(${error_code_pattern})\"" internal cmd tests 2>/dev/null | grep -v 'internal/config/constants.h' || true)"
fi
endpoint_pattern="$(awk '
  /^#define MP_CACHE_HTTP_ROUTE_/ {
    value = $3
    if (value !~ /^"\// || value ~ /%/ || value == "\"/\"") {
      next
    }
    gsub(/"/, "", value)
    gsub(/[][(){}.^$*+?|\\]/, "\\\\&", value)
    printf "%s%s", separator, value
    separator = "|"
  }
' internal/config/constants.h)"
endpoint_drift_report=""
if [ -n "$endpoint_pattern" ]; then
  endpoint_drift_report="$(rg -n "\"[^\"[:space:]]*(${endpoint_pattern})" internal cmd tests scripts 2>/dev/null | grep -v 'internal/config/constants.h' || true)"
fi
script_config_literal_report="$(
  rg -n --color never '\$\{[A-Z0-9_]+:-(\.tmp|build/local-debug|build/local-asan-ubsan|dist|dist/local|configs/bootstrap\.yaml|configs/deploy/nginx-paths\.env|127\.0\.0\.1:8080|/opt/mp-cache|/var/lib|/var/log|/run|/etc/systemd|/tmp|mp-cache-local|mp-cache|local|1970-01-01T00:00:00Z|85|1)([^}]*)\}' scripts 2>/dev/null || true
)"
script_header_usage_examples_report="$(
  for standard_script_file in scripts/*.sh scripts/lib/*.sh; do
    [ -f "$standard_script_file" ] || continue
    awk 'NR <= 30 && /^# Usage examples:/ { found = 1 } END { exit(found ? 0 : 1) }' "$standard_script_file" ||
      printf '%s\n' "$standard_script_file"
  done
)"
script_help_usage_report="$(
  for standard_script_file in scripts/*.sh scripts/lib/*.sh; do
    [ -f "$standard_script_file" ] || continue
    script_help_text="$(bash "$standard_script_file" --help 2>&1)" || {
      printf '%s: --help exited nonzero\n' "$standard_script_file"
      continue
    }
    printf '%s\n' "$script_help_text" | awk '/^Usage:/ { found = 1 } END { exit(found ? 0 : 1) }' ||
      printf '%s: --help did not print Usage:\n' "$standard_script_file"
  done
)"
standards_score="$(awk -v p="$present_standard_path_count" -v t="$required_standard_path_count" 'BEGIN { printf "%.0f", (p * 100) / t }')"

{
  printf '# Standards Check Report\n\n'
  printf '| Field | Value |\n'
  printf '|:------|:------|\n'
  printf '| Checks present | %s/%s |\n' "$present_standard_path_count" "$required_standard_path_count"
  printf '| Score | %s |\n' "$standards_score"
  printf '| Constants source | %s |\n' "$([ -z "$constant_drift_report" ] && [ -z "$error_code_drift_report" ] && [ -z "$endpoint_drift_report" ] && printf 'centralized' || printf 'drift detected')"
  printf '| Script usage headers | %s |\n' "$([ -z "$script_header_usage_examples_report" ] && printf 'present' || printf 'missing')"
  printf '| Script --help usage | %s |\n' "$([ -z "$script_help_usage_report" ] && printf 'present' || printf 'missing')"
  printf '| Script config defaults | %s |\n' "$([ -z "$script_config_literal_report" ] && printf 'centralized' || printf 'hardcoded fallback detected')"
} > "$report_dir/standards-report.md"

if [ -n "$constant_drift_report" ]; then
  printf 'project constants must be defined in internal/config/constants.h:\n%s\n' "$constant_drift_report" >&2
  exit 1
fi
if [ -n "$error_code_drift_report" ]; then
  printf 'project HTTP error_code literals must use internal/config/constants.h macros:\n%s\n' "$error_code_drift_report" >&2
  exit 1
fi
if [ -n "$endpoint_drift_report" ]; then
  printf 'project HTTP endpoint literals must use internal/config/constants.h macros:\n%s\n' "$endpoint_drift_report" >&2
  exit 1
fi
if [ -n "$script_header_usage_examples_report" ]; then
  printf 'scripts must include top-level Usage examples comments:\n%s\n' "$script_header_usage_examples_report" >&2
  exit 1
fi
if [ -n "$script_help_usage_report" ]; then
  printf 'scripts must print usage successfully for --help:\n%s\n' "$script_help_usage_report" >&2
  exit 1
fi
if [ -n "$script_config_literal_report" ]; then
  printf 'script fallback defaults must be centralized in configs/scripts/defaults.env:\n%s\n' "$script_config_literal_report" >&2
  exit 1
fi

if ! awk -v score="$standards_score" -v minimum="$minimum_score" 'BEGIN { exit (score + 0 >= minimum + 0) ? 0 : 1 }'; then
  printf 'standards score %s is below required %s\n' "$standards_score" "$minimum_score" >&2
  exit 1
fi

printf 'standards score %s meets required %s\n' "$standards_score" "$minimum_score"
