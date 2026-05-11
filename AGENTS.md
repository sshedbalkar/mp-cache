# AGENTS.md

project_state: bootstrap
project_foundation_file: README.md

## Bootstrap

- Read order: `AGENTS.md` -> `README.md` -> `AI_PERSONA.md` -> `docs/engineering-standards.md` -> `docs/commit-messages.md` -> `context/repo-map.md` -> `context/standards-routing-map.md` -> `context/doc-cards.md` -> relevant validators when validation involved -> target docs.
- Resolve local durable docs through `context/doc-cards.md`.
- Validator index: `context/validators/README.md`.
- For Git commit-message work, read `docs/commit-messages.md` before writing a commit message.

## Hard Rules

- Use `README.md`, `AI_PERSONA.md`, `docs/engineering-standards.md`, `docs/commit-messages.md`, and local durable docs as source truth.
- Keep `context/` thin. Do not duplicate rule bodies from local durable standards docs.
- Prefer the local folder conventions unless `mp-cache` has a documented reason to diverge.
- Commit messages must follow `docs/commit-messages.md`.
- Configuration must stay explicit, reviewable, and non-secret.
- Secrets must be runtime-provided only.
- Closeout requires validation evidence or an explicit blocker.
