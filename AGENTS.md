# AGENTS.md

project_state: bootstrap
project_foundation_file: README.md

## Bootstrap

- Read order: `AGENTS.md` -> `README.md` -> `AI_PERSONA.md` -> `context/repo-map.md` -> `context/mpstd-routing-map.md` -> `context/doc-cards.md` -> relevant validators when validation involved -> target docs.
- Source-truth engineering standards are inherited from `../multi-player-app-ai/docs/MPSTD*.md`.
- Resolve local durable docs through `context/doc-cards.md`.
- Validator index: `context/validators/README.md`.

## Hard Rules

- Use `README.md`, `AI_PERSONA.md`, local durable docs, and inherited `../multi-player-app-ai/docs/MPSTD*.md` as source truth.
- Keep `context/` thin. Do not duplicate rule bodies from inherited MPSTD documents.
- Prefer the sibling project folder conventions unless `mp-cache` has a documented reason to diverge.
- Configuration must stay explicit, reviewable, and non-secret.
- Secrets must be runtime-provided only.
- Closeout requires validation evidence or an explicit blocker.
