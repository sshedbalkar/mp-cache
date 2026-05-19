#!/usr/bin/env bash
set -euo pipefail

# Runs post-deployment smoke tests against the local systemd and Nginx deployment.
#
# Usage examples:
#   ./scripts/test-local-deployment.sh
#   MP_LOCAL_DEPLOY_NGINX_LISTEN=127.0.0.1:18080 ./scripts/test-local-deployment.sh

if [ "${1:-}" = "--help" ]; then
  cat <<'EOF'
Usage: ./scripts/test-local-deployment.sh

Runs post-deployment smoke tests against the local systemd and Nginx deployment.

Environment:
  MP_LOCAL_DEPLOY_NGINX_LISTEN
      Local Nginx listen address used during deploy. Default: 127.0.0.1:8080.
  MP_LOCAL_DEPLOY_SERVICE_NAME
      Local systemd unit and runtime directory name. Default: mp-cache-local.
  MP_LOCAL_POST_DEPLOY_REPORT_DIR
      Directory for post-deployment reports. Default: .tmp/deploy/local/reports.
  MP_NGINX_PATH_CONFIG_FILE
      Nginx path constants file. Default: configs/deploy/nginx-paths.env.
EOF
  exit 0
fi

cd "$(dirname "$0")/.."
. ./scripts/lib/local-env.sh
. ./scripts/lib/nginx-env.sh

mp_local_post_deploy_service_name="${MP_LOCAL_DEPLOY_SERVICE_NAME:-mp-cache-local}"
mp_local_post_deploy_nginx_listen="${MP_LOCAL_DEPLOY_NGINX_LISTEN:-127.0.0.1:8080}"
mp_local_post_deploy_generated_dir="$MP_REPO_ROOT/.tmp/deploy/local"
mp_local_post_deploy_report_dir="${MP_LOCAL_POST_DEPLOY_REPORT_DIR:-$mp_local_post_deploy_generated_dir/reports}"
mp_local_post_deploy_report_path="$mp_local_post_deploy_report_dir/post-deployment-test-report.md"
mp_local_post_deploy_run_id="post-deploy-$(date -u +%Y%m%dT%H%M%SZ)-$$"
mp_local_post_deploy_response_dir="$mp_local_post_deploy_report_dir/$mp_local_post_deploy_run_id-responses"
mp_local_post_deploy_started_at_utc="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
mp_local_post_deploy_current_step="initializing"
mp_local_post_deploy_step_names=()
mp_local_post_deploy_step_statuses=()
mp_local_post_deploy_step_details=()
mp_local_post_deploy_health_route=""
mp_local_post_deploy_cache_route_prefix=""
mp_local_post_deploy_stats_route=""
mp_local_post_deploy_clients_route=""
mp_local_post_deploy_clients_route_prefix=""
mp_local_post_deploy_client_invalidate_route_suffix=""

mp_local_post_deploy_url() {
  local route_path="$1"
  local location_prefix="$MP_CACHE_NGINX_LOCATION_PATH"

  case "$location_prefix" in
    ""|"/")
      location_prefix=""
      ;;
  esac

  printf 'http://%s%s%s\n' "$mp_local_post_deploy_nginx_listen" "$location_prefix" "$route_path"
}

mp_local_post_deploy_begin_step() {
  mp_local_post_deploy_current_step="$1"
}

mp_local_post_deploy_record_step() {
  mp_local_post_deploy_step_names+=("$1")
  mp_local_post_deploy_step_statuses+=("$2")
  mp_local_post_deploy_step_details+=("$3")
}

mp_local_post_deploy_pass_step() {
  mp_local_post_deploy_record_step "$mp_local_post_deploy_current_step" "PASS" "$1"
  mp_local_post_deploy_current_step=""
}

mp_local_post_deploy_write_report() {
  local exit_code="$1"
  local deployment_test_status="PASS"
  local completed_at_utc=""
  local step_index=0
  local report_tmp_path=""

  completed_at_utc="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  if [ "$exit_code" -ne 0 ]; then
    deployment_test_status="FAIL"
  fi

  mkdir -p "$mp_local_post_deploy_report_dir" 2>/dev/null || return 0
  report_tmp_path="$mp_local_post_deploy_report_path.tmp"

  {
    printf '# Post-Deployment Test Report\n\n'
    printf '| Field | Value |\n'
    printf '|:------|:------|\n'
    printf '| Status | %s |\n' "$deployment_test_status"
    printf '| Started at UTC | %s |\n' "$mp_local_post_deploy_started_at_utc"
    printf '| Completed at UTC | %s |\n' "$completed_at_utc"
    printf '| Service | `%s.service` |\n' "$mp_local_post_deploy_service_name"
    printf '| Proxy URL | `%s` |\n' "$(mp_local_post_deploy_url "")"
    printf '| Response directory | `%s` |\n\n' "$mp_local_post_deploy_response_dir"

    printf '## Checks\n\n'
    printf '| Check | Status | Detail |\n'
    printf '|:------|:-------|:-------|\n'
    while [ "$step_index" -lt "${#mp_local_post_deploy_step_names[@]}" ]; do
      printf '| %s | %s | %s |\n' \
      "${mp_local_post_deploy_step_names[$step_index]}" \
      "${mp_local_post_deploy_step_statuses[$step_index]}" \
      "${mp_local_post_deploy_step_details[$step_index]}"
      step_index=$((step_index + 1))
    done
    if [ "$deployment_test_status" = "FAIL" ] && [ -n "$mp_local_post_deploy_current_step" ]; then
      printf '| %s | FAIL | inspect the console output and captured responses |\n' "$mp_local_post_deploy_current_step"
    fi
  } >"$report_tmp_path"
  mv "$report_tmp_path" "$mp_local_post_deploy_report_path"
  printf 'post-deployment test report: %s\n' "$mp_local_post_deploy_report_path" >&2
}

mp_local_post_deploy_expect_code() {
  local actual_code="$1"
  local expected_code="$2"
  local response_path="$3"

  if [ "$actual_code" != "$expected_code" ]; then
    printf 'expected HTTP %s but got %s for %s\n' "$expected_code" "$actual_code" "$response_path" >&2
    [ -f "$response_path" ] && sed -n '1,80p' "$response_path" >&2
    return 1
  fi
}

mp_local_post_deploy_request() {
  local method="$1"
  local route_path="$2"
  local response_path="$3"
  local expected_code="$4"
  local http_code=""
  shift 4

  if ! http_code="$(
    curl --silent --show-error \
      --output "$response_path" \
      --write-out '%{http_code}' \
      -X "$method" \
      "$@" \
      "$(mp_local_post_deploy_url "$route_path")"
  )"; then
    printf 'curl failed for %s %s\n' "$method" "$route_path" >&2
    return 1
  fi

  mp_local_post_deploy_expect_code "$http_code" "$expected_code" "$response_path"
}

trap 'mp_local_post_deploy_write_report "$?"' EXIT

mp_local_post_deploy_begin_step "check required commands"
mp_require_command curl
mp_require_command jq
mp_require_command systemctl
mp_local_post_deploy_pass_step "curl, jq, and systemctl are available"

mp_local_post_deploy_begin_step "load HTTP route constants"
mp_local_post_deploy_health_route="$(mp_read_c_string_constant MP_CACHE_HTTP_ROUTE_HEALTH)"
mp_local_post_deploy_cache_route_prefix="$(mp_read_c_string_constant MP_CACHE_HTTP_ROUTE_CACHE_PREFIX)"
mp_local_post_deploy_stats_route="$(mp_read_c_string_constant MP_CACHE_HTTP_ROUTE_STATS)"
mp_local_post_deploy_clients_route="$(mp_read_c_string_constant MP_CACHE_HTTP_ROUTE_CLIENTS)"
mp_local_post_deploy_clients_route_prefix="$(mp_read_c_string_constant MP_CACHE_HTTP_ROUTE_CLIENTS_PREFIX)"
mp_local_post_deploy_client_invalidate_route_suffix="$(mp_read_c_string_constant MP_CACHE_HTTP_ROUTE_CLIENT_INVALIDATE_SUFFIX)"
mp_local_post_deploy_pass_step "route constants loaded from internal/config/constants.h"

mp_local_post_deploy_begin_step "prepare response directory"
mkdir -p "$mp_local_post_deploy_response_dir"
mp_local_post_deploy_pass_step "response artifacts will be stored under $mp_local_post_deploy_response_dir"

mp_local_post_deploy_begin_step "load local secrets"
mp_load_local_secrets
[ -n "${MP_SECRET_LOCAL_BOOTSTRAP_ADMIN_TOKEN:-}" ] ||
  mp_exit_with_error "missing MP_SECRET_LOCAL_BOOTSTRAP_ADMIN_TOKEN in $MP_LOCAL_SECRET_ENV_FILE; run ./scripts/write-local-secret-env.sh"
mp_local_post_deploy_pass_step "bootstrap admin token is available"

mp_local_post_deploy_begin_step "verify systemd service is active"
systemctl is-active --quiet "$mp_local_post_deploy_service_name.service"
mp_local_post_deploy_pass_step "systemd reports the service as active"

mp_local_post_deploy_begin_step "verify proxied v1 health"
mp_local_post_deploy_request "GET" "$mp_local_post_deploy_health_route" "$mp_local_post_deploy_response_dir/v1-health.json" "200"
jq -e '.status=="ok" and .service=="mp-cache"' "$mp_local_post_deploy_response_dir/v1-health.json" >/dev/null
mp_local_post_deploy_pass_step "health route returned the expected service payload"

mp_local_post_deploy_begin_step "register smoke-test client"
mp_local_post_deploy_client_id="$mp_local_post_deploy_run_id-client"
mp_local_post_deploy_cache_key="$mp_local_post_deploy_run_id-key"
mp_local_post_deploy_request \
  "POST" \
  "$mp_local_post_deploy_clients_route" \
  "$mp_local_post_deploy_response_dir/register-client.json" \
  "200" \
  -H "Authorization: Bearer $MP_SECRET_LOCAL_BOOTSTRAP_ADMIN_TOKEN" \
  -H "Content-Type: application/json" \
  --data "{\"client_id\":\"$mp_local_post_deploy_client_id\",\"role\":\"client\"}"
jq -e --arg client_id "$mp_local_post_deploy_client_id" '.client_id==$client_id and .role=="client" and (.token | length > 0)' \
  "$mp_local_post_deploy_response_dir/register-client.json" >/dev/null
mp_local_post_deploy_client_token="$(jq -r '.token' "$mp_local_post_deploy_response_dir/register-client.json")"
mp_local_post_deploy_pass_step "registered $mp_local_post_deploy_client_id"

mp_local_post_deploy_begin_step "write cache value through proxy"
mp_local_post_deploy_request \
  "PUT" \
  "$mp_local_post_deploy_cache_route_prefix$mp_local_post_deploy_cache_key" \
  "$mp_local_post_deploy_response_dir/cache-put.json" \
  "200" \
  -H "Authorization: Bearer $mp_local_post_deploy_client_token" \
  -H "Content-Type: application/json" \
  --data '{"value_base64":"cG9zdC1kZXBsb3k=","ttl_seconds":60}'
mp_local_post_deploy_pass_step "cache write route accepted the smoke value"

mp_local_post_deploy_begin_step "read cache value through proxy"
mp_local_post_deploy_request \
  "GET" \
  "$mp_local_post_deploy_cache_route_prefix$mp_local_post_deploy_cache_key" \
  "$mp_local_post_deploy_response_dir/cache-get.json" \
  "200" \
  -H "Authorization: Bearer $mp_local_post_deploy_client_token"
jq -e --arg cache_key "$mp_local_post_deploy_cache_key" '.key==$cache_key and .value_base64=="cG9zdC1kZXBsb3k="' \
  "$mp_local_post_deploy_response_dir/cache-get.json" >/dev/null
mp_local_post_deploy_pass_step "cache read route returned the smoke value"

mp_local_post_deploy_begin_step "read operator stats as admin"
mp_local_post_deploy_request \
  "GET" \
  "$mp_local_post_deploy_stats_route" \
  "$mp_local_post_deploy_response_dir/stats.json" \
  "200" \
  -H "Authorization: Bearer $MP_SECRET_LOCAL_BOOTSTRAP_ADMIN_TOKEN"
jq -e '.memory_limit_bytes > 0 and .uptime_seconds >= 0' "$mp_local_post_deploy_response_dir/stats.json" >/dev/null
mp_local_post_deploy_pass_step "stats route returned operational counters"

mp_local_post_deploy_begin_step "delete smoke cache value"
mp_local_post_deploy_request \
  "DELETE" \
  "$mp_local_post_deploy_cache_route_prefix$mp_local_post_deploy_cache_key" \
  "$mp_local_post_deploy_response_dir/cache-delete.json" \
  "200" \
  -H "Authorization: Bearer $mp_local_post_deploy_client_token"
mp_local_post_deploy_pass_step "cache delete route removed the smoke value"

mp_local_post_deploy_begin_step "invalidate smoke-test client token"
mp_local_post_deploy_request \
  "POST" \
  "$mp_local_post_deploy_clients_route_prefix$mp_local_post_deploy_client_id$mp_local_post_deploy_client_invalidate_route_suffix" \
  "$mp_local_post_deploy_response_dir/invalidate-client.json" \
  "200" \
  -H "Authorization: Bearer $MP_SECRET_LOCAL_BOOTSTRAP_ADMIN_TOKEN"
mp_local_post_deploy_pass_step "admin invalidated the smoke client token"

mp_local_post_deploy_begin_step "verify invalidated token is rejected"
mp_local_post_deploy_request \
  "GET" \
  "$mp_local_post_deploy_cache_route_prefix$mp_local_post_deploy_cache_key" \
  "$mp_local_post_deploy_response_dir/cache-get-invalidated-token.json" \
  "401" \
  -H "Authorization: Bearer $mp_local_post_deploy_client_token"
jq -e '.error_code=="unauthorized"' "$mp_local_post_deploy_response_dir/cache-get-invalidated-token.json" >/dev/null
mp_local_post_deploy_pass_step "invalidated client token is rejected"

printf 'post-deployment smoke tests passed: %s\n' "$mp_local_post_deploy_report_path"
