# Runbook: Manual API Testing With Scripted Local Deploy

## 1. Purpose

Use this runbook when you want the **fast path** for manual API testing on a real local host.

This guide is for the situation where:

- you want the local install, secret creation, build, and deployment work handled by scripts
- you want to manually call the HTTP APIs yourself
- you do **not** need the fully isolated manual test workspace from the longer runbook

This runbook is intentionally written for novice developers. It assumes you are comfortable copying and pasting shell commands, but it does not assume you already know `mp-cache`.

## 2. When To Use This Runbook

Use **this** runbook when you want:

- the default local config path
- the default local socket path
- the default local `.tmp/run`, `.tmp/data`, and `.tmp/exports` paths
- a quick manual API pass after scripted setup

Use `docs/runbooks/manual-end-to-end-testing.md` instead when you want:

- a fully isolated manual test workspace under `.tmp/manual-e2e/`
- a custom test-only config file
- file-based secret testing
- explicit wrong-method, conflict, size-limit, tamper, rate-limit, and restart-persistence checks
- the most exhaustive host-side manual validation path

In short:

- `docs/runbooks/manual-api-testing-with-scripted-local-deploy.md` = faster local script-driven setup, then manual API calls
- `docs/runbooks/manual-end-to-end-testing.md` = slower but more complete and more isolated

## 3. What This Runbook Reuses

This runbook intentionally reuses existing project scripts instead of re-documenting their logic:

- `./scripts/install-local-deps.sh`
- `./scripts/write-local-secret-env.sh`
- `./scripts/deploy-local.sh`
- `./scripts/stop-local-server.sh`
- `./scripts/doctor-local.sh`

If any of those scripts change in the future, this runbook should stay aligned automatically because it tells you to call the scripts directly instead of copying their internal steps here.

## 4. Before You Start

### Host expectations

- a normal local Linux shell
- a local checkout of `multi-player-app`
- permission to write inside the repository

This path is validated for Arch Linux / CachyOS style local development.

### Tools

This runbook uses:

- `bash`
- `curl`
- `jq`

The scripted install step below also installs the required build tools.

### Important difference from the full runbook

This fast path uses the default local deployment layout:

- config path: `configs/bootstrap.ini`
- socket path: `.tmp/run/mp-cache.sock`
- pid file: `.tmp/run/mp-cache.pid`
- data path: `.tmp/data/`
- export path: `.tmp/exports/`

That is faster, but it is less isolated than `docs/runbooks/manual-end-to-end-testing.md`.

## 5. Open A Fresh Shell And Move Into The Repo

Start with a fresh shell so the exported variables in this runbook are easy to track.

```sh
cd /path/to/your/multi-player-app/mp-cache
export MP_CACHE_REPO="$(pwd)"
```

If you are not sure you are in the right place, run:

```sh
pwd
test -f README.md
test -d scripts
test -d docs
```

Those checks should finish without error.

## 6. Run The Scripted Local Setup And Deploy

Run this whole block in the same shell.

This block intentionally uses `set -eo pipefail`, not `set -u`. In some interactive Bash setups, `set -u` can break prompt code and cause errors such as `bash: status_str: unbound variable`.

```sh
cd "$MP_CACHE_REPO"

set -eo pipefail

expect_code() {
  actual="$1"
  expected="$2"

  if [ "$actual" != "$expected" ]; then
    printf 'Expected HTTP %s but got %s\n' "$expected" "$actual" >&2
    return 1
  fi
}

mkdir -p .tmp/manual-api/responses

./scripts/install-local-deps.sh --dry-run --with-jq
./scripts/install-local-deps.sh --with-jq

./scripts/write-local-secret-env.sh

set -a
. ./.tmp/secrets/local.env
set +a

export MP_SOCKET_PATH="$MP_CACHE_REPO/.tmp/run/mp-cache.sock"
export MP_PID_FILE="$MP_CACHE_REPO/.tmp/run/mp-cache.pid"
export RESP_DIR="$MP_CACHE_REPO/.tmp/manual-api/responses"
export RUN_TAG="rapid-$(date +%s)"
export CLIENT_ID="${RUN_TAG}-client"
export OPERATOR_ID="${RUN_TAG}-operator"
export MANUAL_ADMIN_ID="${RUN_TAG}-admin"
export ADMIN_AUTH_HEADER="Authorization: Bearer $MP_SECRET_LOCAL_BOOTSTRAP_ADMIN_TOKEN"

./scripts/deploy-local.sh
```

### What this block did

- installed the local packages needed for build plus `jq`
- created `.tmp/secrets/local.env` if it did not already contain usable values
- loaded those secret values into your current shell
- set the socket path you will use for the manual API calls
- built and started the default local server

### Important note about local secrets

`./scripts/write-local-secret-env.sh` is intentionally conservative:

- if `.tmp/secrets/local.env` already contains real values, it reuses them
- if the file is still a placeholder or incomplete, it replaces it
- if you want fresh local secrets on purpose, run `./scripts/write-local-secret-env.sh --force`

Reusing the existing local secrets is usually the safest choice because your current `.tmp/data/state.checkpoint` and `.tmp/data/state.journal` may already be encrypted with the current storage key.

## 7. Preflight: Confirm The Service Is Up

### 7.1 Check `GET /health`

```sh
http_code="$(
  curl --silent --show-error \
    --output "$RESP_DIR/health.json" \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    http://localhost/health
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 200
jq -e '.status=="ok" and .service=="mp-cache"' "$RESP_DIR/health.json"
```

Expected HTTP code: `200`

### 7.2 Check `GET /v1/health`

```sh
http_code="$(
  curl --silent --show-error \
    --output "$RESP_DIR/health-v1.json" \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    http://localhost/v1/health
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 200
jq -e '.status=="ok" and .socket_path==".tmp/run/mp-cache.sock"' "$RESP_DIR/health-v1.json"
```

Expected HTTP code: `200`

### 7.3 If startup failed

If `./scripts/deploy-local.sh` failed, stop here and run:

```sh
./scripts/doctor-local.sh
```

Then use `docs/runbooks/local-service-lifecycle.md` for diagnosis.

## 8. Create Manual Test Principals

You need three role types for a meaningful API pass:

- one `client`
- one `operator`
- one persisted `admin`

You will create all three with the bootstrap admin token from `.tmp/secrets/local.env`.

### 8.1 Register a client

```sh
http_code="$(
  curl --silent --show-error \
    --output "$RESP_DIR/register-client.json" \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$ADMIN_AUTH_HEADER" \
    -H 'Content-Type: application/json' \
    -X POST \
    http://localhost/v1/clients \
    --data "{\"client_id\":\"${CLIENT_ID}\",\"role\":\"client\"}"
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 200
jq -e --arg client_id "$CLIENT_ID" '.client_id==$client_id and .role=="client" and (.token | length > 0)' "$RESP_DIR/register-client.json"

export CLIENT_TOKEN="$(jq -r '.token' "$RESP_DIR/register-client.json")"
export CLIENT_AUTH_HEADER="Authorization: Bearer $CLIENT_TOKEN"
```

Expected HTTP code: `200`

### 8.2 Register an operator

```sh
http_code="$(
  curl --silent --show-error \
    --output "$RESP_DIR/register-operator.json" \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$ADMIN_AUTH_HEADER" \
    -H 'Content-Type: application/json' \
    -X POST \
    http://localhost/v1/clients \
    --data "{\"client_id\":\"${OPERATOR_ID}\",\"role\":\"operator\"}"
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 200
jq -e --arg client_id "$OPERATOR_ID" '.client_id==$client_id and .role=="operator" and (.token | length > 0)' "$RESP_DIR/register-operator.json"

export OPERATOR_TOKEN="$(jq -r '.token' "$RESP_DIR/register-operator.json")"
export OPERATOR_AUTH_HEADER="Authorization: Bearer $OPERATOR_TOKEN"
```

Expected HTTP code: `200`

### 8.3 Register a second admin

```sh
http_code="$(
  curl --silent --show-error \
    --output "$RESP_DIR/register-admin.json" \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$ADMIN_AUTH_HEADER" \
    -H 'Content-Type: application/json' \
    -X POST \
    http://localhost/v1/clients \
    --data "{\"client_id\":\"${MANUAL_ADMIN_ID}\",\"role\":\"admin\"}"
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 200
jq -e --arg client_id "$MANUAL_ADMIN_ID" '.client_id==$client_id and .role=="admin" and (.token | length > 0)' "$RESP_DIR/register-admin.json"

export MANUAL_ADMIN_TOKEN="$(jq -r '.token' "$RESP_DIR/register-admin.json")"
export MANUAL_ADMIN_AUTH_HEADER="Authorization: Bearer $MANUAL_ADMIN_TOKEN"
```

Expected HTTP code: `200`

## 9. Manually Test The Client APIs

These calls use the `client` token.

### 9.1 `PUT /v1/cache/alpha`

```sh
http_code="$(
  curl --silent --show-error \
    --output "$RESP_DIR/cache-put-alpha.json" \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$CLIENT_AUTH_HEADER" \
    -H 'Content-Type: application/json' \
    -X PUT \
    http://localhost/v1/cache/alpha \
    --data '{"value_base64":"aGVsbG8=","ttl_seconds":60}'
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 200
jq -e '.key=="alpha" and .ttl_seconds==60' "$RESP_DIR/cache-put-alpha.json"
```

Expected HTTP code: `200`

### 9.2 `GET /v1/cache/alpha`

```sh
http_code="$(
  curl --silent --show-error \
    --output "$RESP_DIR/cache-get-alpha.json" \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$CLIENT_AUTH_HEADER" \
    http://localhost/v1/cache/alpha
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 200
jq -e '.key=="alpha" and .value_base64=="aGVsbG8="' "$RESP_DIR/cache-get-alpha.json"
```

Expected HTTP code: `200`

### 9.3 `PUT /v1/cache/beta`

This key will be useful later for export, purge, and import checks.

```sh
http_code="$(
  curl --silent --show-error \
    --output "$RESP_DIR/cache-put-beta.json" \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$CLIENT_AUTH_HEADER" \
    -H 'Content-Type: application/json' \
    -X PUT \
    http://localhost/v1/cache/beta \
    --data '{"value_base64":"d29ybGQ="}'
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 200
jq -e '.key=="beta" and .ttl_seconds==172800' "$RESP_DIR/cache-put-beta.json"
```

Expected HTTP code: `200`

### 9.4 `DELETE /v1/cache/alpha`

```sh
http_code="$(
  curl --silent --show-error \
    --output "$RESP_DIR/cache-delete-alpha.json" \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$CLIENT_AUTH_HEADER" \
    -X DELETE \
    http://localhost/v1/cache/alpha
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 200
jq -e '.key=="alpha" and .deleted==true' "$RESP_DIR/cache-delete-alpha.json"
```

Expected HTTP code: `200`

## 10. Manually Test A Few Auth Boundaries

This runbook is intentionally fast, so it only does two quick auth checks. The full runbook covers more negative cases.

### 10.1 Missing bearer token on an operator route

```sh
http_code="$(
  curl --silent --show-error \
    --output "$RESP_DIR/stats-missing-token.json" \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    http://localhost/v1/stats
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 401
jq -e '.error_code=="unauthorized" and (.error_description | type=="string" and length > 0)' "$RESP_DIR/stats-missing-token.json"
```

Expected HTTP code: `401`

### 10.2 Client token on an operator-only route

```sh
http_code="$(
  curl --silent --show-error \
    --output "$RESP_DIR/stats-client-forbidden.json" \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$CLIENT_AUTH_HEADER" \
    http://localhost/v1/stats
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 403
jq -e '.error_code=="forbidden" and (.error_description | type=="string" and length > 0)' "$RESP_DIR/stats-client-forbidden.json"
```

Expected HTTP code: `403`

## 11. Manually Test The Operator APIs

These calls use the `operator` token.

### 11.1 `GET /v1/stats`

```sh
http_code="$(
  curl --silent --show-error \
    --output "$RESP_DIR/stats-operator.json" \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$OPERATOR_AUTH_HEADER" \
    http://localhost/v1/stats
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 200
jq -e '.client_registrations >= 3 and .cache_sets >= 2' "$RESP_DIR/stats-operator.json"
```

Expected HTTP code: `200`

### 11.2 `GET /v1/metrics/memory`

```sh
http_code="$(
  curl --silent --show-error \
    --output "$RESP_DIR/memory-operator.json" \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$OPERATOR_AUTH_HEADER" \
    http://localhost/v1/metrics/memory
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 200
jq -e '.memory_limit_bytes > 0' "$RESP_DIR/memory-operator.json"
```

Expected HTTP code: `200`

### 11.3 `GET /v1/uptime`

```sh
http_code="$(
  curl --silent --show-error \
    --output "$RESP_DIR/uptime-operator.json" \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$OPERATOR_AUTH_HEADER" \
    http://localhost/v1/uptime
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 200
jq -e '.uptime_seconds >= 0 and .started_at_utc_seconds > 0' "$RESP_DIR/uptime-operator.json"
```

Expected HTTP code: `200`

### 11.4 `GET /v1/logs?tail=20`

```sh
http_code="$(
  curl --silent --show-error \
    --output "$RESP_DIR/logs-operator.json" \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$OPERATOR_AUTH_HEADER" \
    'http://localhost/v1/logs?tail=20'
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 200
jq -e '.tail_lines == 20 and (.text | length) > 0' "$RESP_DIR/logs-operator.json"
```

Expected HTTP code: `200`

## 12. Manually Test The Admin APIs

These calls use the persisted second admin. That proves the admin-only APIs work for stored admin principals, not only for the bootstrap token.

### 12.1 `PUT /v1/cache/gamma`

Store one more key so selective purge has a live target.

```sh
http_code="$(
  curl --silent --show-error \
    --output "$RESP_DIR/cache-put-gamma.json" \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$CLIENT_AUTH_HEADER" \
    -H 'Content-Type: application/json' \
    -X PUT \
    http://localhost/v1/cache/gamma \
    --data '{"value_base64":"Z2FtbWE=","ttl_seconds":60}'
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 200
jq -e '.key=="gamma" and .ttl_seconds==60' "$RESP_DIR/cache-put-gamma.json"
```

Expected HTTP code: `200`

### 12.2 `POST /v1/purge/keys`

```sh
http_code="$(
  curl --silent --show-error \
    --output "$RESP_DIR/purge-selected.json" \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$MANUAL_ADMIN_AUTH_HEADER" \
    -H 'Content-Type: application/json' \
    -X POST \
    http://localhost/v1/purge/keys \
    --data '{"keys":["gamma","missing-key"]}'
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 200
jq -e '.requested_keys==2 and .purged_keys==1 and .missing_keys==1' "$RESP_DIR/purge-selected.json"
```

Expected HTTP code: `200`

### 12.3 `POST /v1/export`

```sh
http_code="$(
  curl --silent --show-error \
    --output "$RESP_DIR/export-admin.json" \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$MANUAL_ADMIN_AUTH_HEADER" \
    -H 'Content-Type: application/json' \
    -X POST \
    http://localhost/v1/export \
    --data '{}'
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 200
jq -e '.entry_count >= 1 and .client_count >= 3 and (.path | length > 0)' "$RESP_DIR/export-admin.json"

export EXPORT_PATH="$(jq -r '.path' "$RESP_DIR/export-admin.json")"
```

Expected HTTP code: `200`

### 12.4 `POST /v1/clients/{client_id}/rotate-token`

```sh
export OLD_CLIENT_TOKEN="$CLIENT_TOKEN"

http_code="$(
  curl --silent --show-error \
    --output "$RESP_DIR/rotate-client-token.json" \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$MANUAL_ADMIN_AUTH_HEADER" \
    -H 'Content-Type: application/json' \
    -X POST \
    "http://localhost/v1/clients/${CLIENT_ID}/rotate-token" \
    --data '{}'
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 200
jq -e --arg client_id "$CLIENT_ID" '.client_id==$client_id and (.token | length > 0)' "$RESP_DIR/rotate-client-token.json"

export CLIENT_TOKEN="$(jq -r '.token' "$RESP_DIR/rotate-client-token.json")"
export CLIENT_AUTH_HEADER="Authorization: Bearer $CLIENT_TOKEN"
```

Expected HTTP code: `200`

### 12.5 Confirm the old client token no longer works

```sh
http_code="$(
  curl --silent --show-error \
    --output "$RESP_DIR/cache-get-beta-old-token.json" \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "Authorization: Bearer $OLD_CLIENT_TOKEN" \
    http://localhost/v1/cache/beta
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 401
jq -e '.error_code=="unauthorized" and (.error_description | type=="string" and length > 0)' "$RESP_DIR/cache-get-beta-old-token.json"
```

Expected HTTP code: `401`

### 12.6 `POST /v1/clients/{client_id}/invalidate-token`

```sh
export INVALIDATED_CLIENT_TOKEN="$CLIENT_TOKEN"

http_code="$(
  curl --silent --show-error \
    --output "$RESP_DIR/invalidate-client-token.json" \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$MANUAL_ADMIN_AUTH_HEADER" \
    -H 'Content-Type: application/json' \
    -X POST \
    "http://localhost/v1/clients/${CLIENT_ID}/invalidate-token" \
    --data '{}'
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 200
jq -e --arg client_id "$CLIENT_ID" '.client_id==$client_id and .token_active==false' "$RESP_DIR/invalidate-client-token.json"
```

Expected HTTP code: `200`

### 12.7 Confirm the invalidated token no longer works

```sh
http_code="$(
  curl --silent --show-error \
    --output "$RESP_DIR/cache-get-beta-invalidated-token.json" \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "Authorization: Bearer $INVALIDATED_CLIENT_TOKEN" \
    http://localhost/v1/cache/beta
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 401
jq -e '.error_code=="unauthorized" and (.error_description | type=="string" and length > 0)' "$RESP_DIR/cache-get-beta-invalidated-token.json"
```

Expected HTTP code: `401`

### 12.8 Rotate again so the client has a fresh working token

```sh
http_code="$(
  curl --silent --show-error \
    --output "$RESP_DIR/rotate-client-token-reissued.json" \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$MANUAL_ADMIN_AUTH_HEADER" \
    -H 'Content-Type: application/json' \
    -X POST \
    "http://localhost/v1/clients/${CLIENT_ID}/rotate-token" \
    --data '{}'
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 200
jq -e --arg client_id "$CLIENT_ID" '.client_id==$client_id and (.token | length > 0)' "$RESP_DIR/rotate-client-token-reissued.json"

export CLIENT_TOKEN="$(jq -r '.token' "$RESP_DIR/rotate-client-token-reissued.json")"
export CLIENT_AUTH_HEADER="Authorization: Bearer $CLIENT_TOKEN"
```

Expected HTTP code: `200`

### 12.9 Confirm the reissued client token works

```sh
http_code="$(
  curl --silent --show-error \
    --output "$RESP_DIR/cache-get-beta-reissued-token.json" \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$CLIENT_AUTH_HEADER" \
    http://localhost/v1/cache/beta
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 200
jq -e '.key=="beta" and .value_base64=="d29ybGQ="' "$RESP_DIR/cache-get-beta-reissued-token.json"
```

Expected HTTP code: `200`

### 12.10 `POST /v1/purge/all`

```sh
http_code="$(
  curl --silent --show-error \
    --output "$RESP_DIR/purge-all.json" \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$MANUAL_ADMIN_AUTH_HEADER" \
    -H 'Content-Type: application/json' \
    -X POST \
    http://localhost/v1/purge/all \
    --data '{}'
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 200
jq -e '.purged==true' "$RESP_DIR/purge-all.json"
```

Expected HTTP code: `200`

### 12.11 Confirm `beta` is gone after purge-all

```sh
http_code="$(
  curl --silent --show-error \
    --output "$RESP_DIR/cache-get-beta-after-purge.json" \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$CLIENT_AUTH_HEADER" \
    http://localhost/v1/cache/beta
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 404
jq -e '.error_code=="not_found" and (.error_description | type=="string" and length > 0)' "$RESP_DIR/cache-get-beta-after-purge.json"
```

Expected HTTP code: `404`

### 12.12 `POST /v1/import`

```sh
http_code="$(
  curl --silent --show-error \
    --output "$RESP_DIR/import-valid.json" \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$MANUAL_ADMIN_AUTH_HEADER" \
    -H 'Content-Type: application/json' \
    -X POST \
    http://localhost/v1/import \
    --data "{\"path\":\"${EXPORT_PATH}\"}"
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 200
jq -e --arg path "$EXPORT_PATH" '.imported==true and .path==$path' "$RESP_DIR/import-valid.json"
```

Expected HTTP code: `200`

### 12.13 Confirm `beta` is back after import

```sh
http_code="$(
  curl --silent --show-error \
    --output "$RESP_DIR/cache-get-beta-after-import.json" \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$CLIENT_AUTH_HEADER" \
    http://localhost/v1/cache/beta
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 200
jq -e '.key=="beta" and .value_base64=="d29ybGQ="' "$RESP_DIR/cache-get-beta-after-import.json"
```

Expected HTTP code: `200`

## 13. What This Fast Runbook Does Not Cover

This runbook is intentionally shorter than `docs/runbooks/manual-end-to-end-testing.md`.

It does **not** try to fully cover:

- isolated test-only config creation
- file-based secret resolution
- duplicate-client conflict checks
- invalid-role checks
- invalid Base64 checks
- size-limit checks
- tampered import rejection
- rate-limit boundary checks
- persistence across restart
- manual cleanup of isolated `.tmp/manual-e2e/` state

If you need those checks, switch to `docs/runbooks/manual-end-to-end-testing.md`.

## 14. Cleanup

### 14.1 Stop the default local server

```sh
./scripts/stop-local-server.sh
```

### 14.2 Remove only the response files created by this runbook

```sh
rm -rf .tmp/manual-api
```

This runbook does **not** automatically remove:

- `.tmp/secrets/local.env`
- `.tmp/data/`
- `.tmp/exports/`

That is intentional, because those paths belong to the default local deployment flow.

## 15. Expected Outcome

If every step above passed, you confirmed that the fast local script-driven path works and that you can manually exercise the implemented APIs on a real host:

- health endpoints respond
- the bootstrap admin can register principals
- client cache APIs work
- auth and role boundaries are enforced
- operator APIs work
- admin token lifecycle APIs work
- admin export, import, purge-selected, and purge-all APIs work

If you want to go deeper next, use `docs/runbooks/manual-end-to-end-testing.md`.
