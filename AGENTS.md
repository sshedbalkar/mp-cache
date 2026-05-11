# AGENTS.md

project_state: bootstrap
project_foundation_file: README.md

## Bootstrap

- Read order: `AGENTS.md` -> `README.md` -> `AI_PERSONA.md` -> `docs/engineering-standards.md` -> `docs/naming-strategy.md` -> `docs/commit-messages.md` -> `context/repo-map.md` -> `context/standards-routing-map.md` -> `context/doc-cards.md` -> relevant validators when validation involved -> target docs.
- Resolve local durable docs through `context/doc-cards.md`.
- Validator index: `context/validators/README.md`.
- For Git commit-message work, read `docs/commit-messages.md` before writing a commit message.

## Hard Rules

- Use `README.md`, `AI_PERSONA.md`, `docs/engineering-standards.md`, `docs/naming-strategy.md`, `docs/commit-messages.md`, and local durable docs as source truth.
- Use `native/mp_logger` as the only logging backend for `mp-cache`. Do not add alternate logging frameworks or ad hoc runtime log sinks; reserve direct stdio for user-facing CLI output, config/template emission, and fatal bootstrap diagnostics before `mp_logger` is available.
- Any change under `cmd/`, `scripts/`, or `internal/` must include a naming-strategy validation run through `./scripts/test-naming-strategy.sh` or `make test-naming-strategy`, with the outcome reported at closeout.
- Any change under `cmd/`, `internal/`, `tests/unit/`, or `native/` must include a Valgrind validation run through `./scripts/test-valgrind.sh` or `make test-valgrind`, with the outcome reported at closeout. If the host cannot run Valgrind, report that blocker explicitly.
- If a sandboxed or network-restricted Valgrind run reports the loader-debuginfo startup blocker, rerun `make test-valgrind` in a less-restricted host shell before reporting `SKIPPED`; for this repository, the direct host rerun is the source of truth. See `docs/runbooks/valgrind-host-setup.md`.
- Keep `context/` thin. Do not duplicate rule bodies from local durable standards docs.
- Prefer the local folder conventions unless `mp-cache` has a documented reason to diverge.
- Commit messages must follow `docs/commit-messages.md`.
- Configuration must stay explicit, reviewable, and non-secret.
- Secrets must be runtime-provided only.
- Closeout requires validation evidence or an explicit blocker.
