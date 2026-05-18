#!/usr/bin/env bash
set -euo pipefail

# Deploy mp-cache as a local auto-start service behind the host Nginx server.
#
# This script is intentionally both a first-time installer and a recurring
# deployment helper. Re-running it rebuilds the local binary by default,
# refreshes generated systemd/Nginx files, restarts the service, reloads Nginx,
# and verifies the proxied health endpoint.
#
# Defaults are local-development oriented:
# - The service binary is staged from dist/local/mp-cache-local.tar.gz.
# - The service runs as the current user so local secret env files remain usable.
# - Nginx listens on 127.0.0.1:8080 and proxies to the service Unix socket.
# - Durable local service state lives under systemd StateDirectory/LogsDirectory
#   names for mp-cache-local instead of repository .tmp files.
#
# Useful overrides:
#   MP_LOCAL_DEPLOY_BUILD=0                          skip rebuilding first
#   MP_LOCAL_DEPLOY_USE_ARTIFACT=0                   run directly from build dir
#   MP_LOCAL_DEPLOY_ARTIFACT=/path/mp-cache.tar.gz   choose another artifact
#   MP_LOCAL_DEPLOY_BUILD_DIR=/path/to/build/dir     choose another build dir
#   MP_LOCAL_DEPLOY_NGINX_LISTEN=127.0.0.1:18080     choose proxy listen address
#   MP_LOCAL_DEPLOY_SERVICE_NAME=mp-cache-local      choose systemd unit name

cd "$(dirname "$0")/.."
. ./scripts/lib/local-env.sh
. ./scripts/lib/nginx-env.sh

mp_local_deploy_service_name="${MP_LOCAL_DEPLOY_SERVICE_NAME:-mp-cache-local}"
mp_local_deploy_build_dir="${MP_LOCAL_DEPLOY_BUILD_DIR:-$MP_REPO_ROOT/build/local-debug}"
mp_local_deploy_should_build="${MP_LOCAL_DEPLOY_BUILD:-1}"
mp_local_deploy_should_use_artifact="${MP_LOCAL_DEPLOY_USE_ARTIFACT:-1}"
mp_local_deploy_artifact_path="${MP_LOCAL_DEPLOY_ARTIFACT:-$MP_REPO_ROOT/dist/local/mp-cache-local.tar.gz}"
mp_local_deploy_nginx_listen="${MP_LOCAL_DEPLOY_NGINX_LISTEN:-127.0.0.1:8080}"
mp_local_deploy_socket_dir="/run/$mp_local_deploy_service_name"
mp_local_deploy_socket_file="$mp_local_deploy_socket_dir/mp-cache.sock"
mp_local_deploy_pid_file="$mp_local_deploy_socket_dir/mp-cache.pid"
mp_local_deploy_state_dir="/var/lib/$mp_local_deploy_service_name"
mp_local_deploy_log_dir="/var/log/$mp_local_deploy_service_name"
mp_local_deploy_unit_file="/etc/systemd/system/$mp_local_deploy_service_name.service"
mp_local_deploy_generated_dir="$MP_REPO_ROOT/.tmp/deploy/local"
mp_local_deploy_config_file="$mp_local_deploy_generated_dir/$mp_local_deploy_service_name.ini"
mp_local_deploy_unit_staging_file="$mp_local_deploy_generated_dir/$mp_local_deploy_service_name.service"
mp_local_deploy_nginx_staging_file="$mp_local_deploy_generated_dir/$mp_local_deploy_service_name.nginx.conf"
mp_local_deploy_artifact_extract_dir="$mp_local_deploy_generated_dir/artifact"

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
      Artifact archive to stage. Default: dist/local/mp-cache-local.tar.gz.
  MP_LOCAL_DEPLOY_BUILD_DIR
      Build directory containing mp-cache-server. Default: build/local-debug.
  MP_LOCAL_DEPLOY_NGINX_LISTEN
      Nginx listen address. Default: 127.0.0.1:8080.
  MP_LOCAL_DEPLOY_SERVICE_NAME
      Local systemd unit and runtime directory name. Default: mp-cache-local.
  MP_NGINX_PATH_CONFIG_FILE
      Nginx path constants file. Default: configs/deploy/nginx-paths.env.
EOF
}

mp_local_deploy_run_privileged() {
  if [ "$(id -u)" -eq 0 ]; then
    "$@"
    return 0
  fi

  sudo "$@"
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
  # The generated config uses absolute paths so systemd can start the service
  # reliably at boot without depending on the caller's current directory.
  cat >"$mp_local_deploy_config_file" <<EOF
[service]
environment_name = local
service_name = mp-cache

[server]
socket_path = $mp_local_deploy_socket_file
pid_file_path = $mp_local_deploy_pid_file
shutdown_timeout_millis = 5000

[cache]
memory_limit_bytes = 268435456
default_ttl_seconds = 172800
min_ttl_seconds = 1
max_ttl_seconds = 2592000
max_key_bytes = 256
max_value_bytes = 1048576
bucket_count = 4096
sweep_interval_seconds = 5

[storage]
data_directory = $mp_local_deploy_state_dir
export_directory = $mp_local_deploy_state_dir/exports
checkpoint_path = $mp_local_deploy_state_dir/state.checkpoint
journal_path = $mp_local_deploy_state_dir/state.journal
max_export_files = 16

[observability]
log_directory = $mp_local_deploy_log_dir
max_log_lines = 200

[security]
bootstrap_admin_token_secret_ref = env:MP_SECRET_LOCAL_BOOTSTRAP_ADMIN_TOKEN
storage_key_secret_ref = env:MP_SECRET_LOCAL_STORAGE_KEY
rate_limit_requests = 240
rate_limit_window_seconds = 60
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
Restart=always
RestartSec=2s
NoNewPrivileges=true
PrivateTmp=true
ProtectSystem=strict
ProtectHome=false
ReadWritePaths=$mp_local_deploy_generated_dir $mp_local_deploy_state_dir $mp_local_deploy_log_dir $mp_local_deploy_socket_dir
RuntimeDirectory=$mp_local_deploy_service_name
RuntimeDirectoryMode=0755
StateDirectory=$mp_local_deploy_service_name
StateDirectoryMode=0750
LogsDirectory=$mp_local_deploy_service_name
LogsDirectoryMode=0750
LimitNOFILE=4096
UMask=0000

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
	server_name localhost mp-cache.local;

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

mp_require_command curl
mp_require_command systemctl
mp_require_command install
mp_require_command tar
mp_local_deploy_prepare_privilege

if [ "$mp_local_deploy_should_build" = "1" ]; then
  ./scripts/build-local.sh
fi

mp_local_deploy_stage_artifact

[ -x "$mp_local_deploy_build_dir/mp-cache-server" ] ||
  mp_exit_with_error "missing executable build file: $mp_local_deploy_build_dir/mp-cache-server"

./scripts/write-local-secret-env.sh
mkdir -p "$mp_local_deploy_generated_dir"
mp_local_deploy_render_config
mp_local_deploy_render_systemd_unit "$(mp_local_deploy_capture_primary_group)"
mp_local_deploy_render_nginx_proxy
mp_local_deploy_install_systemd_unit
mp_local_deploy_install_nginx_proxy "$(mp_local_deploy_select_nginx_conf_file)"
mp_local_deploy_verify_proxy

printf 'local service deployed: %s\n' "$mp_local_deploy_service_name"
printf 'local proxy ready: http://%s\n' "$mp_local_deploy_nginx_listen"
