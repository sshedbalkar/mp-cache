# Local Deployment

Use local deployment for development only.

Local commands:

0. `cp configs/secrets/templates/local.env.template .tmp/secrets/local.env`
1. `./scripts/build-local.sh`
2. `./scripts/run-local-server.sh`
3. `./scripts/test-local.sh`
4. `./scripts/stop-local-server.sh`

If the configured local storage key changes and the existing checkpoint or journal becomes unreadable, the local helper scripts rotate those files to timestamped `.bak` copies and retry once with a clean cache. Set `MP_AUTO_RESET_LOCAL_STATE_ON_LOAD_FAILURE=0` to disable that retry.

If the runtime blocks Unix-socket `bind(2)` or `connect(2)`, local smoke tests must be rerun from a normal local shell because the service transport is intentionally a Unix domain socket.

Writable local paths:

1. `/tmp/mp-cache/run/`
2. `.tmp/data/`
3. `.tmp/exports/`
4. `logging/`
