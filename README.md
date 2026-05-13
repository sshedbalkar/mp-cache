# mp-cache

Unix-socket cache server written in C17 for local S2S deployment on Linux systems.

Project state: **Phase 4 baseline implemented**.

This project is a standalone C cache server repository with its own docs, context, scripts, and build.

## Scope

Target system characteristics:

- local transport through a Unix domain socket;
- remote access only through controlled SSH tunneling and a proxy layer such as Nginx;
- in-memory primary cache with bounded keys, bounded values, and configurable memory limits;
- encrypted local file-backed persistence and export/import workflows;
- role-based API access for `admin`, `operator`, and `client`;
- UTC-only time semantics;
- long-running process with graceful start, stop, and restart behavior.

## Implemented Baseline

Current repository features:

- bearer-token authentication with `admin`, `operator`, and `client` roles;
- authenticated `GET` / `PUT` / `DELETE` cache APIs plus health, stats, memory, uptime, and log-tail endpoints;
- encrypted journal, encrypted checkpoint, and integrity-checked export/import files;
- persisted client registry with admin registration, token rotation, and explicit token invalidation;
- admin selective key-purge and purge-all APIs;
- per-principal rate limiting on authenticated APIs;
- local build, benchmark, sanitizer, and Valgrind validation scripts;
- systemd-ready deployment assets under `deploy/systemd/`.

Still future-facing:

- live Unix-socket integration coverage inside CI or a less-restricted runtime than this sandbox.

## Project Standards

Authoritative local standards:

- `docs/engineering-standards.md`
- `docs/naming-strategy.md`
- `docs/commit-messages.md`

The naming strategy applies to `cmd/`, `scripts/`, and `internal/`.

## Prerequisites

Required local tooling:

- `cmake`
- `make`
- `gcc` or `clang`
- `git`
- `curl`

Optional local tooling:

- `valgrind`
- `shellcheck`
- `jq`
- `mise`

Validated local host example:

- Arch Linux with CachyOS kernel

## Build

```sh
make build
```

Primary commands:

```sh
make native-config
make native-build
make test
make test-full
make test-naming-strategy
make test-hardening
make test-valgrind
make benchmark
make package
make deploy-local
make stop-local
make restart-local
```

`make test` is the rootless local validation suite. It runs the unit tests, naming checks, standards checks, and sanitizer-based hardening checks without requiring elevated privileges. `make test-valgrind` stays separate because some hosts need additional package or container setup before Valgrind can execute successfully. Use `make test-full` when the host is already prepared for both.

On Arch Linux and CachyOS, Valgrind can fail before any project code runs when the system dynamic loader is stripped and matching glibc debuginfo is missing. If `make test-valgrind` reports that host blocker, populate the loader debuginfo cache and rerun Valgrind:

```sh
DEBUGINFOD_URLS="${DEBUGINFOD_URLS:-https://debuginfod.archlinux.org https://debuginfod.cachyos.org}" debuginfod-find debuginfo /lib64/ld-linux-x86-64.so.2
make test-valgrind
```

If `debuginfod-find` cannot fetch the loader symbols, install the matching `glibc-debug` package from your enabled debug repository and rerun the command.

On CachyOS `znver4` systems, a second host-level blocker can remain after debuginfo is available: the installed `glibc` loader may use `x86_64_v4` instructions that current Valgrind cannot emulate. In that case, switch `glibc` and `lib32-glibc` to the generic Arch `core` packages before rerunning Valgrind, or run the Valgrind step inside a generic Arch container or chroot.

Build output:

```text
build/local-debug/
  mp-cache-server
  mp-cachectl
```

## Local Run

Bootstrap and run:

```sh
mkdir -p .tmp/secrets
cp configs/secrets/templates/local.env.template .tmp/secrets/local.env
./scripts/build-local.sh
./scripts/run-local-server.sh
./scripts/test-local.sh
./scripts/stop-local-server.sh
```

After the required build tools are installed, the local build, deploy, and test commands are intended to run from a normal non-root shell.

Local secrets are sourced from `.tmp/secrets/local.env`. The default bootstrap config expects:

- `MP_SECRET_LOCAL_BOOTSTRAP_ADMIN_TOKEN`
- `MP_SECRET_LOCAL_STORAGE_KEY`

If local startup fails because a previous `.tmp/data/state.checkpoint` or `.tmp/data/state.journal` was written with a different `MP_SECRET_LOCAL_STORAGE_KEY`, the local helper scripts now rotate those files to timestamped `.bak` copies and retry once with a clean cache. Set `MP_AUTO_RESET_LOCAL_STATE_ON_LOAD_FAILURE=0` to keep the old strict-fail behavior.

By default the server uses:

- config path: `configs/bootstrap.ini`
- socket path: `/tmp/mp-cache/run/mp-cache.sock`
- pid file: `/tmp/mp-cache/run/mp-cache.pid`
- log directory: `logging/`

If `configs/bootstrap.ini` is missing at startup, the server uses compiled defaults, writes a commented bootstrap template to that path, and continues with those defaults.

The default cache TTL is configurable through the bootstrap config file and defaults to `172800` seconds.

Primary `mp-cachectl` commands:

```sh
./build/local-debug/mp-cachectl health
MP_CACHE_TOKEN="$MP_SECRET_LOCAL_BOOTSTRAP_ADMIN_TOKEN" ./build/local-debug/mp-cachectl stats
MP_CACHE_TOKEN="$MP_SECRET_LOCAL_BOOTSTRAP_ADMIN_TOKEN" ./build/local-debug/mp-cachectl register-client client-one client
MP_CACHE_TOKEN="$MP_SECRET_LOCAL_BOOTSTRAP_ADMIN_TOKEN" ./build/local-debug/mp-cachectl invalidate-client client-one
MP_CACHE_TOKEN="$MP_SECRET_LOCAL_BOOTSTRAP_ADMIN_TOKEN" ./build/local-debug/mp-cachectl purge-keys alpha beta
MP_CACHE_TOKEN="$MP_SECRET_LOCAL_BOOTSTRAP_ADMIN_TOKEN" ./build/local-debug/mp-cachectl export
```

Inside restricted sandboxes, Unix-socket `bind(2)` or client `connect(2)` may still be denied even when the build is healthy. In that case the local scripts now report the sandbox restriction explicitly, and the live socket smoke test must be rerun from a normal local shell.

## Deployment Layout

The local scripts prepare these writable runtime paths:

```text
/tmp/mp-cache/run/
.tmp/data/
.tmp/exports/
logging/
```

Deployment docs:

- `deploy/local/README.md`
- `deploy/qa/README.md`
- `deploy/staging/README.md`
- `deploy/production/README.md`
- `deploy/secrets/README.md`
- `deploy/systemd/README.md`

## Docs

- architecture: `docs/architecture/mp-cache-architecture.md`
- engineering standards: `docs/engineering-standards.md`
- naming strategy: `docs/naming-strategy.md`
- commit messages: `docs/commit-messages.md`
- manual end-to-end testing runbook for CachyOS: `docs/runbooks/manual-end-to-end-testing.md`
- Valgrind host setup runbook: `docs/runbooks/valgrind-host-setup.md`
- API contract: `api/http/v1/cache-service.md`
- runbooks: `docs/runbooks/`
- retrieval context: `context/`

## Layout

```text
cmd/
internal/
api/
configs/
deploy/
docs/
context/
native/
scripts/
tests/
tools/
```
