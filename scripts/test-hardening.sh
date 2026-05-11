#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

report_dir="${1:-.tmp/test-reports}"
mkdir -p "$report_dir"

./scripts/test-sanitizers.sh "$report_dir"
./scripts/test-valgrind.sh "$report_dir"
