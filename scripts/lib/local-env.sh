#!/usr/bin/env bash
set -euo pipefail

mp_detect_repo_root() {
  local repo_root_script_dir
  repo_root_script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
  cd "$repo_root_script_dir/../.." && pwd
}

MP_REPO_ROOT="${MP_REPO_ROOT:-$(mp_detect_repo_root)}"
MP_CONFIG_PATH="${MP_CONFIG_PATH:-$MP_REPO_ROOT/configs/bootstrap.ini}"
MP_SOCKET_PATH="${MP_SOCKET_PATH:-/tmp/mp-cache/run/mp-cache.sock}"
MP_PID_FILE="${MP_PID_FILE:-/tmp/mp-cache/run/mp-cache.pid}"
MP_CONSOLE_LOG="${MP_CONSOLE_LOG:-$MP_REPO_ROOT/.tmp/logs/console.log}"
MP_LOCAL_SECRET_ENV_FILE="${MP_LOCAL_SECRET_ENV_FILE:-$MP_REPO_ROOT/.tmp/secrets/local.env}"

mp_exit_with_error() {
  printf 'error: %s\n' "$1" >&2
  exit 1
}

mp_detect_env_id() {
  if [ "$(uname -s)" != "Linux" ] || [ ! -f /etc/os-release ]; then
    printf 'unknown'
    return 0
  fi

  # shellcheck disable=SC1091
  . /etc/os-release
  case "${ID:-}:${ID_LIKE:-}" in
    arch:*|cachyos:*|*:arch*)
      printf 'arch-cachyos'
      ;;
    *)
      printf 'unknown'
      ;;
  esac
}

mp_assert_target_env_id() {
  local expected_env_id="$1"
  local detected_env_id
  detected_env_id="$(mp_detect_env_id)"
  if [ "$detected_env_id" != "$expected_env_id" ] && [ "${MP_ALLOW_ENV_MISMATCH:-0}" != "1" ]; then
    mp_exit_with_error "this script targets $expected_env_id but detected $detected_env_id; set MP_ALLOW_ENV_MISMATCH=1 to override"
  fi
}

mp_require_command() {
  command -v "$1" >/dev/null 2>&1 || mp_exit_with_error "missing required command: $1"
}

mp_load_local_secrets() {
  if [ -f "$MP_LOCAL_SECRET_ENV_FILE" ]; then
    set -a
    # shellcheck disable=SC1090
    . "$MP_LOCAL_SECRET_ENV_FILE"
    set +a
  fi
}

mp_prepare_runtime_paths() {
  mkdir -p /tmp/mp-cache/run "$MP_REPO_ROOT/.tmp/logs" "$MP_REPO_ROOT/.tmp/data" "$MP_REPO_ROOT/.tmp/exports" "$MP_REPO_ROOT/.tmp/secrets" "$MP_REPO_ROOT/logging"
}

mp_console_log_contains() {
  local expected_text="$1"
  [ -f "$MP_CONSOLE_LOG" ] && grep -F "$expected_text" "$MP_CONSOLE_LOG" >/dev/null 2>&1
}

mp_output_mentions_socket_permission_error() {
  local command_output="$1"
  printf '%s\n' "$command_output" | grep -E 'Operation not permitted|Permission denied' >/dev/null 2>&1
}

mp_is_state_load_failure() {
  mp_console_log_contains "failed to load checkpoint or journal state"
}

mp_is_socket_bind_permission_failure() {
  if ! mp_console_log_contains "failed to bind unix socket at"; then
    return 1
  fi
  mp_console_log_contains "Operation not permitted" || mp_console_log_contains "Permission denied"
}

mp_resolve_effective_config_value() {
  local config_key="$1"
  local config_output=""

  [ -x "$MP_REPO_ROOT/build/local-debug/mp-cache-server" ] || mp_exit_with_error "missing binary: build/local-debug/mp-cache-server"
  config_output="$("$MP_REPO_ROOT/build/local-debug/mp-cache-server" --config "$MP_CONFIG_PATH" --print-config 2>/dev/null)" || return 1
  printf '%s\n' "$config_output" | awk -F= -v config_key="$config_key" '
    $1 == config_key {
      print substr($0, index($0, "=") + 1)
      found = 1
    }
    END {
      if (found != 1) {
        exit 1
      }
    }'
}

mp_expand_repo_relative_path() {
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

mp_rotate_state_file_if_present() {
  local configured_path="$1"
  local rotation_tag="$2"
  local resolved_path=""
  local rotated_path=""

  resolved_path="$(mp_expand_repo_relative_path "$configured_path")"
  if ! [ -f "$resolved_path" ]; then
    return 0
  fi

  rotated_path="${resolved_path}.${rotation_tag}.bak"
  mv "$resolved_path" "$rotated_path"
  printf 'warning: rotated local state file %s -> %s\n' "$resolved_path" "$rotated_path" >&2
}

mp_rotate_local_state_files() {
  local checkpoint_path=""
  local journal_path=""
  local rotation_tag=""

  checkpoint_path="$(mp_resolve_effective_config_value "storage.checkpoint_path")" || return 1
  journal_path="$(mp_resolve_effective_config_value "storage.journal_path")" || return 1
  rotation_tag="$(date -u +%Y%m%dT%H%M%SZ)-$$"

  mp_rotate_state_file_if_present "$checkpoint_path" "$rotation_tag"
  mp_rotate_state_file_if_present "$journal_path" "$rotation_tag"
}

mp_require_local_dependencies() {
  mp_require_command cmake
  mp_require_command make
  if ! command -v cc >/dev/null 2>&1 && ! command -v gcc >/dev/null 2>&1 && ! command -v clang >/dev/null 2>&1; then
    mp_exit_with_error "missing required C compiler"
  fi
  mp_require_command git
  mp_require_command curl
}

mp_build_server() {
  local target_env_id="${1:-}"
  [ -n "$target_env_id" ] || mp_exit_with_error "environment id is required"
  mp_assert_target_env_id "$target_env_id"
  mp_require_local_dependencies
  mp_prepare_runtime_paths
  (cd "$MP_REPO_ROOT" && cmake --fresh --preset local-debug && cmake --build --preset local-debug)
}

mp_is_server_running() {
  [ -f "$MP_PID_FILE" ] || return 1
  kill -0 "$(cat "$MP_PID_FILE")" 2>/dev/null
}

mp_try_start_server() {
  mp_prepare_runtime_paths
  mp_load_local_secrets
  [ -x "$MP_REPO_ROOT/build/local-debug/mp-cache-server" ] || mp_exit_with_error "missing binary: build/local-debug/mp-cache-server"

  if mp_is_server_running; then
    mp_exit_with_error "server already running with pid $(cat "$MP_PID_FILE")"
  fi

  (
    cd "$MP_REPO_ROOT"
    nohup ./build/local-debug/mp-cache-server --config "$MP_CONFIG_PATH" >"$MP_CONSOLE_LOG" 2>&1 &
  )

  sleep 1
  mp_is_server_running
}

mp_report_startup_failure() {
  local checkpoint_path=""
  local journal_path=""

  if mp_is_state_load_failure; then
    checkpoint_path="$(mp_resolve_effective_config_value "storage.checkpoint_path" 2>/dev/null || printf 'the configured checkpoint path')"
    journal_path="$(mp_resolve_effective_config_value "storage.journal_path" 2>/dev/null || printf 'the configured journal path')"
    mp_exit_with_error "server failed to start because local state could not be loaded; reuse the previous MP_SECRET_LOCAL_STORAGE_KEY or move aside $checkpoint_path and $journal_path before retrying"
  fi
  if mp_is_socket_bind_permission_failure; then
    mp_exit_with_error "server failed to bind the Unix socket; this sandbox/runtime denies Unix-socket bind/connect operations, so retry from a normal local shell"
  fi

  mp_exit_with_error "server failed to start; inspect $MP_CONSOLE_LOG"
}

mp_start_server() {
  if mp_try_start_server; then
    return 0
  fi

  if [ "${MP_AUTO_RESET_LOCAL_STATE_ON_LOAD_FAILURE:-1}" = "1" ] && mp_is_state_load_failure; then
    printf 'warning: local state could not be loaded; rotating configured checkpoint and journal files before retrying startup\n' >&2
    if mp_rotate_local_state_files && mp_try_start_server; then
      printf 'warning: local state recovery succeeded; the server started with a clean cache and the previous files were preserved as timestamped backups\n' >&2
      return 0
    fi
  fi

  mp_report_startup_failure
}

mp_stop_server() {
  local server_pid=""
  local stop_attempt_count=0

  if ! [ -f "$MP_PID_FILE" ]; then
    printf 'server is not running\n'
    return 0
  fi

  server_pid="$(cat "$MP_PID_FILE")"
  if ! kill -0 "$server_pid" 2>/dev/null; then
    rm -f "$MP_PID_FILE"
    printf 'removed stale pid file\n'
    return 0
  fi

  kill -TERM "$server_pid"
  while kill -0 "$server_pid" 2>/dev/null; do
    stop_attempt_count=$((stop_attempt_count + 1))
    if [ "$stop_attempt_count" -ge 20 ]; then
      mp_exit_with_error "server did not stop within the expected time"
    fi
    sleep 1
  done

  rm -f "$MP_PID_FILE"
}

mp_test_health_endpoint() {
  local curl_output=""

  if curl_output="$(curl --fail --silent --show-error --unix-socket "$MP_SOCKET_PATH" http://localhost/v1/health 2>&1)"; then
    printf '%s\n' "$curl_output"
    return 0
  fi
  if mp_output_mentions_socket_permission_error "$curl_output"; then
    mp_exit_with_error "health check could not connect to $MP_SOCKET_PATH because this sandbox/runtime denies Unix-socket connect operations; retry from a normal local shell"
  fi

  printf '%s\n' "$curl_output" >&2
  return 1
}

mp_deploy_server() {
  local target_env_id="${1:-}"
  mp_build_server "$target_env_id"
  mp_start_server
}

mp_report_local_environment() {
  printf 'repo_root=%s\n' "$MP_REPO_ROOT"
  printf 'env_id=%s\n' "$(mp_detect_env_id)"
  printf 'config_path=%s\n' "$MP_CONFIG_PATH"
  printf 'socket_path=%s\n' "$MP_SOCKET_PATH"
  printf 'pid_file=%s\n' "$MP_PID_FILE"
  printf 'console_log=%s\n' "$MP_CONSOLE_LOG"

  mp_require_local_dependencies
  if [ -x "$MP_REPO_ROOT/build/local-debug/mp-cache-server" ]; then
    printf 'binary=present\n'
  else
    printf 'binary=missing\n'
  fi

  if mp_is_server_running; then
    printf 'server=running\n'
  else
    printf 'server=stopped\n'
  fi
}
