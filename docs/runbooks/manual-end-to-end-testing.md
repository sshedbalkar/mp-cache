# Runbook: Manual End-To-End Testing On CachyOS

## 1. Purpose

Use this runbook when you want to manually prove that `mp-cache` works end to end on a real CachyOS host.

This guide is intentionally written for novice developers. It assumes you are comfortable copying and pasting shell commands, but it does not assume you already know this project.

By the time you finish, you will have verified:

- the project builds from source
- secrets load correctly
- the Unix-socket server starts
- every implemented HTTP endpoint behaves as expected
- authentication and role checks work
- token rotation and token invalidation work
- selective purge and purge-all work
- export and import work, including tamper rejection
- rate limiting works
- cache data and principal state survive restart

Run everything from a normal host shell, not from a restricted sandbox.

If you want a faster path where local dependency install, secret creation, build, and deployment are handled by scripts and you only manually call the APIs afterward, use `docs/runbooks/manual-api-testing-with-scripted-local-deploy.md`.

## 2. Before You Start

### What you need

- a CachyOS host shell
- a local checkout of the `multi-player-app` repository
- permission to write inside the repository's `.tmp/` directory
- `bash`
- `curl`
- `jq`

Build tools:

- `cmake`
- `make`
- `gcc` or `clang`
- `git`

Optional tool:

- `valgrind`

### Important distinction

Building the project does **not** require secrets.

Starting the server and testing authenticated APIs **does** require secrets:

- a bootstrap admin token
- a storage encryption key

### How to use this document

1. Open a fresh terminal.
2. Run one code block at a time.
3. Do not skip ahead when a step creates environment variables for later steps.
4. If a command prints an HTTP status code, compare it with the expected code written below the block.
5. If a `jq -e` command exits successfully and prints nothing, that is still a pass.

If a step fails, stop there and inspect the response file mentioned in the command. Most responses are saved under `.tmp/manual-e2e/responses/`.

## 3. Move Into The Repository

Change into your local `mp-cache` checkout first.

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

Those checks should succeed without printing an error.

## 4. Install Local Dependencies

First, let the repository show you the package plan it expects on Arch-based systems such as CachyOS.

```sh
cd "$MP_CACHE_REPO"
./scripts/install-local-deps.sh --dry-run --with-jq
```

Now install the packages needed for the commands in this runbook.

```sh
./scripts/install-local-deps.sh --with-jq
```

If you also want optional host-side memory validation later:

```sh
./scripts/install-local-deps.sh --dry-run --with-jq --with-valgrind
./scripts/install-local-deps.sh --with-jq --with-valgrind
```

Package installation may require `sudo`, but the rest of this runbook is designed for a normal non-root shell.

If `make test-valgrind` fails before any project test starts, use `docs/runbooks/valgrind-host-setup.md`.

## 5. Start A Strict Shell Session

This runbook is easier to follow if the shell stops when a command fails.

Use `set -eo pipefail` here, not `set -u`. In some interactive Bash setups, `set -u` can break prompt code and cause errors such as `bash: status_str: unbound variable`.

```sh
set -eo pipefail
```

Next, add one tiny helper function. It makes HTTP checks easier to read.

```sh
expect_code() {
  actual="$1"
  expected="$2"

  if [ "$actual" != "$expected" ]; then
    printf 'Expected HTTP %s but got %s\n' "$expected" "$actual" >&2
    return 1
  fi
}
```

## 6. Prepare An Isolated Manual-Test Workspace

This runbook does **not** use the default local runtime directories. That keeps your manual test separate from any other local development state.

Run this whole block exactly once:

```sh
cd "$MP_CACHE_REPO"

mkdir -p .tmp/manual-e2e/payloads
mkdir -p .tmp/manual-e2e/responses
mkdir -p .tmp/manual-e2e/secrets
mkdir -p .tmp/manual-e2e/run

export MP_LOCAL_SECRET_ENV_FILE="$MP_CACHE_REPO/.tmp/manual-e2e/secrets/local.env"
MP_LOCAL_SECRET_ENV_FILE="$MP_LOCAL_SECRET_ENV_FILE" ./scripts/write-local-secret-env.sh --force

cat > .tmp/manual-e2e/manual-e2e.ini <<'EOF'
[service]
environment_name = local
service_name = mp-cache

[server]
socket_path = .tmp/manual-e2e/run/mp-cache.sock
pid_file_path = .tmp/manual-e2e/run/mp-cache.pid
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
export MP_SOCKET_PATH="$MP_CACHE_REPO/.tmp/manual-e2e/run/mp-cache.sock"
export MP_PID_FILE="$MP_CACHE_REPO/.tmp/manual-e2e/run/mp-cache.pid"
export MP_CONSOLE_LOG="$MP_CACHE_REPO/.tmp/manual-e2e/console.log"

set -a
. "$MP_LOCAL_SECRET_ENV_FILE"
set +a

export BOOTSTRAP_ADMIN_TOKEN="$MP_SECRET_LOCAL_BOOTSTRAP_ADMIN_TOKEN"
export STORAGE_KEY="$MP_SECRET_LOCAL_STORAGE_KEY"
export ADMIN_AUTH_HEADER="Authorization: Bearer $MP_SECRET_LOCAL_BOOTSTRAP_ADMIN_TOKEN"
```

### What this step did

- created a brand-new test-only config file
- created test-only secrets through `./scripts/write-local-secret-env.sh`
- pointed the server at a test-only Unix socket
- kept the manual test runtime files under `.tmp/manual-e2e/`
- exported the bootstrap admin token for later API calls

### Quick sanity check

```sh
printf '%s\n' "$MP_CACHE_REPO"
printf '%s\n' "$MP_CONFIG_PATH"
printf '%s\n' "$MP_SOCKET_PATH"
printf '%s\n' "$MP_PID_FILE"
test -f "$MP_CONFIG_PATH"
test -f "$MP_LOCAL_SECRET_ENV_FILE"
grep '^MP_SECRET_LOCAL_BOOTSTRAP_ADMIN_TOKEN=' "$MP_LOCAL_SECRET_ENV_FILE"
grep '^MP_SECRET_LOCAL_STORAGE_KEY=' "$MP_LOCAL_SECRET_ENV_FILE"
```

Those commands should finish without error.

## 7. Optional: Switch To File-Based Secrets

Skip this section unless you explicitly want to test the `file:<absolute_path>` secret contract.

If you **do** want that variant, run this block and keep using the updated `MP_CONFIG_PATH` afterward.

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

If you skip this section, that is fine. The normal `env:<name>` secret flow is already enough for manual end-to-end testing.

## 8. Build, Start, And Run The Automated Preflight

### 8.1 Stop any old local server

This prevents stale local processes from confusing the test.

```sh
cd "$MP_CACHE_REPO"
./scripts/stop-local-server.sh || true
```

### 8.2 Build the project

```sh
./scripts/build-local.sh
```

If this fails, stop here and fix the build before continuing.

### 8.3 Print the effective config

This confirms the server can read your custom config file.

```sh
./build/local-debug/mp-cache-server --config "$MP_CONFIG_PATH" --print-config | tee .tmp/manual-e2e/responses/effective-config.txt
```

Look for these values in the output:

- `.tmp/manual-e2e/run/mp-cache.sock`
- `.tmp/manual-e2e/data`
- `.tmp/manual-e2e/exports`

### 8.4 Start the server

```sh
./scripts/run-local-server.sh
```

If you see this error:

```json
{"message":"failed to resolve bootstrap admin token","context":"startup"}
```

the most likely cause is that the secret file was never populated correctly. Re-run the full setup block from section 6, then confirm the file contains both required entries:

```sh
cat "$MP_LOCAL_SECRET_ENV_FILE"
grep '^MP_SECRET_LOCAL_BOOTSTRAP_ADMIN_TOKEN=' "$MP_LOCAL_SECRET_ENV_FILE"
grep '^MP_SECRET_LOCAL_STORAGE_KEY=' "$MP_LOCAL_SECRET_ENV_FILE"
```

Then run the start command again:

```sh
./scripts/run-local-server.sh
```

### 8.5 Confirm the server answers health checks

```sh
http_code="$(
  curl --silent --show-error \
    --output .tmp/manual-e2e/responses/startup-health.json \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    http://localhost/v1/health
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 200
jq . .tmp/manual-e2e/responses/startup-health.json
```

Expected:

- HTTP code `200`
- JSON containing `"status": "ok"`

### 8.6 Run the existing local automated suite

```sh
./scripts/test-local.sh
```

This script covers the rootless local validation flow, including the sanitizer pass. Keep Valgrind as a separate optional step later in this runbook.

## 9. Verify Unauthenticated Endpoints

These checks do not use any token yet.

### 9.1 Root route

```sh
http_code="$(
  curl --silent --show-error \
    --output .tmp/manual-e2e/responses/root.json \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    http://localhost/
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 200
jq -e '.service=="mp-cache" and (.message | contains("/health"))' .tmp/manual-e2e/responses/root.json
```

Expected HTTP code: `200`

### 9.2 `GET /health`

```sh
http_code="$(
  curl --silent --show-error \
    --output .tmp/manual-e2e/responses/health.json \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    http://localhost/health
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 200
jq -e '.status=="ok" and .service=="mp-cache" and .environment=="local"' .tmp/manual-e2e/responses/health.json
```

Expected HTTP code: `200`

### 9.3 `GET /v1/health`

```sh
http_code="$(
  curl --silent --show-error \
    --output .tmp/manual-e2e/responses/health-v1.json \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    http://localhost/v1/health
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 200
jq -e '.status=="ok" and .socket_path==".tmp/manual-e2e/run/mp-cache.sock"' .tmp/manual-e2e/responses/health-v1.json
```

Expected HTTP code: `200`

### 9.4 Wrong method on a known route

```sh
http_code="$(
  curl --silent --show-error \
    --output .tmp/manual-e2e/responses/health-post.json \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -X POST \
    http://localhost/v1/health
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 405
jq -e '.error=="invalid_argument" and .message=="method not allowed"' .tmp/manual-e2e/responses/health-post.json
```

Expected HTTP code: `405`

### 9.5 Unknown route

```sh
http_code="$(
  curl --silent --show-error \
    --output .tmp/manual-e2e/responses/not-found-route.json \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    http://localhost/v1/does-not-exist
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 404
jq -e '.error=="not_found" and .message=="route not found"' .tmp/manual-e2e/responses/not-found-route.json
```

Expected HTTP code: `404`

## 10. Create Manual Test Principals

You need three roles for the remaining tests:

- one `client`
- one `operator`
- one extra `admin`

The bootstrap admin token from the earlier setup step is what gives you permission to create them.

### 10.1 Register a client principal

```sh
http_code="$(
  curl --silent --show-error \
    --output .tmp/manual-e2e/responses/register-client.json \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$ADMIN_AUTH_HEADER" \
    -H 'Content-Type: application/json' \
    -X POST \
    http://localhost/v1/clients \
    --data '{"client_id":"manual-client","role":"client"}'
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 200
jq -e '.client_id=="manual-client" and .role=="client" and (.token | length > 0)' .tmp/manual-e2e/responses/register-client.json

export CLIENT_TOKEN="$(jq -r '.token' .tmp/manual-e2e/responses/register-client.json)"
export CLIENT_AUTH_HEADER="Authorization: Bearer $CLIENT_TOKEN"
```

Expected HTTP code: `200`

### 10.2 Register an operator principal

```sh
http_code="$(
  curl --silent --show-error \
    --output .tmp/manual-e2e/responses/register-operator.json \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$ADMIN_AUTH_HEADER" \
    -H 'Content-Type: application/json' \
    -X POST \
    http://localhost/v1/clients \
    --data '{"client_id":"manual-operator","role":"operator"}'
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 200
jq -e '.client_id=="manual-operator" and .role=="operator" and (.token | length > 0)' .tmp/manual-e2e/responses/register-operator.json

export OPERATOR_TOKEN="$(jq -r '.token' .tmp/manual-e2e/responses/register-operator.json)"
export OPERATOR_AUTH_HEADER="Authorization: Bearer $OPERATOR_TOKEN"
```

Expected HTTP code: `200`

### 10.3 Register a second admin principal

```sh
http_code="$(
  curl --silent --show-error \
    --output .tmp/manual-e2e/responses/register-admin.json \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$ADMIN_AUTH_HEADER" \
    -H 'Content-Type: application/json' \
    -X POST \
    http://localhost/v1/clients \
    --data '{"client_id":"manual-admin-2","role":"admin"}'
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 200
jq -e '.client_id=="manual-admin-2" and .role=="admin" and (.token | length > 0)' .tmp/manual-e2e/responses/register-admin.json

export MANUAL_ADMIN_TOKEN="$(jq -r '.token' .tmp/manual-e2e/responses/register-admin.json)"
export MANUAL_ADMIN_AUTH_HEADER="Authorization: Bearer $MANUAL_ADMIN_TOKEN"
```

Expected HTTP code: `200`

### 10.4 Confirm duplicate `client_id` fails with a conflict

```sh
http_code="$(
  curl --silent --show-error \
    --output .tmp/manual-e2e/responses/register-client-conflict.json \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$ADMIN_AUTH_HEADER" \
    -H 'Content-Type: application/json' \
    -X POST \
    http://localhost/v1/clients \
    --data '{"client_id":"manual-client","role":"client"}'
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 409
jq -e '.error=="conflict" and .message=="client_id already exists"' .tmp/manual-e2e/responses/register-client-conflict.json
```

Expected HTTP code: `409`

### 10.5 Confirm invalid roles are rejected

```sh
http_code="$(
  curl --silent --show-error \
    --output .tmp/manual-e2e/responses/register-invalid-role.json \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$ADMIN_AUTH_HEADER" \
    -H 'Content-Type: application/json' \
    -X POST \
    http://localhost/v1/clients \
    --data '{"client_id":"manual-invalid-role","role":"root"}'
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 400
jq -e '.error=="invalid_argument" and (.message | contains("role must be client, operator, or admin"))' .tmp/manual-e2e/responses/register-invalid-role.json
```

Expected HTTP code: `400`

## 11. Verify Cache APIs

These tests use the `client` token you created above.

### 11.1 Store `alpha` with an explicit TTL

```sh
http_code="$(
  curl --silent --show-error \
    --output .tmp/manual-e2e/responses/cache-put-alpha.json \
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
jq -e '.key=="alpha" and .ttl_seconds==60' .tmp/manual-e2e/responses/cache-put-alpha.json
```

Expected HTTP code: `200`

### 11.2 Fetch `alpha`

```sh
http_code="$(
  curl --silent --show-error \
    --output .tmp/manual-e2e/responses/cache-get-alpha.json \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$CLIENT_AUTH_HEADER" \
    http://localhost/v1/cache/alpha
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 200
jq -e '.key=="alpha" and .value_base64=="aGVsbG8="' .tmp/manual-e2e/responses/cache-get-alpha.json
```

Expected HTTP code: `200`

### 11.3 Store `beta` without `ttl_seconds`

This proves the configured default TTL is applied.

```sh
http_code="$(
  curl --silent --show-error \
    --output .tmp/manual-e2e/responses/cache-put-beta.json \
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
jq -e '.key=="beta" and .ttl_seconds==172800' .tmp/manual-e2e/responses/cache-put-beta.json
```

Expected HTTP code: `200`

### 11.4 Fetch `beta`

```sh
http_code="$(
  curl --silent --show-error \
    --output .tmp/manual-e2e/responses/cache-get-beta.json \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$CLIENT_AUTH_HEADER" \
    http://localhost/v1/cache/beta
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 200
jq -e '.key=="beta" and .value_base64=="d29ybGQ="' .tmp/manual-e2e/responses/cache-get-beta.json
```

Expected HTTP code: `200`

### 11.5 Delete `alpha`

```sh
http_code="$(
  curl --silent --show-error \
    --output .tmp/manual-e2e/responses/cache-delete-alpha.json \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$CLIENT_AUTH_HEADER" \
    -X DELETE \
    http://localhost/v1/cache/alpha
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 200
jq -e '.key=="alpha" and .deleted==true' .tmp/manual-e2e/responses/cache-delete-alpha.json
```

Expected HTTP code: `200`

### 11.6 Confirm `alpha` is gone

```sh
http_code="$(
  curl --silent --show-error \
    --output .tmp/manual-e2e/responses/cache-get-alpha-missing.json \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$CLIENT_AUTH_HEADER" \
    http://localhost/v1/cache/alpha
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 404
jq -e '.error=="not_found" and .message=="cache key not found"' .tmp/manual-e2e/responses/cache-get-alpha-missing.json
```

Expected HTTP code: `404`

### 11.7 Confirm invalid Base64 is rejected

```sh
http_code="$(
  curl --silent --show-error \
    --output .tmp/manual-e2e/responses/cache-put-invalid-base64.json \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$CLIENT_AUTH_HEADER" \
    -H 'Content-Type: application/json' \
    -X PUT \
    http://localhost/v1/cache/invalid-base64 \
    --data '{"value_base64":"***not-base64***","ttl_seconds":60}'
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 400
jq -e '.error=="invalid_argument" and .message=="value_base64 is invalid"' .tmp/manual-e2e/responses/cache-put-invalid-base64.json
```

Expected HTTP code: `400`

### 11.8 Confirm invalid TTL is rejected

```sh
http_code="$(
  curl --silent --show-error \
    --output .tmp/manual-e2e/responses/cache-put-invalid-ttl.json \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$CLIENT_AUTH_HEADER" \
    -H 'Content-Type: application/json' \
    -X PUT \
    http://localhost/v1/cache/invalid-ttl \
    --data '{"value_base64":"aGVsbG8=","ttl_seconds":0}'
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 400
jq -e '.error=="invalid_argument" and (.message | contains("ttl_seconds is outside the configured bounds"))' .tmp/manual-e2e/responses/cache-put-invalid-ttl.json
```

Expected HTTP code: `400`

### 11.9 Confirm oversize payloads are rejected

First create an oversized value.

```sh
dd if=/dev/zero of=.tmp/manual-e2e/payloads/too-large.bin bs=1048577 count=1 status=none
base64 .tmp/manual-e2e/payloads/too-large.bin | tr -d '\n' > .tmp/manual-e2e/payloads/too-large.b64

{
  printf '{"value_base64":"'
  cat .tmp/manual-e2e/payloads/too-large.b64
  printf '","ttl_seconds":60}'
} > .tmp/manual-e2e/payloads/too-large.json
```

Now submit it.

```sh
http_code="$(
  curl --silent --show-error \
    --output .tmp/manual-e2e/responses/cache-put-too-large.json \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$CLIENT_AUTH_HEADER" \
    -H 'Content-Type: application/json' \
    -X PUT \
    http://localhost/v1/cache/too-large \
    --data-binary @.tmp/manual-e2e/payloads/too-large.json
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 413
jq -e '.error=="limit_exceeded" and .message=="cache size limit exceeded"' .tmp/manual-e2e/responses/cache-put-too-large.json
```

Expected HTTP code: `413`

## 12. Verify Authentication And Authorization

These checks prove that protected routes really are protected.

### 12.1 Missing bearer token

```sh
http_code="$(
  curl --silent --show-error \
    --output .tmp/manual-e2e/responses/stats-missing-token.json \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    http://localhost/v1/stats
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 401
jq -e '.error=="unauthorized" and .message=="missing bearer token"' .tmp/manual-e2e/responses/stats-missing-token.json
```

Expected HTTP code: `401`

### 12.2 Invalid bearer token

```sh
http_code="$(
  curl --silent --show-error \
    --output .tmp/manual-e2e/responses/stats-invalid-token.json \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H 'Authorization: Bearer invalid-token' \
    http://localhost/v1/stats
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 401
jq -e '.error=="unauthorized" and .message=="invalid bearer token"' .tmp/manual-e2e/responses/stats-invalid-token.json
```

Expected HTTP code: `401`

### 12.3 Client token on an operator-only route

```sh
http_code="$(
  curl --silent --show-error \
    --output .tmp/manual-e2e/responses/stats-client-forbidden.json \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$CLIENT_AUTH_HEADER" \
    http://localhost/v1/stats
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 403
jq -e '.error=="forbidden" and .message=="insufficient role"' .tmp/manual-e2e/responses/stats-client-forbidden.json
```

Expected HTTP code: `403`

## 13. Verify Operator APIs

These checks use the `operator` token.

### 13.1 `GET /v1/stats`

```sh
http_code="$(
  curl --silent --show-error \
    --output .tmp/manual-e2e/responses/stats-operator.json \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$OPERATOR_AUTH_HEADER" \
    http://localhost/v1/stats
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 200
jq -e '.cache_sets >= 2 and .cache_deletes >= 1 and .client_registrations >= 3' .tmp/manual-e2e/responses/stats-operator.json
```

Expected HTTP code: `200`

### 13.2 `GET /v1/metrics/memory`

```sh
http_code="$(
  curl --silent --show-error \
    --output .tmp/manual-e2e/responses/memory-operator.json \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$OPERATOR_AUTH_HEADER" \
    http://localhost/v1/metrics/memory
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 200
jq -e '.entry_count >= 1 and .memory_limit_bytes == 268435456' .tmp/manual-e2e/responses/memory-operator.json
```

Expected HTTP code: `200`

### 13.3 `GET /v1/uptime`

```sh
http_code="$(
  curl --silent --show-error \
    --output .tmp/manual-e2e/responses/uptime-operator.json \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$OPERATOR_AUTH_HEADER" \
    http://localhost/v1/uptime
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 200
jq -e '.uptime_seconds >= 0 and .started_at_utc_seconds > 0' .tmp/manual-e2e/responses/uptime-operator.json
```

Expected HTTP code: `200`

### 13.4 `GET /v1/logs?tail=20`

```sh
http_code="$(
  curl --silent --show-error \
    --output .tmp/manual-e2e/responses/logs-operator.json \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$OPERATOR_AUTH_HEADER" \
    'http://localhost/v1/logs?tail=20'
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 200
jq -e '.tail_lines == 20 and (.text | length) > 0' .tmp/manual-e2e/responses/logs-operator.json
jq -r '.text' .tmp/manual-e2e/responses/logs-operator.json
```

Expected HTTP code: `200`

## 14. Verify Admin APIs

These checks use the second admin token. That proves admin features work for persisted admin principals, not only for the bootstrap token.

### 14.1 Store `gamma` so selective purge has a live target

```sh
http_code="$(
  curl --silent --show-error \
    --output .tmp/manual-e2e/responses/cache-put-gamma.json \
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
jq -e '.key=="gamma" and .ttl_seconds==60' .tmp/manual-e2e/responses/cache-put-gamma.json
```

Expected HTTP code: `200`

### 14.2 Selectively purge one real key and one missing key

```sh
http_code="$(
  curl --silent --show-error \
    --output .tmp/manual-e2e/responses/purge-selected.json \
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
jq -e '.requested_keys==2 and .purged_keys==1 and .missing_keys==1' .tmp/manual-e2e/responses/purge-selected.json
```

Expected HTTP code: `200`

### 14.3 Confirm `gamma` is gone

```sh
http_code="$(
  curl --silent --show-error \
    --output .tmp/manual-e2e/responses/cache-get-gamma-after-purge.json \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$CLIENT_AUTH_HEADER" \
    http://localhost/v1/cache/gamma
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 404
jq -e '.error=="not_found" and .message=="cache key not found"' .tmp/manual-e2e/responses/cache-get-gamma-after-purge.json
```

Expected HTTP code: `404`

### 14.4 Export state with the persisted admin token

```sh
http_code="$(
  curl --silent --show-error \
    --output .tmp/manual-e2e/responses/export-admin-2.json \
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
jq -e '.entry_count >= 1 and .client_count >= 3 and (.path | length > 0) and (.digest_sha256 | length > 0)' .tmp/manual-e2e/responses/export-admin-2.json

export EXPORT_PATH="$(jq -r '.path' .tmp/manual-e2e/responses/export-admin-2.json)"
```

Expected HTTP code: `200`

### 14.5 Wrong method on an admin route

```sh
http_code="$(
  curl --silent --show-error \
    --output .tmp/manual-e2e/responses/export-get-method.json \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$MANUAL_ADMIN_AUTH_HEADER" \
    http://localhost/v1/export
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 405
jq -e '.error=="invalid_argument" and .message=="method not allowed"' .tmp/manual-e2e/responses/export-get-method.json
```

Expected HTTP code: `405`

### 14.6 Rotate the client token

```sh
export OLD_CLIENT_TOKEN="$CLIENT_TOKEN"

http_code="$(
  curl --silent --show-error \
    --output .tmp/manual-e2e/responses/rotate-client-token.json \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$MANUAL_ADMIN_AUTH_HEADER" \
    -H 'Content-Type: application/json' \
    -X POST \
    http://localhost/v1/clients/manual-client/rotate-token \
    --data '{}'
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 200
jq -e '.client_id=="manual-client" and (.token | length > 0)' .tmp/manual-e2e/responses/rotate-client-token.json

export CLIENT_TOKEN="$(jq -r '.token' .tmp/manual-e2e/responses/rotate-client-token.json)"
export CLIENT_AUTH_HEADER="Authorization: Bearer $CLIENT_TOKEN"
```

Expected HTTP code: `200`

### 14.7 Confirm the old client token no longer works

```sh
http_code="$(
  curl --silent --show-error \
    --output .tmp/manual-e2e/responses/cache-get-beta-old-token.json \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "Authorization: Bearer $OLD_CLIENT_TOKEN" \
    http://localhost/v1/cache/beta
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 401
jq -e '.error=="unauthorized" and .message=="invalid bearer token"' .tmp/manual-e2e/responses/cache-get-beta-old-token.json
```

Expected HTTP code: `401`

### 14.8 Confirm the new client token works

```sh
http_code="$(
  curl --silent --show-error \
    --output .tmp/manual-e2e/responses/cache-get-beta-new-token.json \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$CLIENT_AUTH_HEADER" \
    http://localhost/v1/cache/beta
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 200
jq -e '.key=="beta" and .value_base64=="d29ybGQ="' .tmp/manual-e2e/responses/cache-get-beta-new-token.json
```

Expected HTTP code: `200`

### 14.9 Invalidate the current client token

```sh
export INVALIDATED_CLIENT_TOKEN="$CLIENT_TOKEN"

http_code="$(
  curl --silent --show-error \
    --output .tmp/manual-e2e/responses/invalidate-client-token.json \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$MANUAL_ADMIN_AUTH_HEADER" \
    -H 'Content-Type: application/json' \
    -X POST \
    http://localhost/v1/clients/manual-client/invalidate-token \
    --data '{}'
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 200
jq -e '.client_id=="manual-client" and .token_active==false' .tmp/manual-e2e/responses/invalidate-client-token.json
```

Expected HTTP code: `200`

### 14.10 Confirm the invalidated token fails

```sh
http_code="$(
  curl --silent --show-error \
    --output .tmp/manual-e2e/responses/cache-get-beta-invalidated-token.json \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "Authorization: Bearer $INVALIDATED_CLIENT_TOKEN" \
    http://localhost/v1/cache/beta
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 401
jq -e '.error=="unauthorized" and .message=="invalid bearer token"' .tmp/manual-e2e/responses/cache-get-beta-invalidated-token.json
```

Expected HTTP code: `401`

### 14.11 Rotate again to reissue a fresh token

```sh
http_code="$(
  curl --silent --show-error \
    --output .tmp/manual-e2e/responses/rotate-client-token-reissued.json \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$MANUAL_ADMIN_AUTH_HEADER" \
    -H 'Content-Type: application/json' \
    -X POST \
    http://localhost/v1/clients/manual-client/rotate-token \
    --data '{}'
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 200
jq -e '.client_id=="manual-client" and (.token | length > 0)' .tmp/manual-e2e/responses/rotate-client-token-reissued.json

export CLIENT_TOKEN="$(jq -r '.token' .tmp/manual-e2e/responses/rotate-client-token-reissued.json)"
export CLIENT_AUTH_HEADER="Authorization: Bearer $CLIENT_TOKEN"
```

Expected HTTP code: `200`

### 14.12 Confirm the reissued token works

```sh
http_code="$(
  curl --silent --show-error \
    --output .tmp/manual-e2e/responses/cache-get-beta-reissued-token.json \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$CLIENT_AUTH_HEADER" \
    http://localhost/v1/cache/beta
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 200
jq -e '.key=="beta" and .value_base64=="d29ybGQ="' .tmp/manual-e2e/responses/cache-get-beta-reissued-token.json
```

Expected HTTP code: `200`

### 14.13 Export again so later import restores the latest token state

```sh
http_code="$(
  curl --silent --show-error \
    --output .tmp/manual-e2e/responses/export-admin-latest.json \
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
jq -e '.entry_count >= 1 and .client_count >= 3 and (.path | length > 0)' .tmp/manual-e2e/responses/export-admin-latest.json

export EXPORT_PATH="$(jq -r '.path' .tmp/manual-e2e/responses/export-admin-latest.json)"
```

Expected HTTP code: `200`

### 14.14 Confirm token-lifecycle counters increased

```sh
http_code="$(
  curl --silent --show-error \
    --output .tmp/manual-e2e/responses/stats-admin-after-token-lifecycle.json \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$MANUAL_ADMIN_AUTH_HEADER" \
    http://localhost/v1/stats
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 200
jq -e '.token_rotations >= 2 and .token_invalidations >= 1 and .cache_deletes >= 2' .tmp/manual-e2e/responses/stats-admin-after-token-lifecycle.json
```

Expected HTTP code: `200`

### 14.15 Create a tampered export and confirm import rejection

```sh
cp "$EXPORT_PATH" .tmp/manual-e2e/exports/tampered-export.bin
printf 'X' | dd of=.tmp/manual-e2e/exports/tampered-export.bin bs=1 seek=32 conv=notrunc status=none
```

Now try to import the tampered file.

```sh
http_code="$(
  curl --silent --show-error \
    --output .tmp/manual-e2e/responses/import-tampered.json \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$MANUAL_ADMIN_AUTH_HEADER" \
    -H 'Content-Type: application/json' \
    -X POST \
    http://localhost/v1/import \
    --data '{"path":".tmp/manual-e2e/exports/tampered-export.bin"}'
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 400
jq -e '.error=="invalid_argument" and (.message | contains("state import failed integrity or bounds checks"))' .tmp/manual-e2e/responses/import-tampered.json
```

Expected HTTP code: `400`

### 14.16 Confirm invalid import paths are rejected

```sh
http_code="$(
  curl --silent --show-error \
    --output .tmp/manual-e2e/responses/import-invalid-path.json \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$MANUAL_ADMIN_AUTH_HEADER" \
    -H 'Content-Type: application/json' \
    -X POST \
    http://localhost/v1/import \
    --data '{"path":"../outside.bin"}'
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 400
jq -e '.error=="invalid_argument" and (.message | contains("state import failed integrity or bounds checks"))' .tmp/manual-e2e/responses/import-invalid-path.json
```

Expected HTTP code: `400`

### 14.17 Purge all cache state

```sh
http_code="$(
  curl --silent --show-error \
    --output .tmp/manual-e2e/responses/purge-all.json \
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
jq -e '.purged==true' .tmp/manual-e2e/responses/purge-all.json
```

Expected HTTP code: `200`

### 14.18 Confirm `beta` is gone after purge

```sh
http_code="$(
  curl --silent --show-error \
    --output .tmp/manual-e2e/responses/cache-get-beta-after-purge.json \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$CLIENT_AUTH_HEADER" \
    http://localhost/v1/cache/beta
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 404
jq -e '.error=="not_found" and .message=="cache key not found"' .tmp/manual-e2e/responses/cache-get-beta-after-purge.json
```

Expected HTTP code: `404`

### 14.19 Import the valid export back in

```sh
http_code="$(
  curl --silent --show-error \
    --output .tmp/manual-e2e/responses/import-valid.json \
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
jq -e --arg path "$EXPORT_PATH" '.imported==true and .path==$path' .tmp/manual-e2e/responses/import-valid.json
```

Expected HTTP code: `200`

### 14.20 Confirm `beta` is restored

```sh
http_code="$(
  curl --silent --show-error \
    --output .tmp/manual-e2e/responses/cache-get-beta-after-import.json \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$CLIENT_AUTH_HEADER" \
    http://localhost/v1/cache/beta
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 200
jq -e '.key=="beta" and .value_base64=="d29ybGQ="' .tmp/manual-e2e/responses/cache-get-beta-after-import.json
```

Expected HTTP code: `200`

## 15. Verify Rate Limiting

The config in this runbook sets:

- `rate_limit_requests = 240`
- `rate_limit_window_seconds = 60`

That means request `241` inside the same one-minute window should fail with `429`.

### 15.1 Create a fresh operator for the rate-limit test

This avoids interference from the operator requests you already sent earlier.

```sh
http_code="$(
  curl --silent --show-error \
    --output .tmp/manual-e2e/responses/register-rate-limit-operator.json \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$ADMIN_AUTH_HEADER" \
    -H 'Content-Type: application/json' \
    -X POST \
    http://localhost/v1/clients \
    --data '{"client_id":"manual-rate-limit-operator","role":"operator"}'
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 200
jq -e '.client_id=="manual-rate-limit-operator" and .role=="operator"' .tmp/manual-e2e/responses/register-rate-limit-operator.json

export RATE_LIMIT_OPERATOR_TOKEN="$(jq -r '.token' .tmp/manual-e2e/responses/register-rate-limit-operator.json)"
```

Expected HTTP code: `200`

### 15.2 Send 241 operator requests

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
```

### 15.3 Inspect the important lines

```sh
sed -n '1p;240p;241p' .tmp/manual-e2e/responses/rate-limit-codes.txt
```

You should see:

- attempt `1` returning `200`
- attempt `240` returning `200`
- attempt `241` returning `429`

### 15.4 Enforce that expectation

```sh
awk 'NR==240 { ok_240 = ($2 == "200") } NR==241 { ok_241 = ($2 == "429") } END { exit !(ok_240 && ok_241) }' .tmp/manual-e2e/responses/rate-limit-codes.txt
jq -e '.error=="limit_exceeded" and .message=="rate limit exceeded"' .tmp/manual-e2e/responses/rate-limit-241.json
```

If this block passes, the rate limiter behaved correctly.

## 16. Verify Persistence Across Restart

Now prove that data survives a stop-and-start cycle.

### 16.1 Write a key that should survive restart

```sh
http_code="$(
  curl --silent --show-error \
    --output .tmp/manual-e2e/responses/cache-put-persist.json \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$CLIENT_AUTH_HEADER" \
    -H 'Content-Type: application/json' \
    -X PUT \
    http://localhost/v1/cache/persist-key \
    --data '{"value_base64":"cGVyc2lzdC1vaw==","ttl_seconds":600}'
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 200
jq -e '.key=="persist-key" and .ttl_seconds==600' .tmp/manual-e2e/responses/cache-put-persist.json
```

Expected HTTP code: `200`

### 16.2 Stop and restart the server

```sh
./scripts/stop-local-server.sh
./scripts/run-local-server.sh
```

### 16.3 Confirm the cache value survived

```sh
http_code="$(
  curl --silent --show-error \
    --output .tmp/manual-e2e/responses/cache-get-persist-after-restart.json \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$CLIENT_AUTH_HEADER" \
    http://localhost/v1/cache/persist-key
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 200
jq -e '.key=="persist-key" and .value_base64=="cGVyc2lzdC1vaw=="' .tmp/manual-e2e/responses/cache-get-persist-after-restart.json
```

Expected HTTP code: `200`

### 16.4 Confirm operator and admin state also survived

```sh
http_code="$(
  curl --silent --show-error \
    --output .tmp/manual-e2e/responses/stats-after-restart.json \
    --write-out '%{http_code}' \
    --unix-socket "$MP_SOCKET_PATH" \
    -H "$OPERATOR_AUTH_HEADER" \
    http://localhost/v1/stats
)"
printf '%s\n' "$http_code"
expect_code "$http_code" 200
jq -e '.entry_count >= 1' .tmp/manual-e2e/responses/stats-after-restart.json
```

And confirm the admin can still export the restored state:

```sh
http_code="$(
  curl --silent --show-error \
    --output .tmp/manual-e2e/responses/export-after-restart.json \
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
jq -e '.entry_count >= 1 and .client_count >= 4' .tmp/manual-e2e/responses/export-after-restart.json
```

Expected HTTP code: `200` for both commands above.

## 17. Optional Valgrind Validation

If your host is already prepared for Valgrind, run:

```sh
make test-valgrind
```

If Valgrind fails before it starts the project tests, follow `docs/runbooks/valgrind-host-setup.md`.

## 18. Cleanup

### 18.1 Stop the isolated server

```sh
./scripts/stop-local-server.sh
```

### 18.2 Remove the test-only artifacts

```sh
rm -rf .tmp/manual-e2e
```

Only run that cleanup command if you are done inspecting the saved responses, exports, and logs.

## 19. Expected Outcome

If you reached the end without errors, you proved that:

- `mp-cache` builds on a normal CachyOS shell
- isolated local secrets are resolved correctly
- the Unix-socket service starts and answers health checks
- the documented HTTP routes behave correctly
- auth and role boundaries are enforced
- token rotation and token invalidation work correctly
- export and import behave correctly, including tamper rejection
- selective purge and purge-all work correctly
- oversized payloads, conflicts, wrong methods, bad auth, bad roles, invalid inputs, and unknown routes return the expected errors
- rate limiting works as configured
- cache data and principal state survive restart

If you want a shorter day-to-day flow after your first manual pass, use:

- `./scripts/build-local.sh`
- `./scripts/run-local-server.sh`
- `./scripts/test-local.sh`
- `./scripts/stop-local-server.sh`
