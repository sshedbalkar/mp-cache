#!/usr/bin/env bash
set -euo pipefail

# Verifies that required durable docs and context routing files exist.
#
# Usage examples:
#   ./scripts/check-context.sh
#   ./scripts/test-local.sh

cd "$(dirname "$0")/.."

required_context_paths=(
  README.md
  AGENTS.md
  AI_PERSONA.md
  docs/engineering-standards.md
  docs/naming-strategy.md
  docs/commit-messages.md
  context/README.md
  context/repo-map.md
  context/standards-routing-map.md
  context/doc-cards.md
  docs/architecture/mp-cache-architecture.md
  api/http/v1/cache-service.md
  docs/runbooks/deployment.md
  docs/runbooks/local-service-lifecycle.md
  docs/runbooks/manual-api-testing-with-scripted-local-deploy.md
)

for required_path in "${required_context_paths[@]}"; do
  [ -f "$required_path" ] || {
    printf 'missing required context or durable doc: %s\n' "$required_path" >&2
    exit 1
  }
done

printf 'context check passed\n'
