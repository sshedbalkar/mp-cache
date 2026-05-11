#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

required_files=(
  README.md
  AGENTS.md
  AI_PERSONA.md
  context/README.md
  context/repo-map.md
  context/mpstd-routing-map.md
  context/doc-cards.md
  docs/architecture/mp-cache-architecture.md
  api/http/v1/cache-service.md
  docs/runbooks/local-service-lifecycle.md
)

for path in "${required_files[@]}"; do
  [ -f "$path" ] || {
    printf 'missing required context or durable doc: %s\n' "$path" >&2
    exit 1
  }
done

printf 'context check passed\n'
