# Runbook: Manual End-To-End Testing On CachyOS

## Purpose

Run a full host-side manual validation of `mp-cache` from a normal CachyOS local shell.

This runbook covers:

- local dependency setup
- runtime secret creation and deployment
- isolated manual-test config creation
- build, start, smoke test, and cleanup
- every implemented HTTP API endpoint
- deterministic auth, role, conflict, not-found, method, size-limit, import-integrity, and rate-limit checks
- persistence across restart

This runbook is written for CachyOS hosts and does not require the Codex sandbox. Run it directly on the host.

## Preconditions

- CachyOS host shell with Unix-socket `bind(2)` and `connect(2)` allowed
- repo checked out locally
- writable repo `.tmp/` paths
- writable `/tmp/`
- Bash available
- `jq` installed if you want to run the commands exactly as written below

Build requirements:

- `cmake`
- `make`
- `gcc` or `clang`
- `git`
- `curl`

Recommended validation tools:

- `jq`
- `valgrind`

## Important Distinction

The build does not require secrets.

Server startup, persistence, and authenticated API testing do require secrets:

- bootstrap admin token
- storage encryption key

## Install Local Dependencies

Supported local package flow on CachyOS:

```sh
export MP_CACHE_REPO=/run/media/san/ce0dc301-0250-497b-8390-b8547d284322/workwork/repositories/multi-player-app/mp-cache
cd "$MP_CACHE_REPO"

./scripts/install-local-deps.sh --dry-run
sudo pacman -S --needed base-devel cmake git curl jq
```

Optional host-side memory validation tools:

```sh
sudo pacman -S --needed valgrind debuginfod
```

This runbook assumes CachyOS because the local helper scripts target the repo's `arch-cachyos` environment check. If you intentionally adapt the flow to a different host, install equivalent packages manually and allow the local build helper to skip the OS identity check:

```sh
export MP_ALLOW_ENV_MISMATCH=1
```

If `make test-valgrind` fails before any unit test starts, use [valgrind-host-setup.md](/run/media/san/ce0dc301-0250-497b-8390-b8547d284322/workwork/repositories/multi-player-app/mp-cache/docs/runbooks/valgrind-host-setup.md:1).

## Prepare Isolated Manual-Test State

The commands below keep manual validation isolated from the repo's default `.tmp/data` and `.tmp/exports`.

```sh
export MP_CACHE_REPO=/run/media/san/ce0dc301-0250-497b-8390-b8547d284322/workwork/repositories/multi-player-app/mp-cache
cd "$MP_CACHE_REPO"

mkdir -p .tmp/manual-e2e/payloads .tmp/manual-e2e/responses .tmp/manual-e2e/secrets
mkdir -p /tmp/mp-cache/manual-e2e

BOOTSTRAP_ADMIN_TOKEN="manual-admin-$(date +%s)-$(tr -dc 'A-Za-z0-9' </dev/urandom | head -c 16)"
STORAGE_KEY="manual-storage-$(date +%s)-$(tr -dc 'A-Za-z0-9' </dev/urandom | head -c 32)"

cat > .tmp/manual-e2e/secrets/local.env <<EOF
MP_SECRET_LOCAL_BOOTSTRAP_ADMIN_TOKEN=${BOOTSTRAP_ADMIN_TOKEN}
MP_SECRET_LOCAL_STORAGE_KEY=${STORAGE_KEY}
EOF

chmod 600 .tmp/manual-e2e/secrets/local.env

cat > .tmp/manual-e2e/manual-e2e.ini <<'EOF'
[service]
environment_name = local
service_name = mp-cache

[server]
socket_path = /tmp/mp-cache/manual-e2e/mp-cache.sock
pid_file_path = /tmp/mp-cache/manual-e2e/mp-cache.pid
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
data_directory = .tmp/manual-e2e/data
export_directory = .tmp/manual-e2e/exports
checkpoint_path = .tmp/manual-e2e/data/state.checkpoint
journal_path = .tmp/manual-e2e/data/state.journal
max_export_files = 16

[observability]
log_directory = .tmp/manual-e2e/logs
max_log_lines = 200

[security]
bootstrap_admin_token_secret_ref = env:MP_SECRET_LOCAL_BOOTSTRAP_ADMIN_TOKEN
storage_key_secret_ref = env:MP_SECRET_LOCAL_STORAGE_KEY
rate_limit_requests = 240
rate_limit_window_seconds = 60
EOF

export MP_CONFIG_PATH="$MP_CACHE_REPO/.tmp/manual-e2e/manual-e2e.ini"
export MP_SOCKET_PATH=/tmp/mp-cache/manual-e2e/mp-cache.sock
export MP_PID_FILE=/tmp/mp-cache/manual-e2e/mp-cache.pid
export MP_CONSOLE_LOG="$MP_CACHE_REPO/.tmp/manual-e2e/console.log"
export MP_LOCAL_SECRET_ENV_FILE="$MP_CACHE_REPO/.tmp/manual-e2e/secrets/local.env"

set -a
. "$MP_LOCAL_SECRET_ENV_FILE"
set +a

export ADMIN_AUTH_HEADER="Authorization: Bearer $MP_SECRET_LOCAL_BOOTSTRAP_ADMIN_TOKEN"
```

## Optional File-Based Secret Deployment

Use this only if you want to test the `file:<absolute_path>` secret contract instead of the local `env:<name>` contract.

```sh
cd "$MP_CACHE_REPO"

mkdir -p .tmp/manual-e2e/file-secrets
printf '%s\n' "$MP_SECRET_LOCAL_BOOTSTRAP_ADMIN_TOKEN" > .tmp/manual-e2e/file-secrets/bootstrap_admin_token
printf '%s\n' "$MP_SECRET_LOCAL_STORAGE_KEY" > .tmp/manual-e2e/file-secrets/storage_key
chmod 600 .tmp/manual-e2e/file-secrets/bootstrap_admin_token .tmp/manual-e2e/file-secrets/storage_key

admin_secret_path="$(realpath .tmp/manual-e2e/file-secrets/bootstrap_admin_token)"
storage_secret_path="$(realpath .tmp/manual-e2e/file-secrets/storage_key)"

sed \
  -e "s#env:MP_SECRET_LOCAL_BOOTSTRAP_ADMIN_TOKEN#file:${admin_secret_path}#" \
  -e "s#env:MP_SECRET_LOCAL_STORAGE_KEY#file:${storage_secret_path}#" \
  .tmp/manual-e2e/manual-e2e.ini > .tmp/manual-e2e/manual-e2e.file-secrets.ini

export MP_CONFIG_PATH="$MP_CACHE_REPO/.tmp/manual-e2e/manual-e2e.file-secrets.ini"
export MP_LOCAL_SECRET_ENV_FILE="$MP_CACHE_REPO/.tmp/manual-e2e/secrets/unused.local.env"
rm -f "$MP_LOCAL_SECRET_ENV_FILE"
unset MP_SECRET_LOCAL_BOOTSTRAP_ADMIN_TOKEN
unset MP_SECRET_LOCAL_STORAGE_KEY
unset ADMIN_AUTH_HEADER
export ADMIN_AUTH_HEADER="Authorization: Bearer $BOOTSTRAP_ADMIN_TOKEN"
```

If you use the file-based variant, keep `MP_CONFIG_PATH` pointed at `.tmp/manual-e2e/manual-e2e.file-secrets.ini` for the remaining steps.

## Build, Start, and Automated Preflight

```sh
cd "$MP_CACHE_REPO"

./scripts/stop-local-server.sh || true
./scripts/build-local.sh

./build/local-debug/mp-cache-server --config "$MP_CONFIG_PATH" --print-config | tee .tmp/manual-e2e/responses/effective-config.txt

./scripts/run-local-server.sh

curl --silent --show-error --unix-socket "$MP_SOCKET_PATH" http://localhost/v1/health | tee .tmp/manual-e2e/responses/startup-health.json | jq .

./scripts/test-local.sh
```

## Unauthenticated Endpoint Checks

Root route:

```sh
curl --silent --show-error \
  --output .tmp/manual-e2e/responses/root.json \
  --write-out '%{http_code}\n' \
  --unix-socket "$MP_SOCKET_PATH" \
  http://localhost/

jq -e '.service=="mp-cache" and (.message | contains("/health"))' .tmp/manual-e2e/responses/root.json
```

`GET /health`:

```sh
curl --silent --show-error \
  --output .tmp/manual-e2e/responses/health.json \
  --write-out '%{http_code}\n' \
  --unix-socket "$MP_SOCKET_PATH" \
  http://localhost/health

jq -e '.status=="ok" and .service=="mp-cache" and .environment=="local"' .tmp/manual-e2e/responses/health.json
```

`GET /v1/health`:

```sh
curl --silent --show-error \
  --output .tmp/manual-e2e/responses/health-v1.json \
  --write-out '%{http_code}\n' \
  --unix-socket "$MP_SOCKET_PATH" \
  http://localhost/v1/health

jq -e '.status=="ok" and .socket_path=="/tmp/mp-cache/manual-e2e/mp-cache.sock"' .tmp/manual-e2e/responses/health-v1.json
```

Wrong method on health:

```sh
curl --silent --show-error \
  --output .tmp/manual-e2e/responses/health-post.json \
  --write-out '%{http_code}\n' \
  --unix-socket "$MP_SOCKET_PATH" \
  -X POST \
  http://localhost/v1/health

jq -e '.error=="invalid_argument" and .message=="method not allowed"' .tmp/manual-e2e/responses/health-post.json
```

Unknown route:

```sh
curl --silent --show-error \
  --output .tmp/manual-e2e/responses/not-found-route.json \
  --write-out '%{http_code}\n' \
  --unix-socket "$MP_SOCKET_PATH" \
  http://localhost/v1/does-not-exist

jq -e '.error=="not_found" and .message=="route not found"' .tmp/manual-e2e/responses/not-found-route.json
```

## Create Manual Test Principals

Register a client:

```sh
curl --silent --show-error \
  --output .tmp/manual-e2e/responses/register-client.json \
  --write-out '%{http_code}\n' \
  --unix-socket "$MP_SOCKET_PATH" \
  -H "$ADMIN_AUTH_HEADER" \
  -H 'Content-Type: application/json' \
  -X POST \
  http://localhost/v1/clients \
  --data '{"client_id":"manual-client","role":"client"}'

jq -e '.client_id=="manual-client" and .role=="client" and (.token | length > 0)' .tmp/manual-e2e/responses/register-client.json

export CLIENT_TOKEN="$(jq -r '.token' .tmp/manual-e2e/responses/register-client.json)"
export CLIENT_AUTH_HEADER="Authorization: Bearer $CLIENT_TOKEN"
```

Register an operator:

```sh
curl --silent --show-error \
  --output .tmp/manual-e2e/responses/register-operator.json \
  --write-out '%{http_code}\n' \
  --unix-socket "$MP_SOCKET_PATH" \
  -H "$ADMIN_AUTH_HEADER" \
  -H 'Content-Type: application/json' \
  -X POST \
  http://localhost/v1/clients \
  --data '{"client_id":"manual-operator","role":"operator"}'

jq -e '.client_id=="manual-operator" and .role=="operator" and (.token | length > 0)' .tmp/manual-e2e/responses/register-operator.json

export OPERATOR_TOKEN="$(jq -r '.token' .tmp/manual-e2e/responses/register-operator.json)"
export OPERATOR_AUTH_HEADER="Authorization: Bearer $OPERATOR_TOKEN"
```

Register a second admin:

```sh
curl --silent --show-error \
  --output .tmp/manual-e2e/responses/register-admin.json \
  --write-out '%{http_code}\n' \
  --unix-socket "$MP_SOCKET_PATH" \
  -H "$ADMIN_AUTH_HEADER" \
  -H 'Content-Type: application/json' \
  -X POST \
  http://localhost/v1/clients \
  --data '{"client_id":"manual-admin-2","role":"admin"}'

jq -e '.client_id=="manual-admin-2" and .role=="admin" and (.token | length > 0)' .tmp/manual-e2e/responses/register-admin.json

export MANUAL_ADMIN_TOKEN="$(jq -r '.token' .tmp/manual-e2e/responses/register-admin.json)"
export MANUAL_ADMIN_AUTH_HEADER="Authorization: Bearer $MANUAL_ADMIN_TOKEN"
```

Duplicate `client_id` conflict:

```sh
curl --silent --show-error \
  --output .tmp/manual-e2e/responses/register-client-conflict.json \
  --write-out '%{http_code}\n' \
  --unix-socket "$MP_SOCKET_PATH" \
  -H "$ADMIN_AUTH_HEADER" \
  -H 'Content-Type: application/json' \
  -X POST \
  http://localhost/v1/clients \
  --data '{"client_id":"manual-client","role":"client"}'

jq -e '.error=="conflict" and .message=="client_id already exists"' .tmp/manual-e2e/responses/register-client-conflict.json
```

Invalid role:

```sh
curl --silent --show-error \
  --output .tmp/manual-e2e/responses/register-invalid-role.json \
  --write-out '%{http_code}\n' \
  --unix-socket "$MP_SOCKET_PATH" \
  -H "$ADMIN_AUTH_HEADER" \
  -H 'Content-Type: application/json' \
  -X POST \
  http://localhost/v1/clients \
  --data '{"client_id":"manual-invalid-role","role":"root"}'

jq -e '.error=="invalid_argument" and (.message | contains("role must be client, operator, or admin"))' .tmp/manual-e2e/responses/register-invalid-role.json
```

## Cache API Checks

Store `alpha` with explicit TTL:

```sh
curl --silent --show-error \
  --output .tmp/manual-e2e/responses/cache-put-alpha.json \
  --write-out '%{http_code}\n' \
  --unix-socket "$MP_SOCKET_PATH" \
  -H "$CLIENT_AUTH_HEADER" \
  -H 'Content-Type: application/json' \
  -X PUT \
  http://localhost/v1/cache/alpha \
  --data '{"value_base64":"aGVsbG8=","ttl_seconds":60}'

jq -e '.key=="alpha" and .ttl_seconds==60' .tmp/manual-e2e/responses/cache-put-alpha.json
```

Fetch `alpha`:

```sh
curl --silent --show-error \
  --output .tmp/manual-e2e/responses/cache-get-alpha.json \
  --write-out '%{http_code}\n' \
  --unix-socket "$MP_SOCKET_PATH" \
  -H "$CLIENT_AUTH_HEADER" \
  http://localhost/v1/cache/alpha

jq -e '.key=="alpha" and .value_base64=="aGVsbG8="' .tmp/manual-e2e/responses/cache-get-alpha.json
```

Store `beta` without `ttl_seconds` to verify the configured default TTL:

```sh
curl --silent --show-error \
  --output .tmp/manual-e2e/responses/cache-put-beta.json \
  --write-out '%{http_code}\n' \
  --unix-socket "$MP_SOCKET_PATH" \
  -H "$CLIENT_AUTH_HEADER" \
  -H 'Content-Type: application/json' \
  -X PUT \
  http://localhost/v1/cache/beta \
  --data '{"value_base64":"d29ybGQ="}'

jq -e '.key=="beta" and .ttl_seconds==172800' .tmp/manual-e2e/responses/cache-put-beta.json
```

Fetch `beta`:

```sh
curl --silent --show-error \
  --output .tmp/manual-e2e/responses/cache-get-beta.json \
  --write-out '%{http_code}\n' \
  --unix-socket "$MP_SOCKET_PATH" \
  -H "$CLIENT_AUTH_HEADER" \
  http://localhost/v1/cache/beta

jq -e '.key=="beta" and .value_base64=="d29ybGQ="' .tmp/manual-e2e/responses/cache-get-beta.json
```

Delete `alpha`:

```sh
curl --silent --show-error \
  --output .tmp/manual-e2e/responses/cache-delete-alpha.json \
  --write-out '%{http_code}\n' \
  --unix-socket "$MP_SOCKET_PATH" \
  -H "$CLIENT_AUTH_HEADER" \
  -X DELETE \
  http://localhost/v1/cache/alpha

jq -e '.key=="alpha" and .deleted==true' .tmp/manual-e2e/responses/cache-delete-alpha.json
```

Verify `alpha` is gone:

```sh
curl --silent --show-error \
  --output .tmp/manual-e2e/responses/cache-get-alpha-missing.json \
  --write-out '%{http_code}\n' \
  --unix-socket "$MP_SOCKET_PATH" \
  -H "$CLIENT_AUTH_HEADER" \
  http://localhost/v1/cache/alpha

jq -e '.error=="not_found" and .message=="cache key not found"' .tmp/manual-e2e/responses/cache-get-alpha-missing.json
```

Invalid Base64:

```sh
curl --silent --show-error \
  --output .tmp/manual-e2e/responses/cache-put-invalid-base64.json \
  --write-out '%{http_code}\n' \
  --unix-socket "$MP_SOCKET_PATH" \
  -H "$CLIENT_AUTH_HEADER" \
  -H 'Content-Type: application/json' \
  -X PUT \
  http://localhost/v1/cache/invalid-base64 \
  --data '{"value_base64":"***not-base64***","ttl_seconds":60}'

jq -e '.error=="invalid_argument" and .message=="value_base64 is invalid"' .tmp/manual-e2e/responses/cache-put-invalid-base64.json
```

Invalid TTL:

```sh
curl --silent --show-error \
  --output .tmp/manual-e2e/responses/cache-put-invalid-ttl.json \
  --write-out '%{http_code}\n' \
  --unix-socket "$MP_SOCKET_PATH" \
  -H "$CLIENT_AUTH_HEADER" \
  -H 'Content-Type: application/json' \
  -X PUT \
  http://localhost/v1/cache/invalid-ttl \
  --data '{"value_base64":"aGVsbG8=","ttl_seconds":0}'

jq -e '.error=="invalid_argument" and (.message | contains("ttl_seconds is outside the configured bounds"))' .tmp/manual-e2e/responses/cache-put-invalid-ttl.json
```

Oversized value to trigger `413 Payload Too Large`:

```sh
dd if=/dev/zero of=.tmp/manual-e2e/payloads/too-large.bin bs=1048577 count=1 status=none
base64 .tmp/manual-e2e/payloads/too-large.bin | tr -d '\n' > .tmp/manual-e2e/payloads/too-large.b64

{
  printf '{"value_base64":"'
  cat .tmp/manual-e2e/payloads/too-large.b64
  printf '","ttl_seconds":60}'
} > .tmp/manual-e2e/payloads/too-large.json

curl --silent --show-error \
  --output .tmp/manual-e2e/responses/cache-put-too-large.json \
  --write-out '%{http_code}\n' \
  --unix-socket "$MP_SOCKET_PATH" \
  -H "$CLIENT_AUTH_HEADER" \
  -H 'Content-Type: application/json' \
  -X PUT \
  http://localhost/v1/cache/too-large \
  --data-binary @.tmp/manual-e2e/payloads/too-large.json

jq -e '.error=="limit_exceeded" and .message=="cache size limit exceeded"' .tmp/manual-e2e/responses/cache-put-too-large.json
```

## Authentication and Authorization Checks

Missing bearer token:

```sh
curl --silent --show-error \
  --output .tmp/manual-e2e/responses/stats-missing-token.json \
  --write-out '%{http_code}\n' \
  --unix-socket "$MP_SOCKET_PATH" \
  http://localhost/v1/stats

jq -e '.error=="unauthorized" and .message=="missing bearer token"' .tmp/manual-e2e/responses/stats-missing-token.json
```

Invalid bearer token:

```sh
curl --silent --show-error \
  --output .tmp/manual-e2e/responses/stats-invalid-token.json \
  --write-out '%{http_code}\n' \
  --unix-socket "$MP_SOCKET_PATH" \
  -H 'Authorization: Bearer invalid-token' \
  http://localhost/v1/stats

jq -e '.error=="unauthorized" and .message=="invalid bearer token"' .tmp/manual-e2e/responses/stats-invalid-token.json
```

Client token against an operator-only route:

```sh
curl --silent --show-error \
  --output .tmp/manual-e2e/responses/stats-client-forbidden.json \
  --write-out '%{http_code}\n' \
  --unix-socket "$MP_SOCKET_PATH" \
  -H "$CLIENT_AUTH_HEADER" \
  http://localhost/v1/stats

jq -e '.error=="forbidden" and .message=="insufficient role"' .tmp/manual-e2e/responses/stats-client-forbidden.json
```

## Operator API Checks

Stats:

```sh
curl --silent --show-error \
  --output .tmp/manual-e2e/responses/stats-operator.json \
  --write-out '%{http_code}\n' \
  --unix-socket "$MP_SOCKET_PATH" \
  -H "$OPERATOR_AUTH_HEADER" \
  http://localhost/v1/stats

jq -e '.cache_sets >= 2 and .cache_deletes >= 1 and .client_registrations >= 3' .tmp/manual-e2e/responses/stats-operator.json
```

Memory metrics:

```sh
curl --silent --show-error \
  --output .tmp/manual-e2e/responses/memory-operator.json \
  --write-out '%{http_code}\n' \
  --unix-socket "$MP_SOCKET_PATH" \
  -H "$OPERATOR_AUTH_HEADER" \
  http://localhost/v1/metrics/memory

jq -e '.entry_count >= 1 and .memory_limit_bytes == 268435456' .tmp/manual-e2e/responses/memory-operator.json
```

Uptime:

```sh
curl --silent --show-error \
  --output .tmp/manual-e2e/responses/uptime-operator.json \
  --write-out '%{http_code}\n' \
  --unix-socket "$MP_SOCKET_PATH" \
  -H "$OPERATOR_AUTH_HEADER" \
  http://localhost/v1/uptime

jq -e '.uptime_seconds >= 0 and .started_at_utc_seconds > 0' .tmp/manual-e2e/responses/uptime-operator.json
```

Logs:

```sh
curl --silent --show-error \
  --output .tmp/manual-e2e/responses/logs-operator.json \
  --write-out '%{http_code}\n' \
  --unix-socket "$MP_SOCKET_PATH" \
  -H "$OPERATOR_AUTH_HEADER" \
  'http://localhost/v1/logs?tail=20'

jq -e '.tail_lines == 20 and (.text | length) > 0' .tmp/manual-e2e/responses/logs-operator.json
jq -r '.text' .tmp/manual-e2e/responses/logs-operator.json
```

## Admin API Checks

Use the second admin token on an admin-only route:

```sh
curl --silent --show-error \
  --output .tmp/manual-e2e/responses/export-admin-2.json \
  --write-out '%{http_code}\n' \
  --unix-socket "$MP_SOCKET_PATH" \
  -H "$MANUAL_ADMIN_AUTH_HEADER" \
  -H 'Content-Type: application/json' \
  -X POST \
  http://localhost/v1/export \
  --data '{}'

jq -e '.entry_count >= 1 and .client_count >= 3 and (.path | length > 0) and (.digest_sha256 | length > 0)' .tmp/manual-e2e/responses/export-admin-2.json

export EXPORT_PATH="$(jq -r '.path' .tmp/manual-e2e/responses/export-admin-2.json)"
```

Wrong method on an admin route:

```sh
curl --silent --show-error \
  --output .tmp/manual-e2e/responses/export-get-method.json \
  --write-out '%{http_code}\n' \
  --unix-socket "$MP_SOCKET_PATH" \
  -H "$MANUAL_ADMIN_AUTH_HEADER" \
  http://localhost/v1/export

jq -e '.error=="invalid_argument" and .message=="method not allowed"' .tmp/manual-e2e/responses/export-get-method.json
```

Rotate the client token:

```sh
export OLD_CLIENT_TOKEN="$CLIENT_TOKEN"

curl --silent --show-error \
  --output .tmp/manual-e2e/responses/rotate-client-token.json \
  --write-out '%{http_code}\n' \
  --unix-socket "$MP_SOCKET_PATH" \
  -H "$MANUAL_ADMIN_AUTH_HEADER" \
  -H 'Content-Type: application/json' \
  -X POST \
  http://localhost/v1/clients/manual-client/rotate-token \
  --data '{}'

jq -e '.client_id=="manual-client" and (.token | length > 0)' .tmp/manual-e2e/responses/rotate-client-token.json

export CLIENT_TOKEN="$(jq -r '.token' .tmp/manual-e2e/responses/rotate-client-token.json)"
export CLIENT_AUTH_HEADER="Authorization: Bearer $CLIENT_TOKEN"
```

Verify the old token now fails:

```sh
curl --silent --show-error \
  --output .tmp/manual-e2e/responses/cache-get-beta-old-token.json \
  --write-out '%{http_code}\n' \
  --unix-socket "$MP_SOCKET_PATH" \
  -H "Authorization: Bearer $OLD_CLIENT_TOKEN" \
  http://localhost/v1/cache/beta

jq -e '.error=="unauthorized" and .message=="invalid bearer token"' .tmp/manual-e2e/responses/cache-get-beta-old-token.json
```

Verify the new token works:

```sh
curl --silent --show-error \
  --output .tmp/manual-e2e/responses/cache-get-beta-new-token.json \
  --write-out '%{http_code}\n' \
  --unix-socket "$MP_SOCKET_PATH" \
  -H "$CLIENT_AUTH_HEADER" \
  http://localhost/v1/cache/beta

jq -e '.key=="beta" and .value_base64=="d29ybGQ="' .tmp/manual-e2e/responses/cache-get-beta-new-token.json
```

Create a tampered export file and verify import rejection:

```sh
cp "$EXPORT_PATH" .tmp/manual-e2e/exports/tampered-export.bin
printf 'X' | dd of=.tmp/manual-e2e/exports/tampered-export.bin bs=1 seek=32 conv=notrunc status=none

curl --silent --show-error \
  --output .tmp/manual-e2e/responses/import-tampered.json \
  --write-out '%{http_code}\n' \
  --unix-socket "$MP_SOCKET_PATH" \
  -H "$MANUAL_ADMIN_AUTH_HEADER" \
  -H 'Content-Type: application/json' \
  -X POST \
  http://localhost/v1/import \
  --data '{"path":".tmp/manual-e2e/exports/tampered-export.bin"}'

jq -e '.error=="invalid_argument" and (.message | contains("state import failed integrity or bounds checks"))' .tmp/manual-e2e/responses/import-tampered.json
```

Invalid import path:

```sh
curl --silent --show-error \
  --output .tmp/manual-e2e/responses/import-invalid-path.json \
  --write-out '%{http_code}\n' \
  --unix-socket "$MP_SOCKET_PATH" \
  -H "$MANUAL_ADMIN_AUTH_HEADER" \
  -H 'Content-Type: application/json' \
  -X POST \
  http://localhost/v1/import \
  --data '{"path":"../outside.bin"}'

jq -e '.error=="invalid_argument" and (.message | contains("state import failed integrity or bounds checks"))' .tmp/manual-e2e/responses/import-invalid-path.json
```

Purge all cache state:

```sh
curl --silent --show-error \
  --output .tmp/manual-e2e/responses/purge-all.json \
  --write-out '%{http_code}\n' \
  --unix-socket "$MP_SOCKET_PATH" \
  -H "$MANUAL_ADMIN_AUTH_HEADER" \
  -H 'Content-Type: application/json' \
  -X POST \
  http://localhost/v1/purge/all \
  --data '{}'

jq -e '.purged==true' .tmp/manual-e2e/responses/purge-all.json
```

Verify `beta` is gone after purge:

```sh
curl --silent --show-error \
  --output .tmp/manual-e2e/responses/cache-get-beta-after-purge.json \
  --write-out '%{http_code}\n' \
  --unix-socket "$MP_SOCKET_PATH" \
  -H "$CLIENT_AUTH_HEADER" \
  http://localhost/v1/cache/beta

jq -e '.error=="not_found" and .message=="cache key not found"' .tmp/manual-e2e/responses/cache-get-beta-after-purge.json
```

Import the valid export file back:

```sh
curl --silent --show-error \
  --output .tmp/manual-e2e/responses/import-valid.json \
  --write-out '%{http_code}\n' \
  --unix-socket "$MP_SOCKET_PATH" \
  -H "$MANUAL_ADMIN_AUTH_HEADER" \
  -H 'Content-Type: application/json' \
  -X POST \
  http://localhost/v1/import \
  --data "{\"path\":\"${EXPORT_PATH}\"}"

jq -e --arg path "$EXPORT_PATH" '.imported==true and .path==$path' .tmp/manual-e2e/responses/import-valid.json
```

Verify `beta` is restored:

```sh
curl --silent --show-error \
  --output .tmp/manual-e2e/responses/cache-get-beta-after-import.json \
  --write-out '%{http_code}\n' \
  --unix-socket "$MP_SOCKET_PATH" \
  -H "$CLIENT_AUTH_HEADER" \
  http://localhost/v1/cache/beta

jq -e '.key=="beta" and .value_base64=="d29ybGQ="' .tmp/manual-e2e/responses/cache-get-beta-after-import.json
```

## Rate-Limit Check

Create a fresh operator so earlier operator requests do not affect the rate-limit test:

```sh
curl --silent --show-error \
  --output .tmp/manual-e2e/responses/register-rate-limit-operator.json \
  --write-out '%{http_code}\n' \
  --unix-socket "$MP_SOCKET_PATH" \
  -H "$ADMIN_AUTH_HEADER" \
  -H 'Content-Type: application/json' \
  -X POST \
  http://localhost/v1/clients \
  --data '{"client_id":"manual-rate-limit-operator","role":"operator"}'

jq -e '.client_id=="manual-rate-limit-operator" and .role=="operator"' .tmp/manual-e2e/responses/register-rate-limit-operator.json

export RATE_LIMIT_OPERATOR_TOKEN="$(jq -r '.token' .tmp/manual-e2e/responses/register-rate-limit-operator.json)"
```

Run 241 authenticated operator requests in the same 60-second window. The first 240 should be `200`; request 241 should be `429`.

```sh
: > .tmp/manual-e2e/responses/rate-limit-codes.txt

for attempt in $(seq 1 241); do
  http_code="$(
    curl --silent --show-error \
      --output ".tmp/manual-e2e/responses/rate-limit-${attempt}.json" \
      --write-out '%{http_code}' \
      --unix-socket "$MP_SOCKET_PATH" \
      -H "Authorization: Bearer $RATE_LIMIT_OPERATOR_TOKEN" \
      http://localhost/v1/uptime
  )"
  printf '%s %s\n' "$attempt" "$http_code" >> .tmp/manual-e2e/responses/rate-limit-codes.txt
done

sed -n '1p;240p;241p' .tmp/manual-e2e/responses/rate-limit-codes.txt
awk 'NR==240 { ok_240 = ($2 == "200") } NR==241 { ok_241 = ($2 == "429") } END { exit !(ok_240 && ok_241) }' .tmp/manual-e2e/responses/rate-limit-codes.txt
jq -e '.error=="limit_exceeded" and .message=="rate limit exceeded"' .tmp/manual-e2e/responses/rate-limit-241.json
```

## Persistence Across Restart

Write a value that should survive restart:

```sh
curl --silent --show-error \
  --output .tmp/manual-e2e/responses/cache-put-persist.json \
  --write-out '%{http_code}\n' \
  --unix-socket "$MP_SOCKET_PATH" \
  -H "$CLIENT_AUTH_HEADER" \
  -H 'Content-Type: application/json' \
  -X PUT \
  http://localhost/v1/cache/persist-key \
  --data '{"value_base64":"cGVyc2lzdC1vaw==","ttl_seconds":600}'

jq -e '.key=="persist-key" and .ttl_seconds==600' .tmp/manual-e2e/responses/cache-put-persist.json
```

Stop and restart:

```sh
./scripts/stop-local-server.sh
./scripts/run-local-server.sh
```

Verify the value survived:

```sh
curl --silent --show-error \
  --output .tmp/manual-e2e/responses/cache-get-persist-after-restart.json \
  --write-out '%{http_code}\n' \
  --unix-socket "$MP_SOCKET_PATH" \
  -H "$CLIENT_AUTH_HEADER" \
  http://localhost/v1/cache/persist-key

jq -e '.key=="persist-key" and .value_base64=="cGVyc2lzdC1vaw=="' .tmp/manual-e2e/responses/cache-get-persist-after-restart.json
```

Verify operator and admin state also survived:

```sh
curl --silent --show-error \
  --output .tmp/manual-e2e/responses/stats-after-restart.json \
  --write-out '%{http_code}\n' \
  --unix-socket "$MP_SOCKET_PATH" \
  -H "$OPERATOR_AUTH_HEADER" \
  http://localhost/v1/stats

jq -e '.entry_count >= 1' .tmp/manual-e2e/responses/stats-after-restart.json

curl --silent --show-error \
  --output .tmp/manual-e2e/responses/export-after-restart.json \
  --write-out '%{http_code}\n' \
  --unix-socket "$MP_SOCKET_PATH" \
  -H "$MANUAL_ADMIN_AUTH_HEADER" \
  -H 'Content-Type: application/json' \
  -X POST \
  http://localhost/v1/export \
  --data '{}'

jq -e '.entry_count >= 1 and .client_count >= 4' .tmp/manual-e2e/responses/export-after-restart.json
```

## Optional Valgrind Validation

If the host is prepared for Valgrind:

```sh
make test-valgrind
```

If that fails before test execution starts, use [valgrind-host-setup.md](/run/media/san/ce0dc301-0250-497b-8390-b8547d284322/workwork/repositories/multi-player-app/mp-cache/docs/runbooks/valgrind-host-setup.md:1).

## Cleanup

Stop the isolated manual-test server:

```sh
./scripts/stop-local-server.sh
```

Remove isolated manual-test artifacts:

```sh
rm -rf .tmp/manual-e2e /tmp/mp-cache/manual-e2e
```

## Expected Outcome

If all commands above pass:

- the project builds in a normal local shell
- secrets are resolved correctly
- the Unix-socket HTTP server starts and responds
- all implemented APIs behave as documented
- auth and role boundaries work
- export/import and persistence behave correctly
- oversize payloads, conflicts, invalid requests, unknown routes, wrong methods, and rate limits return the expected errors
