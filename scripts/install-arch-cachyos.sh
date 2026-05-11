#!/usr/bin/env bash
set -euo pipefail

dry_run=0
if [ "${1:-}" = "--dry-run" ]; then
  dry_run=1
fi

packages=(
  base-devel
  cmake
  git
  curl
)

if [ "$dry_run" -eq 1 ]; then
  printf 'pacman -S --needed %s\n' "${packages[*]}"
  exit 0
fi

sudo pacman -S --needed "${packages[@]}"
