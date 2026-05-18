#!/usr/bin/env bash
set -euo pipefail

# Installs required Arch Linux or CachyOS packages for local mp-cache work.
#
# Usage examples:
#   ./scripts/install-arch-cachyos.sh --dry-run
#   ./scripts/install-arch-cachyos.sh --with-jq --with-valgrind

is_dry_run_requested=0
include_jq_package=0
include_valgrind_packages=0

while [ "$#" -gt 0 ]; do
  case "$1" in
    --dry-run)
      is_dry_run_requested=1
      ;;
    --with-jq)
      include_jq_package=1
      ;;
    --with-valgrind)
      include_valgrind_packages=1
      ;;
    --help)
      cat <<'EOF'
Usage: ./scripts/install-arch-cachyos.sh [--dry-run] [--with-jq] [--with-valgrind]

Options:
- --dry-run: print the pacman command without running it
- --with-jq: include jq for manual API and response inspection workflows
- --with-valgrind: include valgrind and debuginfod for host memory validation
EOF
      exit 0
      ;;
    *)
      printf 'error: unknown option: %s\n' "$1" >&2
      exit 1
      ;;
  esac
  shift
done

required_package_names=(
  base-devel
  cmake
  git
  curl
)

if [ "$include_jq_package" -eq 1 ]; then
  required_package_names+=(jq)
fi

if [ "$include_valgrind_packages" -eq 1 ]; then
  required_package_names+=(valgrind debuginfod)
fi

if [ "$is_dry_run_requested" -eq 1 ]; then
  printf 'pacman -S --needed %s\n' "${required_package_names[*]}"
  exit 0
fi

sudo pacman -S --needed "${required_package_names[@]}"
