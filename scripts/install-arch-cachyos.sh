#!/usr/bin/env bash
set -euo pipefail

is_dry_run_requested=0
if [ "${1:-}" = "--dry-run" ]; then
  is_dry_run_requested=1
fi

required_package_names=(
  base-devel
  cmake
  git
  curl
)

if [ "$is_dry_run_requested" -eq 1 ]; then
  printf 'pacman -S --needed %s\n' "${required_package_names[*]}"
  exit 0
fi

sudo pacman -S --needed "${required_package_names[@]}"
