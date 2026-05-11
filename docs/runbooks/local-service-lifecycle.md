# Runbook: Local Service Lifecycle

## Symptoms

- server does not start
- socket file missing
- stale pid file blocks startup
- health endpoint not responding

## Impact

Local development, validation, and manual testing are blocked.

## Preconditions

- local dependencies installed
- `.tmp/secrets/local.env` created from `configs/secrets/templates/local.env.template`
- build completed
- writable `.tmp/` and `logging/` paths

## Diagnosis

1. Run `./scripts/doctor-local.sh`.
2. Check `build/local-debug/mp-cache-server` exists.
3. Check `/tmp/mp-cache/run/mp-cache.pid` and `/tmp/mp-cache/run/mp-cache.sock`.
4. Inspect `logging/` and `.tmp/logs/console.log`.
5. Run `./build/local-debug/mp-cachectl health`.
6. Run `MP_CACHE_TOKEN="$MP_SECRET_LOCAL_BOOTSTRAP_ADMIN_TOKEN" ./build/local-debug/mp-cachectl stats`.

## Mitigation

1. `./scripts/stop-local-server.sh`
2. `./scripts/build-local.sh`
3. `./scripts/run-local-server.sh`
4. `./scripts/test-local.sh`

## Rollback

Stop the local server and revert to the last known-good artifact or config file.

## Verification

- `./build/local-debug/mp-cachectl health` returns `200`
- `MP_CACHE_TOKEN="$MP_SECRET_LOCAL_BOOTSTRAP_ADMIN_TOKEN" ./build/local-debug/mp-cachectl stats` returns cache and request counters
- `curl --unix-socket /tmp/mp-cache/run/mp-cache.sock http://localhost/health`

## Escalation

If the server builds but crashes on startup, capture the console output and the latest `logging/` entries before changing more state.
