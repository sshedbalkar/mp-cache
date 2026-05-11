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
- persisted client registry with admin registration and token rotation;
- per-principal rate limiting on authenticated APIs;
- local build, benchmark, sanitizer, and Valgrind validation scripts;
- systemd-ready deployment assets under `deploy/systemd/`.

Still future-facing:

- selective key-purge API;
- explicit token invalidation beyond rotation;
- live Unix-socket integration coverage inside CI or a less-restricted runtime than this sandbox.

## Project Standards

Authoritative local standards:

- `docs/engineering-standards.md`
- `docs/commit-messages.md`

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

Current local target environment:

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
make benchmark
make package
make deploy-local
make stop-local
make restart-local
```

Build output:

```text
build/local-debug/
  mp-cache-server
  mp-cachectl
```

## Local Run

Bootstrap and run:

```sh
cp configs/secrets/templates/local.env.template .tmp/secrets/local.env
./scripts/build-local.sh
./scripts/run-local-server.sh
./scripts/test-local.sh
./scripts/stop-local-server.sh
```

Local secrets are sourced from `.tmp/secrets/local.env`. The default bootstrap config expects:

- `MP_SECRET_LOCAL_BOOTSTRAP_ADMIN_TOKEN`
- `MP_SECRET_LOCAL_STORAGE_KEY`

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
MP_CACHE_TOKEN="$MP_SECRET_LOCAL_BOOTSTRAP_ADMIN_TOKEN" ./build/local-debug/mp-cachectl export
```

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
- commit messages: `docs/commit-messages.md`
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
