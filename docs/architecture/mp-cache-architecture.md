# mp-cache Architecture

| Field | Value |
|:------|:------|
| Status | Accepted |
| Last Updated | 2026-05-11 |
| Owner | Project Architect |
| Applies To | `mp-cache` service, local tooling, deployment assets |

## Executive Summary

`mp-cache` is a Linux-hosted S2S cache service written in C17. It uses a Unix domain socket for local access, keeps hot data in memory, persists important state to encrypted local files, and exposes controlled management APIs for `admin` and `operator` roles.

The current baseline includes the Phase 1 scaffold plus the Phase 2 authenticated data plane, Phase 3 persistence and state portability features, and the Phase 4 hardening baseline described below.

## Goals

1. Keep the hot path local, bounded, and low overhead.
2. Keep deployment and recovery simple through exportable state.
3. Keep security explicit with role-based APIs, token management, and at-rest encryption.
4. Keep the standards and operational layout self-contained and predictable inside this repository.

## Non-Goals For The Bootstrap Phase

1. Do not implement the full admin/operator/client API surface in one step.
2. Do not add remote TCP listeners to the service itself.
3. Do not couple the service to a specific cloud provider or CI platform.

## Core Decisions

| Decision | Choice | Reason |
|:---------|:-------|:-------|
| Service language | C17 | Matches the native-service requirement and low-overhead goals. |
| Local transport | HTTP over Unix socket | Simple S2S control plane with standard tooling compatibility. |
| Remote exposure | SSH tunnel plus proxy such as Nginx | Keeps the service local-only while still enabling controlled network access. |
| Logging | `native/mp_logger` submodule | Reuses the existing bounded logger implementation and conventions. |
| Config format | YAML bootstrap config | Simple C parser, commentable, exportable on missing config. |
| Timezone | UTC only | Required by the product constraints and easier for export/import correctness. |
| Runtime shape | Long-running foreground process with signal-driven graceful shutdown | Simpler lifecycle, packaging, and operational control. |

## Planned Runtime Model

Startup order:

1. Load compiled defaults.
2. Load bootstrap config if present.
3. Export a commented bootstrap config if it is absent.
4. Ensure runtime directories exist.
5. Initialize `mp_logger`.
6. Initialize in-memory cache state.
7. Bind Unix socket and start request loop.
8. Serve health and future data-management APIs.

Shutdown order:

1. Stop accepting new connections.
2. Drain in-flight requests.
3. Flush logs.
4. Remove socket and pid files.

## Current API Surface

- `client`
  - `GET`
  - `SET`
  - `DELETE`
  - `Health`
- `operator`
  - all client APIs
  - uptime
  - stats
  - memory usage
  - logs access
- `admin`
  - all operator APIs
  - client registration
  - token rotation
  - token invalidation
  - export/import
  - selective key purge
  - purge all

## Future Extensions

- broader multi-file export partitioning

## Phases

### Phase 1: Bootstrap Scaffold

- project layout
- `mp_logger` integration
- config loader and template export
- Unix socket health service
- bounded in-memory cache module
- local scripts and docs

### Phase 2: Authenticated Data Plane

- status: implemented
- bearer token auth
- RBAC enforcement
- `GET`/`SET`/`DELETE` HTTP APIs
- stats and uptime endpoints

### Phase 3: Persistence And State Portability

- status: implemented
- encrypted append-only journal
- checkpoint files
- export/import commands and APIs
- client registry persistence

### Phase 4: Operations And Hardening

- status: implemented baseline
- logs API
- rate limiting
- import/export integrity validation
- systemd-ready deployment assets
- benchmarks and sanitizer/Valgrind gates
