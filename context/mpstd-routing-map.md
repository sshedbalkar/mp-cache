# MPSTD Routing Map

Use inherited standards from `../multi-player-app-ai/docs/`.

## Route Selectors

- `route.build-release`
  - `MPSTD11`
  - local files: `Makefile`, `configs/build/CMakePresets.json`, `scripts/package.sh`, `deploy/`
- `route.c-service`
  - `MPSTD1`, `MPSTD7`, `MPSTD17`
  - local files: `cmd/`, `internal/`, `native/mp_logger`
- `route.server-api`
  - `MPSTD3`, `MPSTD9`, `MPSTD10`
  - local files: `api/http/v1/cache-service.md`, `internal/httpserver/`
- `route.technical-docs`
  - `MPSTD13`, `MPSTD16`
  - local files: `docs/`, `context/`
- `route.testing`
  - `MPSTD15`
  - local files: `tests/`, `scripts/test-unit.sh`, `scripts/check-standards.sh`
