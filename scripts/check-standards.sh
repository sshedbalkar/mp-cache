#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

report_dir="${1:-.tmp/test-reports}"
minimum_score="${2:-85}"
mkdir -p "$report_dir"

required_standard_paths=(
  README.md
  AGENTS.md
  Makefile
  CMakeLists.txt
  internal/config/constants.h
  docs/naming-strategy.md
  configs/bootstrap.ini
  configs/build/CMakePresets.json
  configs/logger.bootstrap.ini
  deploy/local/README.md
  deploy/qa/README.md
  deploy/staging/README.md
  deploy/production/README.md
  deploy/systemd/README.md
  deploy/systemd/mp-cache.service
  docs/architecture/mp-cache-architecture.md
  api/http/v1/cache-service.md
  docs/runbooks/local-service-lifecycle.md
  docs/runbooks/manual-api-testing-with-scripted-local-deploy.md
  scripts/benchmark-cache.sh
  scripts/build-local.sh
  scripts/check-logging.sh
  scripts/deploy-local.sh
  scripts/test-naming-strategy.sh
  scripts/test-hardening.sh
  scripts/test-local.sh
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

constant_drift_report="$(rg -n '^#define MP_CACHE_' internal cmd tests 2>/dev/null | grep -v 'internal/config/constants.h' | grep -v '#define MP_CACHE_INTERNAL_' || true)"
standards_score="$(awk -v p="$present_standard_path_count" -v t="$required_standard_path_count" 'BEGIN { printf "%.0f", (p * 100) / t }')"

{
  printf '# Standards Check Report\n\n'
  printf '| Field | Value |\n'
  printf '|:------|:------|\n'
  printf '| Checks present | %s/%s |\n' "$present_standard_path_count" "$required_standard_path_count"
  printf '| Score | %s |\n' "$standards_score"
  printf '| Constants source | %s |\n' "$([ -z "$constant_drift_report" ] && printf 'centralized' || printf 'drift detected')"
} > "$report_dir/standards-report.md"

if [ -n "$constant_drift_report" ]; then
  printf 'project constants must be defined in internal/config/constants.h:\n%s\n' "$constant_drift_report" >&2
  exit 1
fi

if ! awk -v score="$standards_score" -v minimum="$minimum_score" 'BEGIN { exit (score + 0 >= minimum + 0) ? 0 : 1 }'; then
  printf 'standards score %s is below required %s\n' "$standards_score" "$minimum_score" >&2
  exit 1
fi

printf 'standards score %s meets required %s\n' "$standards_score" "$minimum_score"
