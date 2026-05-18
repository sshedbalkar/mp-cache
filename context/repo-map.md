# Repo Map

## Lookup Strategy

- primary_lookup: section + stable path
- ordering: fixed framework sections first, alphabetical within section
- jump_index: present

## Jump Index

- source-truth: `README.md`, `AI_PERSONA.md`, `docs/engineering-standards.md`, `docs/naming-strategy.md`, `docs/commit-messages.md`, local durable docs
- git-workflow-root: `docs/commit-messages.md`
- build-deploy-roots: `Makefile`, `CMakeLists.txt`, `configs/build/CMakePresets.json`, `scripts/*.sh`, `deploy/*/README.md`
- implementation-roots: `cmd/`, `internal/`, `native/mp_logger`
- api-doc-roots: `api/http/v1/`
- validation-roots: `tests/`, `scripts/check-context.sh`, `scripts/check-standards.sh`, `scripts/test-hardening.sh`, `scripts/test-naming-strategy.sh`, `scripts/test-unit.sh`, `scripts/test-valgrind.sh`

## Source Truth

- `README.md`: project foundation, scope, commands, layout.
- `AI_PERSONA.md`: decision priorities and architecture posture.
- `docs/engineering-standards.md`: local engineering, API, security, build, testing, and layout rules.
- `docs/naming-strategy.md`: local naming strategy for scripts, entrypoints, variables, and external names.
- `docs/commit-messages.md`: authoritative local Git commit-message standard.
- `docs/architecture/mp-cache-architecture.md`: accepted service architecture and phased implementation plan.
- `api/http/v1/cache-service.md`: HTTP API contract and role model.
- `docs/runbooks/local-service-lifecycle.md`: local lifecycle runbook.
- `docs/runbooks/deployment.md`: local auto-start, Nginx proxy, and remote environment deployment runbook.
- `docs/runbooks/manual-api-testing-with-scripted-local-deploy.md`: faster script-driven local deploy before manual API checks.
- `docs/runbooks/manual-end-to-end-testing.md`: exhaustive isolated manual validation runbook.

## Implementation Roots

- `cmd/mp-cache-server`: server entrypoint.
- `cmd/mp-cachectl`: control CLI for local socket requests.
- `internal/cache`: bounded in-memory cache module.
- `internal/config`: bootstrap config defaults, parsing, export, and centralized durable constants in `constants.h`.
- `internal/httpserver`: Unix-socket HTTP listener and response helpers.
- `internal/observability`: `mp_logger` integration wrapper.
- `internal/platform`: filesystem and process helpers.
- `internal/runtime`: service composition and lifecycle.
- `internal/security`: reserved for auth, RBAC, and client token lifecycle logic.
- `internal/storage`: reserved for encrypted persistence, export/import, and journal replay.
- `native/mp_logger`: logging submodule.

## Validation

- bootstrap_state: phase-4-baseline
- validate_fast: `./scripts/check-context.sh`
- validate_full: `make test`
- validate_host_memory: `make test-valgrind`
