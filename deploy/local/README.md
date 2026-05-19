# Local Deployment

Use local deployment for development only.

For an auto-start local service behind Nginx, run:

```sh
./scripts/deploy-local.sh
```

The auto-start script uses the current local user, builds `build/local-debug`, creates `dist/local/mp-cache-local.tar.gz`, stages binaries from that artifact by default, installs `mp-cache-local.service`, configures local Nginx, verifies the proxied health endpoint, and writes `.tmp/deploy/local/reports/deploy-local-report.md`. Re-run the same command for recurring local deployment after code changes.

After deployment, run:

```sh
./scripts/test-local-deployment.sh
```

The post-deployment smoke test exercises the installed service through Nginx and writes `.tmp/deploy/local/reports/post-deployment-test-report.md` with captured HTTP responses beside it.

Nginx filesystem paths and the `/cache` location prefix are centralized in `configs/deploy/nginx-paths.env`.

To create the default local artifact without installing the service, run `./scripts/build-local.sh` or `make build-artifact`.

The older rootless command sequence remains useful for short-lived manual testing from a normal non-root shell.

Local commands:

0. `./scripts/write-local-secret-env.sh`
1. `./scripts/build-local.sh`
2. `./scripts/run-local-server.sh`
3. `./scripts/test-local.sh`
4. `./scripts/test-local-deployment.sh`
5. `./scripts/stop-local-server.sh`

If you need a fresh local secret file, use `./scripts/write-local-secret-env.sh --force`.

If you want a faster scripted setup before manually calling the HTTP APIs yourself, use `docs/runbooks/manual-api-testing-with-scripted-local-deploy.md`.

`./scripts/test-local.sh` now runs the full rootless validation suite, including sanitizers, and leaves `./scripts/test-valgrind.sh` as the only separate host-prepared memory check.

If the configured local storage key changes and the existing checkpoint or journal becomes unreadable, the local helper scripts rotate those files to timestamped `.bak` copies and retry once with a clean cache. Set `MP_AUTO_RESET_LOCAL_STATE_ON_LOAD_FAILURE=0` to disable that retry.

If the runtime blocks Unix-socket `bind(2)` or `connect(2)`, local smoke tests must be rerun from a normal local shell because the service transport is intentionally a Unix domain socket.

Writable local paths:

1. `.tmp/run/`
2. `.tmp/data/`
3. `.tmp/exports/`
4. `logging/`
