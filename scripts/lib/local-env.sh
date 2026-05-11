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

mp_start_server() {
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
  if ! mp_is_server_running; then
    mp_exit_with_error "server failed to start; inspect $MP_CONSOLE_LOG"
  fi
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
  curl --fail --silent --show-error --unix-socket "$MP_SOCKET_PATH" http://localhost/v1/health
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
