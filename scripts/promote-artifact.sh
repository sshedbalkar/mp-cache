#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

artifact="${1:-}"
from_env="${2:-}"
to_env="${3:-}"

if [ -z "$artifact" ] || [ -z "$from_env" ] || [ -z "$to_env" ]; then
  printf 'usage: %s <artifact> <from-env> <to-env>\n' "$(basename "$0")" >&2
  exit 1
fi

[ -f "$artifact" ] || {
  printf 'artifact not found: %s\n' "$artifact" >&2
  exit 1
}

destination_dir="dist/promotions/$to_env"
mkdir -p "$destination_dir"
cp "$artifact" "$destination_dir/"

if [ -f "$artifact.sha256" ]; then
  cp "$artifact.sha256" "$destination_dir/"
fi

printf 'promoted %s from %s to %s\n' "$artifact" "$from_env" "$to_env"
