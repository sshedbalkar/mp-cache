#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

artifact_path="${1:-}"
source_env_id="${2:-}"
target_env_id="${3:-}"

if [ -z "$artifact_path" ] || [ -z "$source_env_id" ] || [ -z "$target_env_id" ]; then
  printf 'usage: %s <artifact> <from-env> <to-env>\n' "$(basename "$0")" >&2
  exit 1
fi

[ -f "$artifact_path" ] || {
  printf 'artifact not found: %s\n' "$artifact_path" >&2
  exit 1
}

promotion_destination_dir="dist/promotions/$target_env_id"
mkdir -p "$promotion_destination_dir"
cp "$artifact_path" "$promotion_destination_dir/"

if [ -f "$artifact_path.sha256" ]; then
  cp "$artifact_path.sha256" "$promotion_destination_dir/"
fi

printf 'promoted %s from %s to %s\n' "$artifact_path" "$source_env_id" "$target_env_id"
