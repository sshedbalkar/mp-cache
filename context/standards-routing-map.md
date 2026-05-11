# Standards Routing Map

Use local standards from `docs/engineering-standards.md`, `docs/naming-strategy.md`, and `docs/commit-messages.md`.

## Route Selectors

- `route.build-release`
  - section: `Build, Release, And Deployment`
  - local files: `Makefile`, `configs/build/CMakePresets.json`, `scripts/package.sh`, `deploy/`
- `route.c-service`
  - sections: `Common Engineering`, `Naming And Terminology`, `C Service And CMake`, `Security And Operations`, `Dependency Management`
  - local files: `cmd/`, `internal/`, `native/mp_logger`
- `route.naming`
  - section: `Naming And Terminology`
  - local files: `docs/naming-strategy.md`, `cmd/`, `scripts/`, `internal/`, `Makefile`
- `route.observability-logging`
  - section: `Security And Operations`
  - local files: `internal/observability/`, `internal/runtime/`, `native/mp_logger`, `configs/logger.bootstrap.ini`
- `route.server-api`
  - sections: `Server And API`, `Security And Operations`
  - local files: `api/http/v1/cache-service.md`, `internal/httpserver/`
- `route.technical-docs`
  - sections: `Documentation`, `Folder Structure`
  - local files: `docs/`, `context/`
- `route.testing`
  - section: `Testing And Validation`
  - local files: `tests/`, `scripts/test-unit.sh`, `scripts/test-naming-strategy.sh`, `scripts/test-hardening.sh`, `scripts/test-valgrind.sh`, `scripts/check-standards.sh`
