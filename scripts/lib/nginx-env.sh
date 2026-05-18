#!/usr/bin/env bash
set -euo pipefail

# Loads shared Nginx path constants for local and remote deployment scripts.
#
# Usage examples:
#   . ./scripts/lib/nginx-env.sh
#   MP_NGINX_PATH_CONFIG_FILE=configs/deploy/nginx-paths.env . ./scripts/lib/nginx-env.sh

if [ "${BASH_SOURCE[0]}" = "$0" ] && [ "${1:-}" = "--help" ]; then
  cat <<'EOF'
Usage: . ./scripts/lib/nginx-env.sh

Loads shared Nginx path constants for local and remote deployment scripts.

Environment:
  MP_NGINX_PATH_CONFIG_FILE
      Optional Nginx path constants file override.
EOF
  exit 0
fi

mp_nginx_detect_repo_root() {
  local nginx_env_script_dir
  nginx_env_script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
  cd "$nginx_env_script_dir/../.." && pwd
}

MP_REPO_ROOT="${MP_REPO_ROOT:-$(mp_nginx_detect_repo_root)}"
MP_NGINX_PATH_CONFIG_FILE="${MP_NGINX_PATH_CONFIG_FILE:-$MP_REPO_ROOT/configs/deploy/nginx-paths.env}"

[ -f "$MP_NGINX_PATH_CONFIG_FILE" ] || {
  printf 'error: missing Nginx path config: %s\n' "$MP_NGINX_PATH_CONFIG_FILE" >&2
  exit 1
}

# shellcheck disable=SC1090
. "$MP_NGINX_PATH_CONFIG_FILE"
