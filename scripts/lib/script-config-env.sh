#!/usr/bin/env bash
set -euo pipefail

# Loads centralized shell-script defaults and path helpers.
#
# Usage examples:
#   . ./scripts/lib/script-config-env.sh
#   MP_SCRIPT_CONFIG_FILE=configs/scripts/defaults.env . ./scripts/lib/script-config-env.sh

if [ "${BASH_SOURCE[0]}" = "$0" ] && [ "${1:-}" = "--help" ]; then
  cat <<'EOF'
Usage: . ./scripts/lib/script-config-env.sh

Loads centralized shell-script defaults and path helpers.

Environment:
  MP_SCRIPT_CONFIG_FILE
      Optional script defaults file override. Default: configs/scripts/defaults.env.
EOF
  exit 0
fi

mp_script_config_detect_repo_root() {
  local script_config_dir
  script_config_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
  cd "$script_config_dir/../.." && pwd
}

MP_REPO_ROOT="${MP_REPO_ROOT:-$(mp_script_config_detect_repo_root)}"
MP_SCRIPT_CONFIG_FILE="${MP_SCRIPT_CONFIG_FILE:-$MP_REPO_ROOT/configs/scripts/defaults.env}"

[ -f "$MP_SCRIPT_CONFIG_FILE" ] || {
  printf 'error: missing script defaults config: %s\n' "$MP_SCRIPT_CONFIG_FILE" >&2
  exit 1
}

# shellcheck disable=SC1090
. "$MP_SCRIPT_CONFIG_FILE"

mp_script_repo_path() {
  local configured_path="$1"

  case "$configured_path" in
    /*)
      printf '%s\n' "$configured_path"
      ;;
    *)
      printf '%s/%s\n' "$MP_REPO_ROOT" "$configured_path"
      ;;
  esac
}

mp_script_join_path() {
  local parent_path="$1"
  local child_path="$2"

  case "$parent_path" in
    */)
      printf '%s%s\n' "$parent_path" "$child_path"
      ;;
    *)
      printf '%s/%s\n' "$parent_path" "$child_path"
      ;;
  esac
}
