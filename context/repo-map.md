# Repo Map

## Lookup Strategy

- primary_lookup: section + stable path
- ordering: fixed framework sections first, alphabetical within section
- jump_index: present

## Jump Index

- source-truth: `README.md`, `AI_PERSONA.md`, local durable docs, inherited `../multi-player-app-ai/docs/MPSTD*.md`
- build-deploy-roots: `Makefile`, `CMakeLists.txt`, `configs/build/CMakePresets.json`, `scripts/*.sh`, `deploy/*/README.md`
- implementation-roots: `cmd/`, `internal/`, `native/mp_logger`
- api-doc-roots: `api/http/v1/`
- validation-roots: `tests/`, `scripts/check-context.sh`, `scripts/check-standards.sh`, `scripts/test-unit.sh`

## Source Truth

- `README.md`: project foundation, scope, commands, layout.
- `AI_PERSONA.md`: decision priorities and architecture posture.
- `docs/architecture/mp-cache-architecture.md`: accepted service architecture and phased implementation plan.
- `api/http/v1/cache-service.md`: HTTP API contract and role model.
- `docs/runbooks/local-service-lifecycle.md`: local lifecycle runbook.
- `../multi-player-app-ai/docs/MPSTD*.md`: inherited engineering standards.

## Implementation Roots

- `cmd/mp-cache-server`: server entrypoint.
- `cmd/mp-cachectl`: control CLI for local socket requests.
- `internal/cache`: bounded in-memory cache module.
- `internal/config`: bootstrap config defaults, parsing, and export.
- `internal/httpserver`: Unix-socket HTTP listener and response helpers.
- `internal/observability`: `mp_logger` integration wrapper.
- `internal/platform`: filesystem and process helpers.
- `internal/runtime`: service composition and lifecycle.
- `internal/security`: reserved for auth and RBAC logic.
- `internal/storage`: reserved for encrypted persistence and export/import.
- `native/mp_logger`: logging submodule.

## Validation

- bootstrap_state: scaffold
- validate_fast: `./scripts/check-context.sh`
- validate_full: `make test`
