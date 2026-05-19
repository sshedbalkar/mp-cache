# Deployment Runbook

This runbook covers local service deployment and remote Development, QA, Staging, and Production deployment for `mp-cache`.

## Local Auto-Start Deployment

Use local auto-start deployment when you want the repository build to run continuously behind the local Nginx server.

Command:

```sh
./scripts/deploy-local.sh
```

The script builds `build/local-debug/mp-cache-server`, creates `dist/local/mp-cache-local.tar.gz`, stages the service binary from that artifact, writes local secrets if needed, generates a host-specific config under `.tmp/deploy/local/`, installs a `mp-cache-local.service` systemd unit that runs as the current local user, installs an Nginx proxy config, restarts the service, reloads Nginx, verifies the proxied health endpoint, and writes `.tmp/deploy/local/reports/deploy-local-report.md`.

Local defaults:

1. Build directory: `build/local-debug`.
2. Local artifact: `dist/local/mp-cache-local.tar.gz`.
3. Service socket: `/run/mp-cache-local/mp-cache.sock`.
4. Nginx listener: `127.0.0.1:8080`.
5. Nginx location prefix: `/cache`.
6. Nginx path constants: `configs/deploy/nginx-paths.env`.
7. State directory: `/var/lib/mp-cache-local`.
8. Log directory: `/var/log/mp-cache-local`.
9. Deployment report: `.tmp/deploy/local/reports/deploy-local-report.md`.

Common overrides:

```sh
MP_LOCAL_DEPLOY_BUILD=0 ./scripts/deploy-local.sh
MP_LOCAL_DEPLOY_USE_ARTIFACT=0 ./scripts/deploy-local.sh
MP_LOCAL_DEPLOY_ARTIFACT="$PWD/dist/local/mp-cache-local.tar.gz" ./scripts/deploy-local.sh
MP_LOCAL_DEPLOY_NGINX_LISTEN=127.0.0.1:18080 ./scripts/deploy-local.sh
MP_CACHE_NGINX_LOCATION_PATH=/cache ./scripts/deploy-local.sh
MP_LOCAL_DEPLOY_BUILD_DIR="$PWD/build/local-debug" ./scripts/deploy-local.sh
MP_LOCAL_DEPLOY_REPORT_DIR="$PWD/.tmp/deploy/local/reports" ./scripts/deploy-local.sh
```

After a successful local deployment, run the post-deployment smoke suite:

```sh
./scripts/test-local-deployment.sh
make test-local-deployment
```

This verifies the installed systemd service, the local Nginx proxy, unauthenticated health, admin client registration, authenticated cache write/read/delete, operator stats through admin access, and token invalidation. It writes `.tmp/deploy/local/reports/post-deployment-test-report.md` and stores the captured HTTP responses under a run-specific directory beside that report. If you deployed with `MP_LOCAL_DEPLOY_NGINX_LISTEN`, `MP_LOCAL_DEPLOY_SERVICE_NAME`, or `MP_CACHE_NGINX_LOCATION_PATH` overrides, pass the same values to the post-deployment test command.

To create the local artifact without installing the service, run:

```sh
./scripts/build-local.sh
make build
make build-artifact
```

Use `./scripts/run-local-server.sh` when you want the older rootless foreground-style development flow instead of a boot-time service.

## Remote Deployment

Remote deployments promote one packaged artifact through Development, QA, Staging, and Production. Do not rebuild between environments.

Package locally:

```sh
make test
make package VERSION=<version> COMMIT=<commit> BUILD_TIME=<utc-time>
```

Deploy to Development:

```sh
./scripts/deploy-development.sh dist/mp-cache-<version>.tar.gz deploy@dev-host
make deploy-development ARTIFACT=dist/mp-cache-<version>.tar.gz SSH_TARGET=deploy@dev-host
```

Promote and deploy to QA, Staging, and Production:

```sh
./scripts/promote-artifact.sh dist/mp-cache-<version>.tar.gz development qa
./scripts/deploy-qa.sh dist/promotions/qa/mp-cache-<version>.tar.gz deploy@qa-host
make deploy-qa ARTIFACT=dist/promotions/qa/mp-cache-<version>.tar.gz SSH_TARGET=deploy@qa-host

./scripts/promote-artifact.sh dist/promotions/qa/mp-cache-<version>.tar.gz qa staging
./scripts/deploy-staging.sh dist/promotions/staging/mp-cache-<version>.tar.gz deploy@staging-host
make deploy-staging ARTIFACT=dist/promotions/staging/mp-cache-<version>.tar.gz SSH_TARGET=deploy@staging-host

./scripts/promote-artifact.sh dist/promotions/staging/mp-cache-<version>.tar.gz staging production
./scripts/deploy-production.sh dist/promotions/production/mp-cache-<version>.tar.gz deploy@prod-host
make deploy-production ARTIFACT=dist/promotions/production/mp-cache-<version>.tar.gz SSH_TARGET=deploy@prod-host
```

Remote scripts install each release under `/opt/mp-cache/releases/`, update `/opt/mp-cache/current`, create the dedicated `mp-cache` system user and group when missing, install or refresh `mp-cache.service`, restart the service, and optionally install an Nginx proxy. When Nginx is installed by the script, its configured worker user is added to the `mp-cache` group so it can connect to the service Unix socket. Remote services run as the dedicated `mp-cache` identity by default. Set `MP_REMOTE_DEPLOY_INSTALL_NGINX=0` when the proxy is managed by another host automation layer.

Nginx filesystem paths and the location prefix are centralized in `configs/deploy/nginx-paths.env`. Deployment scripts source that file through `scripts/lib/nginx-env.sh`; override `MP_NGINX_PATH_CONFIG_FILE` or individual `MP_CACHE_NGINX_*` values only when a host has a different Nginx layout.

The systemd unit uses `RequiresMountsFor=` for `/opt/mp-cache`, `/var/lib/mp-cache`, `/var/log/mp-cache`, and `/run/secrets/mp-cache`, so startup waits for the backing SSD or other mounted volume paths before launching the service.

Remote host expectations:

1. `ssh`, `scp`, `sudo`, `systemd`, `tar`, and Nginx are available.
2. Secret files exist at `/run/secrets/mp-cache/bootstrap-admin-token` and `/run/secrets/mp-cache/storage-key`.
3. The deploy identity can run the required `sudo` commands on the target host.
4. Remote HTTP exposure stays behind Nginx or an audited proxy boundary.

## Remote Service Lifecycle

Remote stop and restart commands mirror the local lifecycle targets, but they require an SSH target.

Development:

```sh
make stop-development SSH_TARGET=deploy@dev-host
make restart-development SSH_TARGET=deploy@dev-host
```

QA:

```sh
make stop-qa SSH_TARGET=deploy@qa-host
make restart-qa SSH_TARGET=deploy@qa-host
```

Staging:

```sh
make stop-staging SSH_TARGET=deploy@staging-host
make restart-staging SSH_TARGET=deploy@staging-host
```

Production:

```sh
make stop-production SSH_TARGET=deploy@prod-host
make restart-production SSH_TARGET=deploy@prod-host
```

The equivalent scripts are `./scripts/stop-<environment>.sh <ssh-target>` and `./scripts/restart-<environment>.sh <ssh-target>`.
