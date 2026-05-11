#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

report_dir="${1:-.tmp/test-reports}"
minimum_score="${2:-85}"
mkdir -p "$report_dir"

checks=(
  README.md
  AGENTS.md
  Makefile
  CMakeLists.txt
  configs/bootstrap.ini
  configs/build/CMakePresets.json
  configs/logger.bootstrap.ini
  deploy/local/README.md
  deploy/qa/README.md
  deploy/staging/README.md
  deploy/production/README.md
  docs/architecture/mp-cache-architecture.md
  api/http/v1/cache-service.md
  docs/runbooks/local-service-lifecycle.md
  scripts/build-local.sh
  scripts/deploy-local.sh
  scripts/test-local.sh
  tests/unit/config_tests.c
  tests/unit/cache_tests.c
)

total="${#checks[@]}"
present=0
for path in "${checks[@]}"; do
  if [ -e "$path" ]; then
    present=$((present + 1))
  fi
done

score="$(awk -v p="$present" -v t="$total" 'BEGIN { printf "%.0f", (p * 100) / t }')"

{
  printf '# Standards Check Report\n\n'
  printf '| Field | Value |\n'
  printf '|:------|:------|\n'
  printf '| Checks present | %s/%s |\n' "$present" "$total"
  printf '| Score | %s |\n' "$score"
} > "$report_dir/standards-report.md"

if ! awk -v score="$score" -v minimum="$minimum_score" 'BEGIN { exit (score + 0 >= minimum + 0) ? 0 : 1 }'; then
  printf 'standards score %s is below required %s\n' "$score" "$minimum_score" >&2
  exit 1
fi

printf 'standards score %s meets required %s\n' "$score" "$minimum_score"
