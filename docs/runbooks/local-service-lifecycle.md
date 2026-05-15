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
- normal non-root shell
- `.tmp/secrets/local.env` created by `./scripts/write-local-secret-env.sh` or from `configs/secrets/templates/local.env.template`
- build completed
- writable `.tmp/` and `logging/` paths

## Diagnosis

1. Run `./scripts/doctor-local.sh`.
2. Check `build/local-debug/mp-cache-server` exists.
3. Check `.tmp/run/mp-cache.pid` and `.tmp/run/mp-cache.sock`.
4. Inspect `logging/` and `.tmp/logs/console.log`.
5. Run `./build/local-debug/mp-cachectl health`.
6. Run `MP_CACHE_TOKEN="$MP_SECRET_LOCAL_BOOTSTRAP_ADMIN_TOKEN" ./build/local-debug/mp-cachectl stats`.
7. If startup logs mention checkpoint or journal load failure after a storage-key change, look for timestamped `.bak` files under the configured data directory.
8. If startup or health checks mention Unix-socket permission denial, rerun the live smoke test outside the sandbox.

## Mitigation

1. `./scripts/stop-local-server.sh`
2. `./scripts/build-local.sh`
3. `./scripts/run-local-server.sh`
4. `./scripts/test-local.sh`

`./scripts/run-local-server.sh` now retries once after rotating unreadable local checkpoint and journal files to timestamped backups. `./scripts/test-local.sh` covers the full rootless validation suite and keeps Valgrind separate as an explicit host-prepared follow-up step.

## Rollback

Stop the local server and revert to the last known-good artifact or config file.

## Verification

- `./build/local-debug/mp-cachectl health` returns `200`
- `MP_CACHE_TOKEN="$MP_SECRET_LOCAL_BOOTSTRAP_ADMIN_TOKEN" ./build/local-debug/mp-cachectl stats` returns cache and request counters
- `curl --unix-socket .tmp/run/mp-cache.sock http://localhost/health`
- for a faster script-driven local deploy before manual API checks, use `docs/runbooks/manual-api-testing-with-scripted-local-deploy.md`
- for full host-side API coverage, use `docs/runbooks/manual-end-to-end-testing.md`

## Escalation

If the server builds but crashes on startup, capture the console output and the latest `logging/` entries before changing more state.
