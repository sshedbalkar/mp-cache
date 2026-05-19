#!/usr/bin/env bash
set -euo pipefail

# Deploy mp-cache as a local auto-start service behind the host Nginx server.
#
# This script is intentionally both a first-time installer and a recurring
# deployment helper. Re-running it rebuilds the local binary by default,
# refreshes generated systemd/Nginx files, restarts the service, reloads Nginx,
# verifies the proxied health endpoint, and writes a deployment report under
# .tmp/deploy/local/reports/.
#
# Defaults are local-development oriented and centralized:
# - Shell-script paths, service names, and systemd settings come from
#   configs/scripts/defaults.env through scripts/lib/script-config-env.sh.
# - Nginx paths and location prefix come from configs/deploy/nginx-paths.env.
# - Application cache, storage, observability, and security values come from
#   the configured server config file.
#
# Useful overrides:
#   MP_LOCAL_DEPLOY_BUILD=0                          skip rebuilding first
#   MP_LOCAL_DEPLOY_USE_ARTIFACT=0                   run directly from build dir
#   MP_LOCAL_DEPLOY_ARTIFACT=/path/mp-cache.tar.gz   choose another artifact
#   MP_LOCAL_DEPLOY_BUILD_DIR=/path/to/build/dir     choose another build dir
#   MP_LOCAL_DEPLOY_NGINX_LISTEN=127.0.0.1:18080     choose proxy listen address
#   MP_LOCAL_DEPLOY_SERVICE_NAME=<name>              choose systemd unit name
#   MP_LOCAL_DEPLOY_REPORT_DIR=.tmp/deploy/local/reports
#                                                     choose report directory
#
# Usage examples:
#   ./scripts/deploy-local.sh
#   MP_LOCAL_DEPLOY_BUILD=0 ./scripts/deploy-local.sh
#   MP_LOCAL_DEPLOY_USE_ARTIFACT=0 ./scripts/deploy-local.sh

cd "$(dirname "$0")/.."
. ./scripts/lib/local-env.sh
. ./scripts/lib/version-env.sh
. ./scripts/lib/nginx-env.sh

mp_local_deploy_service_name="${MP_LOCAL_DEPLOY_SERVICE_NAME:-$MP_SCRIPT_DEFAULT_LOCAL_DEPLOY_SERVICE_NAME}"
mp_local_deploy_build_dir="${MP_LOCAL_DEPLOY_BUILD_DIR:-$MP_LOCAL_BUILD_DIR}"
mp_local_deploy_should_build="${MP_LOCAL_DEPLOY_BUILD:-$MP_SCRIPT_DEFAULT_LOCAL_DEPLOY_SHOULD_BUILD}"
mp_local_deploy_should_use_artifact="${MP_LOCAL_DEPLOY_USE_ARTIFACT:-$MP_SCRIPT_DEFAULT_LOCAL_DEPLOY_USE_ARTIFACT}"
mp_local_deploy_artifact_path="${MP_LOCAL_DEPLOY_ARTIFACT:-}"
mp_local_deploy_nginx_listen="${MP_LOCAL_DEPLOY_NGINX_LISTEN:-$MP_SCRIPT_DEFAULT_LOCAL_DEPLOY_NGINX_LISTEN}"
mp_local_deploy_socket_dir="$(mp_script_join_path "$MP_SCRIPT_DEFAULT_SYSTEM_RUNTIME_ROOT" "$mp_local_deploy_service_name")"
mp_local_deploy_socket_file="$mp_local_deploy_socket_dir/mp-cache.sock"
mp_local_deploy_pid_file="$mp_local_deploy_socket_dir/mp-cache.pid"
mp_local_deploy_state_dir="$(mp_script_join_path "$MP_SCRIPT_DEFAULT_SYSTEM_STATE_ROOT" "$mp_local_deploy_service_name")"
mp_local_deploy_log_dir="$(mp_script_join_path "$MP_SCRIPT_DEFAULT_SYSTEM_LOG_ROOT" "$mp_local_deploy_service_name")"
mp_local_deploy_unit_file="$(mp_script_join_path "$MP_SCRIPT_DEFAULT_SYSTEMD_UNIT_DIR" "$mp_local_deploy_service_name.service")"
mp_local_deploy_generated_dir="$(mp_script_repo_path "$MP_SCRIPT_DEFAULT_LOCAL_DEPLOY_GENERATED_DIR")"
mp_local_deploy_config_file="$mp_local_deploy_generated_dir/$mp_local_deploy_service_name.ini"
mp_local_deploy_unit_staging_file="$mp_local_deploy_generated_dir/$mp_local_deploy_service_name.service"
mp_local_deploy_nginx_staging_file="$mp_local_deploy_generated_dir/$mp_local_deploy_service_name.nginx.conf"
mp_local_deploy_artifact_extract_dir="$mp_local_deploy_generated_dir/artifact"
mp_local_deploy_report_dir="${MP_LOCAL_DEPLOY_REPORT_DIR:-$(mp_script_join_path "$mp_local_deploy_generated_dir" "$MP_SCRIPT_DEFAULT_LOCAL_DEPLOY_REPORT_SUBDIR")}"
mp_local_deploy_report_path="$mp_local_deploy_report_dir/deploy-local-report.md"
mp_local_deploy_started_at_utc="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
mp_local_deploy_current_step="initializing"
mp_local_deploy_step_names=()
mp_local_deploy_step_statuses=()
mp_local_deploy_step_details=()

mp_local_deploy_print_usage() {
  cat <<'EOF'
Usage: ./scripts/deploy-local.sh

Installs or updates mp-cache as a local systemd service behind local Nginx.

Environment:
  MP_LOCAL_DEPLOY_BUILD
      Set to 0 to reuse existing build files without invoking CMake.
  MP_LOCAL_DEPLOY_USE_ARTIFACT
      Set to 0 to run directly from MP_LOCAL_DEPLOY_BUILD_DIR.
  MP_LOCAL_DEPLOY_ARTIFACT
      Artifact archive to stage. Default package directory comes from configs/scripts/defaults.env.
  MP_LOCAL_DEPLOY_BUILD_DIR
      Build directory containing mp-cache-server.
  MP_LOCAL_DEPLOY_NGINX_LISTEN
      Nginx listen address.
  MP_LOCAL_DEPLOY_SERVICE_NAME
      Local systemd unit and runtime directory name.
  MP_LOCAL_DEPLOY_REPORT_DIR
      Directory for deploy-local-report.md.
  MP_NGINX_PATH_CONFIG_FILE
      Nginx path constants file. Default: MP_SCRIPT_DEFAULT_NGINX_PATH_CONFIG_PATH from configs/scripts/defaults.env.
  MP_SCRIPT_CONFIG_FILE
      Script defaults file. Default: configs/scripts/defaults.env.
EOF
}

mp_local_deploy_begin_step() {
  mp_local_deploy_current_step="$1"
}

mp_local_deploy_record_step() {
  mp_local_deploy_step_names+=("$1")
  mp_local_deploy_step_statuses+=("$2")
  mp_local_deploy_step_details+=("$3")
}

mp_local_deploy_pass_step() {
  mp_local_deploy_record_step "$mp_local_deploy_current_step" "PASS" "$1"
  mp_local_deploy_current_step=""
}

mp_local_deploy_write_report() {
  local exit_code="$1"
  local deployment_status="PASS"
  local completed_at_utc=""
  local step_index=0
  local report_tmp_path=""

  completed_at_utc="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  if [ "$exit_code" -ne 0 ]; then
    deployment_status="FAIL"
  fi

  mkdir -p "$mp_local_deploy_report_dir" 2>/dev/null || return 0
  report_tmp_path="$mp_local_deploy_report_path.tmp"

  {
    printf '# Local Deployment Report\n\n'
    printf '| Field | Value |\n'
    printf '|:------|:------|\n'
    printf '| Status | %s |\n' "$deployment_status"
    printf '| Started at UTC | %s |\n' "$mp_local_deploy_started_at_utc"
    printf '| Completed at UTC | %s |\n' "$completed_at_utc"
    printf '| Service | `%s.service` |\n' "$mp_local_deploy_service_name"
    printf '| Build mode | `%s` |\n' "$([ "$mp_local_deploy_should_build" = "1" ] && printf 'build before deploy' || printf 'reuse existing build')"
    printf '| Artifact mode | `%s` |\n' "$([ "$mp_local_deploy_should_use_artifact" = "1" ] && printf 'stage artifact' || printf 'run build directory')"
    printf '| Artifact | `%s` |\n' "$mp_local_deploy_artifact_path"
    printf '| Service binary directory | `%s` |\n' "$mp_local_deploy_build_dir"
    printf '| Generated config | `%s` |\n' "$mp_local_deploy_config_file"
    printf '| Generated systemd unit | `%s` |\n' "$mp_local_deploy_unit_staging_file"
    printf '| Installed systemd unit | `%s` |\n' "$mp_local_deploy_unit_file"
    printf '| Generated Nginx config | `%s` |\n' "$mp_local_deploy_nginx_staging_file"
    printf '| Proxy URL | `http://%s%s` |\n' "$mp_local_deploy_nginx_listen" "$MP_CACHE_NGINX_LOCATION_PATH"
    printf '| Post-deployment test | `./scripts/test-local-deployment.sh` |\n\n'

    printf '## Steps\n\n'
    printf '| Step | Status | Detail |\n'
    printf '|:-----|:-------|:-------|\n'
    while [ "$step_index" -lt "${#mp_local_deploy_step_names[@]}" ]; do
      printf '| %s | %s | %s |\n' \
      "${mp_local_deploy_step_names[$step_index]}" \
      "${mp_local_deploy_step_statuses[$step_index]}" \
      "${mp_local_deploy_step_details[$step_index]}"
      step_index=$((step_index + 1))
    done
    if [ "$deployment_status" = "FAIL" ] && [ -n "$mp_local_deploy_current_step" ]; then
      printf '| %s | FAIL | inspect the console output above this report path |\n' "$mp_local_deploy_current_step"
    fi
  } >"$report_tmp_path"
  mv "$report_tmp_path" "$mp_local_deploy_report_path"
  printf 'local deployment report: %s\n' "$mp_local_deploy_report_path" >&2
}

mp_local_deploy_run_privileged() {
  if [ "$(id -u)" -eq 0 ]; then
    "$@"
    return 0
  fi

  sudo "$@"
}

mp_local_deploy_resolve_artifact_path() {
  if [ -n "$mp_local_deploy_artifact_path" ]; then
    return 0
  fi

  mp_local_deploy_artifact_path="$(mp_script_repo_path "$MP_SCRIPT_DEFAULT_LOCAL_PACKAGE_DIR")/mp-cache-$(mp_read_build_version_from_config "$MP_CONFIG_PATH").tar.gz"
}

mp_local_deploy_prepare_privilege() {
  # Ask for administrator credentials before building so a missing sudo session
  # fails early instead of after the local build has already completed.
  if [ "$(id -u)" -ne 0 ]; then
    sudo -v
  fi
}

mp_local_deploy_capture_primary_group() {
  id -gn
}

mp_local_deploy_select_nginx_conf_file() {
  if [ -d "$MP_CACHE_NGINX_CONF_DIR" ]; then
    printf '%s/%s.conf\n' "$MP_CACHE_NGINX_CONF_DIR" "$mp_local_deploy_service_name"
    return 0
  fi

  printf '%s/%s.conf\n' "$MP_CACHE_NGINX_SITES_AVAILABLE_DIR" "$mp_local_deploy_service_name"
}

mp_local_deploy_enable_nginx_site_if_needed() {
  local nginx_conf_file="$1"
  local nginx_enabled_file=""

  if [ -d "$MP_CACHE_NGINX_SITES_ENABLED_DIR" ] && [ "${nginx_conf_file#"$MP_CACHE_NGINX_SITES_AVAILABLE_DIR/"}" != "$nginx_conf_file" ]; then
    nginx_enabled_file="$MP_CACHE_NGINX_SITES_ENABLED_DIR/$(basename "$nginx_conf_file")"
    mp_local_deploy_run_privileged ln -sfn "$nginx_conf_file" "$nginx_enabled_file"
  fi
}

mp_local_deploy_stage_artifact() {
  local artifact_root_dir=""

  if [ "$mp_local_deploy_should_use_artifact" != "1" ]; then
    return 0
  fi

  [ -f "$mp_local_deploy_artifact_path" ] ||
    mp_exit_with_error "missing local deploy artifact: $mp_local_deploy_artifact_path; run ./scripts/build-local.sh or set MP_LOCAL_DEPLOY_USE_ARTIFACT=0"

  rm -rf "$mp_local_deploy_artifact_extract_dir"
  mkdir -p "$mp_local_deploy_artifact_extract_dir"
  tar -xzf "$mp_local_deploy_artifact_path" -C "$mp_local_deploy_artifact_extract_dir"

  artifact_root_dir="$(find "$mp_local_deploy_artifact_extract_dir" -mindepth 1 -maxdepth 1 -type d | head -n 1)"
  [ -n "$artifact_root_dir" ] ||
    mp_exit_with_error "local deploy artifact did not contain a top-level directory"
  [ -x "$artifact_root_dir/bin/mp-cache-server" ] ||
    mp_exit_with_error "local deploy artifact is missing bin/mp-cache-server"

  mp_local_deploy_build_dir="$artifact_root_dir/bin"
}

mp_local_deploy_render_config() {
  local config_binary="$mp_local_deploy_build_dir/mp-cache-server"

  # The generated config uses absolute paths so systemd can start the service
  # reliably at boot without depending on the caller's current directory.
  cat >"$mp_local_deploy_config_file" <<EOF
[service]
environment_name = $(mp_resolve_effective_config_value service.environment_name "$config_binary")
service_name = $(mp_resolve_effective_config_value service.service_name "$config_binary")
build_version = $(mp_resolve_effective_config_value service.build_version "$config_binary")

[server]
socket_path = $mp_local_deploy_socket_file
pid_file_path = $mp_local_deploy_pid_file
shutdown_timeout_millis = $(mp_resolve_effective_config_value server.shutdown_timeout_millis "$config_binary")

[cache]
memory_limit_bytes = $(mp_resolve_effective_config_value cache.memory_limit_bytes "$config_binary")
default_ttl_seconds = $(mp_resolve_effective_config_value cache.default_ttl_seconds "$config_binary")
min_ttl_seconds = $(mp_resolve_effective_config_value cache.min_ttl_seconds "$config_binary")
max_ttl_seconds = $(mp_resolve_effective_config_value cache.max_ttl_seconds "$config_binary")
max_key_bytes = $(mp_resolve_effective_config_value cache.max_key_bytes "$config_binary")
max_value_bytes = $(mp_resolve_effective_config_value cache.max_value_bytes "$config_binary")
bucket_count = $(mp_resolve_effective_config_value cache.bucket_count "$config_binary")
sweep_interval_seconds = $(mp_resolve_effective_config_value cache.sweep_interval_seconds "$config_binary")

[storage]
data_directory = $mp_local_deploy_state_dir
export_directory = $mp_local_deploy_state_dir/exports
checkpoint_path = $mp_local_deploy_state_dir/state.checkpoint
journal_path = $mp_local_deploy_state_dir/state.journal
max_export_files = $(mp_resolve_effective_config_value storage.max_export_files "$config_binary")

[observability]
log_directory = $mp_local_deploy_log_dir
max_log_lines = $(mp_resolve_effective_config_value observability.max_log_lines "$config_binary")

[security]
bootstrap_admin_token_secret_ref = $(mp_resolve_effective_config_value security.bootstrap_admin_token_secret_ref "$config_binary")
storage_key_secret_ref = $(mp_resolve_effective_config_value security.storage_key_secret_ref "$config_binary")
rate_limit_requests = $(mp_resolve_effective_config_value security.rate_limit_requests "$config_binary")
rate_limit_window_seconds = $(mp_resolve_effective_config_value security.rate_limit_window_seconds "$config_binary")
EOF
}

mp_local_deploy_render_systemd_unit() {
  local service_group_name="$1"

  # UMask=0000 keeps the Unix socket connectable by the local Nginx worker.
  # The service still binds only a filesystem socket and Nginx listens on the
  # loopback address configured below.
  cat >"$mp_local_deploy_unit_staging_file" <<EOF
[Unit]
Description=mp-cache local service behind Nginx
After=network-online.target nginx.service
Wants=network-online.target
RequiresMountsFor=$MP_REPO_ROOT $mp_local_deploy_generated_dir $mp_local_deploy_state_dir $mp_local_deploy_log_dir

[Service]
Type=simple
User=$(id -un)
Group=$service_group_name
WorkingDirectory=$MP_REPO_ROOT
EnvironmentFile=$MP_LOCAL_SECRET_ENV_FILE
ExecStart=$mp_local_deploy_build_dir/mp-cache-server --config $mp_local_deploy_config_file
Restart=$MP_SCRIPT_DEFAULT_SYSTEMD_RESTART_POLICY
RestartSec=$MP_SCRIPT_DEFAULT_SYSTEMD_RESTART_SEC
NoNewPrivileges=true
PrivateTmp=true
ProtectSystem=strict
ProtectHome=false
ReadWritePaths=$mp_local_deploy_generated_dir $mp_local_deploy_state_dir $mp_local_deploy_log_dir $mp_local_deploy_socket_dir
RuntimeDirectory=$mp_local_deploy_service_name
RuntimeDirectoryMode=$MP_SCRIPT_DEFAULT_LOCAL_SYSTEMD_RUNTIME_MODE
StateDirectory=$mp_local_deploy_service_name
StateDirectoryMode=$MP_SCRIPT_DEFAULT_SYSTEMD_STATE_MODE
LogsDirectory=$mp_local_deploy_service_name
LogsDirectoryMode=$MP_SCRIPT_DEFAULT_SYSTEMD_LOGS_MODE
LimitNOFILE=$MP_SCRIPT_DEFAULT_SYSTEMD_LIMIT_NOFILE
UMask=$MP_SCRIPT_DEFAULT_LOCAL_SYSTEMD_UMASK

[Install]
WantedBy=multi-user.target
EOF
}

mp_local_deploy_render_nginx_proxy() {
  # Nginx proxies normal HTTP requests to the mp-cache Unix socket. The proxy
  # stays loopback-only by default; change MP_LOCAL_DEPLOY_NGINX_LISTEN only
  # when you intentionally want a different local binding.
  cat >"$mp_local_deploy_nginx_staging_file" <<EOF
upstream mp_cache_local_upstream {
	server unix:$mp_local_deploy_socket_file;
}

server {
	listen $mp_local_deploy_nginx_listen;
	server_name $MP_SCRIPT_DEFAULT_LOCAL_NGINX_SERVER_NAMES;

	access_log $MP_CACHE_NGINX_LOG_DIR/$mp_local_deploy_service_name.access.log;
	error_log $MP_CACHE_NGINX_LOG_DIR/$mp_local_deploy_service_name.error.log $MP_CACHE_NGINX_ERROR_LOG_LEVEL;

	location $MP_CACHE_NGINX_LOCATION_PATH {
		rewrite ^$MP_CACHE_NGINX_LOCATION_PATH/?(.*)$ /\$1 break;
		proxy_pass http://mp_cache_local_upstream;
		proxy_http_version 1.1;
		proxy_set_header Host \$host;
		proxy_set_header X-Real-IP \$remote_addr;
		proxy_set_header X-Forwarded-For \$proxy_add_x_forwarded_for;
		proxy_set_header X-Forwarded-Proto \$scheme;
		proxy_set_header Authorization \$http_authorization;
		proxy_set_header Connection "";
	}
}
EOF
}

mp_local_deploy_install_systemd_unit() {
  mp_local_deploy_run_privileged install -m 0644 "$mp_local_deploy_unit_staging_file" "$mp_local_deploy_unit_file"
  mp_local_deploy_run_privileged systemctl daemon-reload
  mp_local_deploy_run_privileged systemctl enable --now "$mp_local_deploy_service_name.service"
  mp_local_deploy_run_privileged systemctl restart "$mp_local_deploy_service_name.service"
}

mp_local_deploy_install_nginx_proxy() {
  local nginx_conf_file="$1"

  mp_require_command nginx
  mp_local_deploy_run_privileged install -m 0644 "$mp_local_deploy_nginx_staging_file" "$nginx_conf_file"
  mp_local_deploy_enable_nginx_site_if_needed "$nginx_conf_file"
  mp_local_deploy_run_privileged nginx -t
  if mp_local_deploy_run_privileged systemctl is-active --quiet nginx; then
    mp_local_deploy_run_privileged systemctl reload nginx
  else
    mp_local_deploy_run_privileged systemctl enable --now nginx
  fi
}

mp_local_deploy_verify_proxy() {
  local health_endpoint_path=""

  health_endpoint_path="$(mp_read_c_string_constant MP_CACHE_HTTP_ROUTE_HEALTH)" ||
    mp_exit_with_error "failed to read health endpoint from internal/config/constants.h"

  curl --fail --silent --show-error "http://$mp_local_deploy_nginx_listen$MP_CACHE_NGINX_LOCATION_PATH$health_endpoint_path" >/dev/null
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --help)
      mp_local_deploy_print_usage
      exit 0
      ;;
    *)
      mp_exit_with_error "unknown option: $1"
      ;;
  esac
done

trap 'mp_local_deploy_write_report "$?"' EXIT

mp_local_deploy_begin_step "check required commands"
mp_require_command curl
mp_require_command systemctl
mp_require_command install
mp_require_command tar
mp_local_deploy_pass_step "required commands are available"

mp_local_deploy_begin_step "prepare administrator privileges"
mp_local_deploy_prepare_privilege
mp_local_deploy_pass_step "administrator privilege check completed"

if [ "$mp_local_deploy_should_build" = "1" ]; then
  mp_local_deploy_begin_step "build local artifact"
  ./scripts/build-local.sh
  mp_local_deploy_pass_step "local build and artifact completed"
else
  mp_local_deploy_record_step "build local artifact" "SKIP" "MP_LOCAL_DEPLOY_BUILD=0"
fi

if [ "$mp_local_deploy_should_use_artifact" = "1" ]; then
  mp_local_deploy_resolve_artifact_path
fi

mp_local_deploy_begin_step "stage deployment artifact"
mp_local_deploy_stage_artifact
mp_local_deploy_pass_step "service binary directory is $mp_local_deploy_build_dir"

mp_local_deploy_begin_step "validate service binary"
[ -x "$mp_local_deploy_build_dir/mp-cache-server" ] ||
  mp_exit_with_error "missing executable build file: $mp_local_deploy_build_dir/mp-cache-server"
mp_local_deploy_pass_step "mp-cache-server is executable"

mp_local_deploy_begin_step "prepare local secrets"
./scripts/write-local-secret-env.sh
mp_local_deploy_pass_step "local secret environment file is ready"

mp_local_deploy_begin_step "render generated deployment files"
mkdir -p "$mp_local_deploy_generated_dir"
mp_local_deploy_render_config
mp_local_deploy_render_systemd_unit "$(mp_local_deploy_capture_primary_group)"
mp_local_deploy_render_nginx_proxy
mp_local_deploy_pass_step "generated config, systemd unit, and Nginx proxy files"

mp_local_deploy_begin_step "install and restart systemd service"
mp_local_deploy_install_systemd_unit
mp_local_deploy_pass_step "systemd service installed and restarted"

mp_local_deploy_begin_step "install and reload Nginx proxy"
mp_local_deploy_install_nginx_proxy "$(mp_local_deploy_select_nginx_conf_file)"
mp_local_deploy_pass_step "Nginx proxy installed and reloaded"

mp_local_deploy_begin_step "verify proxied health endpoint"
mp_local_deploy_verify_proxy
mp_local_deploy_pass_step "proxied health endpoint returned success"

printf 'local service deployed: %s\n' "$mp_local_deploy_service_name"
printf 'local proxy ready: http://%s\n' "$mp_local_deploy_nginx_listen"
printf 'post-deployment smoke test: ./scripts/test-local-deployment.sh\n'
