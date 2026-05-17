# Engineering Standards

This document is the local engineering standard for `mp-cache`. It is authoritative for this repository.

## Decision Priority

1. Correctness, security, and data safety.
2. Maintainability, clear ownership, and replaceable modules.
3. Runtime performance and low overhead.
4. Recoverability, observability, and graceful operations.
5. Portability across supported Linux environments.
6. Delivery speed.

## Common Engineering

- Keep modules single-purpose and easy to replace.
- Keep important state exportable and importable.
- Treat cache contents as non-authoritative data.
- Validate all boundary input before use.
- Keep hot-path data structures bounded and explicit.
- Use UTC for all persisted and exported time values.

## Naming And Terminology

- Follow `docs/naming-strategy.md` for files, modules, entrypoints, scripts, functions, variables, metrics, config keys, and future wire or database names.
- Prefer names that narrow from boundary or domain to concept to role.
- Keep shared service terms stable across code, scripts, docs, and APIs.

## Configuration And Secrets

- Configuration must be explicit, layered, and reviewable.
- Safe defaults may exist in code for bootstrap and local development.
- Durable service constants, compiled defaults, config section/key names, environment variable names, protocol markers, role names, and reusable capacity limits must be defined in `internal/config/constants.h`.
- Do not scatter project-level constants or default values across `cmd/`, `internal/`, or tests; consuming modules must include the central constants header through their normal module boundary.
- If the bootstrap config file is missing, export a commented template and continue with safe defaults.
- Secrets must never be committed.
- Runtime secret sources are explicit only: `env:<name>` and `file:<absolute_path>`.
- Prefer mounted read-only secret files outside local development.

## Server And API

- Use stable versioned HTTP contracts for control-plane APIs.
- The local transport boundary is a Unix domain socket.
- Remote network exposure must stay outside the service process through controlled SSH tunnel and proxy infrastructure.
- Handlers must follow this order: parse, validate, authenticate, authorize, apply limits, execute service logic, map response, emit logs and metrics.
- Known path plus wrong method returns `405`; unknown path returns `404`.
- Every application error response must carry a project-defined `error_code`. Human text belongs in `error_description` and must not be used by clients for branching or retry decisions.
- Auth-sensitive responses default to `Cache-Control: no-store`.
- Large exports and imports must stay bounded and integrity-checked.

## C Service And CMake

- Use C17.
- Keep public APIs small and explicit.
- Use pointer-plus-length buffer APIs.
- Document ownership, lifetime, and error behavior.
- Return explicit status codes.
- Validate all lengths before reads and writes.
- Build through target-based CMake with warnings enabled.

## Security And Operations

- Authenticate before granting data or management access.
- Separate authentication from authorization.
- Enforce role-based access for `admin`, `operator`, and `client`.
- Redact secrets and tokens from logs and diagnostics.
- Expose liveness/readiness-style health signals and useful structured logs.
- All service and library logging must go through `native/mp_logger`, preferably via the local observability wrapper in `internal/observability/`.
- Do not add alternate logging dependencies or direct runtime log emission to `stdout`, `stderr`, or `syslog`.
- Direct stdio is reserved for user-facing CLI output, config/template emission, and fatal bootstrap diagnostics before `mp_logger` is initialized.
- Use least-privilege runtime identities and writable paths.

## Build, Release, And Deployment

- Builds must be reproducible from checked-in source and build config.
- Build scripts must live in the repository.
- Promote the same artifact through local, QA, staging, and production.
- Only config, secrets, limits, and bindings change between environments.
- Every packaged artifact should include version, commit, build time, and checksum.

## Testing And Validation

- Cover happy, sad, edge, hot, and recovery paths.
- Unit tests are required for core logic and config parsing.
- Changes under `cmd/`, `scripts/`, or `internal/` require `./scripts/test-naming-strategy.sh` or `make test-naming-strategy`.
- Native validation should use ASan, UBSan, and Valgrind where available.
- Any change to native code in `cmd/`, `internal/`, `tests/unit/`, or `native/` requires a Valgrind run before closeout, using `./scripts/test-valgrind.sh` or an equivalent wrapper such as `make test-valgrind`.
- If the current host cannot execute Valgrind successfully, treat that as a validation blocker unless the failure is an environment limitation that is captured and reported explicitly.
- `make test` is the local validation artifact.
- Context and durable docs must stay in sync with implementation changes.

## Folder Structure

- Keep committed config in `configs/`.
- Keep durable docs in `docs/`.
- Keep retrieval metadata in `context/`.
- Keep automation in `scripts/`.
- Keep repository-local temporary runtime, validation, and crash artifacts under `.tmp/` instead of the repository root.
- Keep unit and integration tests under `tests/`.
- Keep native or bundled third-party code under clearly bounded roots such as `native/`.

## Dependency Management

- Prefer standard library and small, conservative dependencies.
- Isolate third-party or submodule code behind clear boundaries.
- Avoid hidden global state and cyclic dependencies.
- Keep reusable interfaces consumer-owned where practical.

## Documentation

- Long-lived architecture, API, security, deployment, and workflow rules must live in repository docs.
- Update docs and context in the same change when durable facts change.
- Keep `context/` thin and route-oriented; keep rule bodies in durable docs, not in context indexes.
- Use a sentence-first documentation format for code comments.
- Public C APIs, public structs, public enums, callback types, and macros with behavioral meaning must carry comments directly above the declaration that describe purpose and any non-obvious ownership, lifetime, bounds, units, or error semantics.
- Non-trivial internal structs and file-local helpers must use the same format directly above the definition.
- Prefer `/* ... */` block comments for C declarations and definitions, and `// Name ...` doc comments for exported Go identifiers.
- Keep comments implementation-backed, concise, and synchronized with the current code path.
