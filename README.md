# mp-cache

Unix-socket cache server written in C17 for local S2S deployment on Linux systems.

Project state: **bootstrap / MVP scaffold**.

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

## Current Bootstrap Implementation

The initial scaffold created in this change provides:

- project structure aligned to the local standards in this repository;
- `mp_logger` integrated as a git submodule under `native/mp_logger`;
- CMake and Make build wrappers;
- bootstrap config defaults with export-on-missing behavior;
- a buildable long-running server skeleton;
- Unix socket listener with health endpoints;
- a bounded in-memory cache module with TTL-aware unit tests;
- local lifecycle, validation, packaging, and promotion scripts;
- local context, architecture, API, and runbook docs.

Not yet implemented in this scaffold:

- full authenticated data APIs;
- persistent encrypted journal and checkpoint storage;
- client registration and token rotation APIs;
- export/import file execution;
- logs API and full operator/admin control plane.

Those are documented in the architecture and API specs as planned next phases.

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
./scripts/build-local.sh
./scripts/run-local-server.sh
./scripts/test-local.sh
./scripts/stop-local-server.sh
```

By default the server uses:

- config path: `configs/bootstrap.ini`
- socket path: `/tmp/mp-cache/run/mp-cache.sock`
- pid file: `/tmp/mp-cache/run/mp-cache.pid`
- log directory: `logging/`

If `configs/bootstrap.ini` is missing at startup, the server uses compiled defaults, writes a commented bootstrap template to that path, and continues with those defaults.

The default cache TTL is configurable through the bootstrap config file and defaults to `172800` seconds.

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
