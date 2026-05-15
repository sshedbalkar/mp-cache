#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
. ./scripts/lib/local-env.sh

is_force_requested=0

while [ "$#" -gt 0 ]; do
  case "$1" in
    --force)
      is_force_requested=1
      ;;
    --help)
      cat <<'EOF'
Usage: ./scripts/write-local-secret-env.sh [--force]

Writes local development secrets to MP_LOCAL_SECRET_ENV_FILE.

Behavior:
- reuses an existing file when both required values are already present
- overwrites placeholder or incomplete values automatically
- forces fresh values when --force is provided

Environment:
- MP_LOCAL_SECRET_ENV_FILE: optional output path override
EOF
      exit 0
      ;;
    *)
      mp_exit_with_error "unknown option: $1"
      ;;
  esac
  shift
done

local_secret_env_value_is_usable() {
  local secret_name="$1"

  [ -f "$MP_LOCAL_SECRET_ENV_FILE" ] || return 1

  awk -F= -v secret_name="$secret_name" '
    $1 == secret_name && $2 != "" && $2 != "replace-me" {
      found = 1
    }
    END {
      exit(found ? 0 : 1)
    }' "$MP_LOCAL_SECRET_ENV_FILE"
}

generate_local_secret_suffix() {
  local byte_count="$1"
  od -An -N "$byte_count" -tx1 /dev/urandom | tr -d ' \n'
}

mkdir -p "$(dirname "$MP_LOCAL_SECRET_ENV_FILE")"

if [ "$is_force_requested" -ne 1 ] &&
  local_secret_env_value_is_usable "MP_SECRET_LOCAL_BOOTSTRAP_ADMIN_TOKEN" &&
  local_secret_env_value_is_usable "MP_SECRET_LOCAL_STORAGE_KEY"; then
  printf 'using existing local secret env file: %s\n' "$MP_LOCAL_SECRET_ENV_FILE"
  exit 0
fi

bootstrap_admin_token="local-admin-$(date +%s)-$(generate_local_secret_suffix 8)"
storage_key="local-storage-$(date +%s)-$(generate_local_secret_suffix 16)"

cat > "$MP_LOCAL_SECRET_ENV_FILE" <<EOF
MP_SECRET_LOCAL_BOOTSTRAP_ADMIN_TOKEN=${bootstrap_admin_token}
MP_SECRET_LOCAL_STORAGE_KEY=${storage_key}
EOF

chmod 600 "$MP_LOCAL_SECRET_ENV_FILE"
printf 'wrote local secret env file: %s\n' "$MP_LOCAL_SECRET_ENV_FILE"
