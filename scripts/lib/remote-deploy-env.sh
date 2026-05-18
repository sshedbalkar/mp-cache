#!/usr/bin/env bash
set -euo pipefail

. ./scripts/lib/nginx-env.sh

mp_remote_deploy_print_usage() {
  local remote_environment_name="$1"

  cat <<EOF
Usage: ./scripts/deploy-$remote_environment_name.sh <artifact.tar.gz> <ssh-target>

Deploys one packaged mp-cache artifact to the $remote_environment_name host.

Environment:
  MP_REMOTE_DEPLOY_ROOT
      Install root on the remote host. Default: /opt/mp-cache.
  MP_REMOTE_DEPLOY_SERVICE_USER
      Remote service identity. Default: mp-cache.
  MP_REMOTE_DEPLOY_SERVICE_GROUP
      Remote service group. Default: mp-cache.
  MP_REMOTE_DEPLOY_NGINX_LISTEN
      Remote Nginx listen address. Default: 127.0.0.1:8080.
  MP_REMOTE_DEPLOY_INSTALL_NGINX
      Set to 0 when an external proxy is managed separately. Default: 1.
  MP_NGINX_PATH_CONFIG_FILE
      Nginx path constants file. Default: configs/deploy/nginx-paths.env.
EOF
}

mp_remote_deploy_run() {
  local remote_environment_name="$1"
  shift

  local remote_archive_file="${1:-}"
  local remote_ssh_target="${2:-}"
  local remote_deploy_root="${MP_REMOTE_DEPLOY_ROOT:-/opt/mp-cache}"
  local remote_service_user="${MP_REMOTE_DEPLOY_SERVICE_USER:-mp-cache}"
  local remote_service_group="${MP_REMOTE_DEPLOY_SERVICE_GROUP:-mp-cache}"
  local remote_nginx_listen="${MP_REMOTE_DEPLOY_NGINX_LISTEN:-127.0.0.1:8080}"
  local remote_should_install_nginx="${MP_REMOTE_DEPLOY_INSTALL_NGINX:-1}"
  local remote_staging_dir="/tmp/mp-cache-deploy-$remote_environment_name-$$"
  local remote_archive_name=""

  if [ "$remote_archive_file" = "--help" ] || [ -z "$remote_archive_file" ] || [ -z "$remote_ssh_target" ]; then
    mp_remote_deploy_print_usage "$remote_environment_name"
    exit 0
  fi

  [ -f "$remote_archive_file" ] || {
    printf 'artifact archive not found: %s\n' "$remote_archive_file" >&2
    exit 1
  }

  command -v ssh >/dev/null 2>&1 || {
    printf 'missing required command: ssh\n' >&2
    exit 1
  }
  command -v scp >/dev/null 2>&1 || {
    printf 'missing required command: scp\n' >&2
    exit 1
  }

  remote_archive_name="$(basename "$remote_archive_file")"

  ssh "$remote_ssh_target" "mkdir -p '$remote_staging_dir'"
  scp "$remote_archive_file" "$remote_ssh_target:$remote_staging_dir/$remote_archive_name"
  if [ -f "$remote_archive_file.sha256" ]; then
    scp "$remote_archive_file.sha256" "$remote_ssh_target:$remote_staging_dir/$remote_archive_name.sha256"
  fi

  ssh "$remote_ssh_target" \
    "MP_REMOTE_ENVIRONMENT_NAME='$remote_environment_name' \
     MP_REMOTE_ARCHIVE_NAME='$remote_archive_name' \
     MP_REMOTE_DEPLOY_ROOT='$remote_deploy_root' \
     MP_REMOTE_SERVICE_USER='$remote_service_user' \
     MP_REMOTE_SERVICE_GROUP='$remote_service_group' \
     MP_REMOTE_NGINX_LISTEN='$remote_nginx_listen' \
     MP_REMOTE_INSTALL_NGINX='$remote_should_install_nginx' \
     MP_REMOTE_STAGING_DIR='$remote_staging_dir' \
     MP_CACHE_NGINX_MAIN_CONFIG_FILE='$MP_CACHE_NGINX_MAIN_CONFIG_FILE' \
     MP_CACHE_NGINX_CONF_DIR='$MP_CACHE_NGINX_CONF_DIR' \
     MP_CACHE_NGINX_LOG_DIR='$MP_CACHE_NGINX_LOG_DIR' \
     MP_CACHE_NGINX_REMOTE_CONF_FILE_NAME='$MP_CACHE_NGINX_REMOTE_CONF_FILE_NAME' \
     MP_CACHE_NGINX_LOCATION_PATH='$MP_CACHE_NGINX_LOCATION_PATH' \
     MP_CACHE_NGINX_ERROR_LOG_LEVEL='$MP_CACHE_NGINX_ERROR_LOG_LEVEL' \
     bash -s" <<'REMOTE_DEPLOY_SCRIPT'
set -euo pipefail

remote_release_id="$(date -u +%Y%m%dT%H%M%SZ)-$MP_REMOTE_ENVIRONMENT_NAME"
remote_release_dir="$MP_REMOTE_DEPLOY_ROOT/releases/$remote_release_id"
remote_current_link="$MP_REMOTE_DEPLOY_ROOT/current"
remote_unit_file="/etc/systemd/system/mp-cache.service"
remote_nginx_conf_file="$MP_CACHE_NGINX_CONF_DIR/$MP_CACHE_NGINX_REMOTE_CONF_FILE_NAME"
remote_socket_dir="/run/mp-cache"
remote_socket_file="$remote_socket_dir/mp-cache.sock"
remote_nologin_shell="/usr/sbin/nologin"
remote_nginx_user=""

if [ ! -x "$remote_nologin_shell" ]; then
  remote_nologin_shell="/sbin/nologin"
fi

if [ -f "$MP_REMOTE_STAGING_DIR/$MP_REMOTE_ARCHIVE_NAME.sha256" ] && command -v sha256sum >/dev/null 2>&1; then
  (cd "$MP_REMOTE_STAGING_DIR" && sha256sum -c "$MP_REMOTE_ARCHIVE_NAME.sha256")
fi

if ! getent group "$MP_REMOTE_SERVICE_GROUP" >/dev/null 2>&1; then
  sudo groupadd --system "$MP_REMOTE_SERVICE_GROUP"
fi
if ! id "$MP_REMOTE_SERVICE_USER" >/dev/null 2>&1; then
  sudo useradd --system --no-create-home --shell "$remote_nologin_shell" --gid "$MP_REMOTE_SERVICE_GROUP" "$MP_REMOTE_SERVICE_USER"
fi

mkdir -p "$MP_REMOTE_STAGING_DIR/extract"
tar -xzf "$MP_REMOTE_STAGING_DIR/$MP_REMOTE_ARCHIVE_NAME" -C "$MP_REMOTE_STAGING_DIR/extract"
remote_extracted_dir="$(find "$MP_REMOTE_STAGING_DIR/extract" -mindepth 1 -maxdepth 1 -type d | head -n 1)"
[ -n "$remote_extracted_dir" ] || {
  printf 'artifact archive did not contain a top-level directory\n' >&2
  exit 1
}
[ -x "$remote_extracted_dir/bin/mp-cache-server" ] || {
  printf 'artifact archive is missing bin/mp-cache-server\n' >&2
  exit 1
}
[ -f "$remote_extracted_dir/configs/env/$MP_REMOTE_ENVIRONMENT_NAME.ini" ] || {
  printf 'artifact archive is missing configs/env/%s.ini\n' "$MP_REMOTE_ENVIRONMENT_NAME" >&2
  exit 1
}

sudo mkdir -p "$MP_REMOTE_DEPLOY_ROOT/releases"
sudo rm -rf "$remote_release_dir"
sudo cp -a "$remote_extracted_dir" "$remote_release_dir"
sudo chown -R root:root "$remote_release_dir"
sudo ln -sfn "$remote_release_dir" "$remote_current_link"

cat >"$MP_REMOTE_STAGING_DIR/mp-cache.service" <<EOF
[Unit]
Description=mp-cache $MP_REMOTE_ENVIRONMENT_NAME service
After=network-online.target
Wants=network-online.target
RequiresMountsFor=$MP_REMOTE_DEPLOY_ROOT /var/lib/mp-cache /var/log/mp-cache /run/secrets/mp-cache

[Service]
Type=simple
User=$MP_REMOTE_SERVICE_USER
Group=$MP_REMOTE_SERVICE_GROUP
WorkingDirectory=$remote_current_link
ExecStart=$remote_current_link/bin/mp-cache-server --config $remote_current_link/configs/env/$MP_REMOTE_ENVIRONMENT_NAME.ini
Restart=always
RestartSec=2s
NoNewPrivileges=true
PrivateTmp=true
ProtectSystem=strict
ProtectHome=true
ReadWritePaths=/var/lib/mp-cache /var/log/mp-cache $remote_socket_dir
RuntimeDirectory=mp-cache
RuntimeDirectoryMode=0750
StateDirectory=mp-cache
StateDirectoryMode=0750
LogsDirectory=mp-cache
LogsDirectoryMode=0750
LimitNOFILE=4096
UMask=0007

[Install]
WantedBy=multi-user.target
EOF

sudo install -m 0644 "$MP_REMOTE_STAGING_DIR/mp-cache.service" "$remote_unit_file"
sudo systemctl daemon-reload
sudo systemctl enable --now mp-cache.service
sudo systemctl restart mp-cache.service

if [ "$MP_REMOTE_INSTALL_NGINX" = "1" ]; then
  remote_nginx_user="$(awk '$1 == "user" { gsub(";", "", $2); print $2; exit }' "$MP_CACHE_NGINX_MAIN_CONFIG_FILE" 2>/dev/null || true)"
  if [ -n "$remote_nginx_user" ] && id "$remote_nginx_user" >/dev/null 2>&1; then
    sudo usermod -a -G "$MP_REMOTE_SERVICE_GROUP" "$remote_nginx_user"
  fi

  mkdir -p "$MP_REMOTE_STAGING_DIR/nginx"
  cat >"$MP_REMOTE_STAGING_DIR/nginx/mp-cache.conf" <<EOF
upstream mp_cache_upstream {
	server unix:$remote_socket_file;
}

server {
	listen $MP_REMOTE_NGINX_LISTEN;
	server_name _;

	access_log $MP_CACHE_NGINX_LOG_DIR/mp-cache.access.log;
	error_log $MP_CACHE_NGINX_LOG_DIR/mp-cache.error.log $MP_CACHE_NGINX_ERROR_LOG_LEVEL;

	location $MP_CACHE_NGINX_LOCATION_PATH {
		rewrite ^$MP_CACHE_NGINX_LOCATION_PATH/?(.*)$ /\$1 break;
		proxy_pass http://mp_cache_upstream;
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
  sudo install -m 0644 "$MP_REMOTE_STAGING_DIR/nginx/mp-cache.conf" "$remote_nginx_conf_file"
  sudo nginx -t
  if sudo systemctl is-active --quiet nginx; then
    sudo systemctl reload nginx
  else
    sudo systemctl enable --now nginx
  fi
fi

sudo systemctl --no-pager --full status mp-cache.service >/dev/null
rm -rf "$MP_REMOTE_STAGING_DIR"
printf 'remote deployment complete: %s -> %s\n' "$MP_REMOTE_ENVIRONMENT_NAME" "$remote_release_dir"
REMOTE_DEPLOY_SCRIPT
}

mp_remote_service_print_usage() {
  local remote_environment_name="$1"
  local remote_service_action="$2"

  cat <<EOF
Usage: ./scripts/$remote_service_action-$remote_environment_name.sh <ssh-target>

Runs systemctl $remote_service_action for mp-cache on the $remote_environment_name host.

Environment:
  MP_REMOTE_DEPLOY_SERVICE_NAME
      Remote systemd service name. Default: mp-cache.
EOF
}

mp_remote_service_run() {
  local remote_environment_name="$1"
  local remote_service_action="$2"
  shift 2

  local remote_ssh_target="${1:-}"
  local remote_service_name="${MP_REMOTE_DEPLOY_SERVICE_NAME:-mp-cache}"

  if [ "$remote_ssh_target" = "--help" ] || [ -z "$remote_ssh_target" ]; then
    mp_remote_service_print_usage "$remote_environment_name" "$remote_service_action"
    exit 0
  fi

  command -v ssh >/dev/null 2>&1 || {
    printf 'missing required command: ssh\n' >&2
    exit 1
  }

  case "$remote_service_action" in
    stop|restart)
      ;;
    *)
      printf 'unsupported remote service action: %s\n' "$remote_service_action" >&2
      exit 1
      ;;
  esac

  ssh "$remote_ssh_target" \
    "MP_REMOTE_SERVICE_ACTION='$remote_service_action' \
     MP_REMOTE_SERVICE_NAME='$remote_service_name' \
     bash -s" <<'REMOTE_SERVICE_SCRIPT'
set -euo pipefail

case "$MP_REMOTE_SERVICE_ACTION" in
  stop)
    sudo systemctl stop "$MP_REMOTE_SERVICE_NAME.service"
    if sudo systemctl is-active --quiet "$MP_REMOTE_SERVICE_NAME.service"; then
      printf 'remote service is still active after stop: %s.service\n' "$MP_REMOTE_SERVICE_NAME" >&2
      exit 1
    fi
    ;;
  restart)
    sudo systemctl restart "$MP_REMOTE_SERVICE_NAME.service"
    sudo systemctl --no-pager --full status "$MP_REMOTE_SERVICE_NAME.service" >/dev/null
    ;;
  *)
    printf 'unsupported remote service action: %s\n' "$MP_REMOTE_SERVICE_ACTION" >&2
    exit 1
    ;;
esac

printf 'remote service %s complete: %s.service\n' "$MP_REMOTE_SERVICE_ACTION" "$MP_REMOTE_SERVICE_NAME"
REMOTE_SERVICE_SCRIPT
}
