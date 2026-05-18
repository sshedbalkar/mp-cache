#!/usr/bin/env bash
set -euo pipefail

# Starts the local repository-managed mp-cache server process.
#
# Usage examples:
#   ./scripts/run-local-server.sh
#   MP_CONSOLE_LOG=.tmp/logs/console.log ./scripts/run-local-server.sh

if [ "${1:-}" = "--help" ]; then
  cat <<'EOF'
Usage: ./scripts/run-local-server.sh

Starts the local repository-managed mp-cache server process.

Environment:
  MP_CONSOLE_LOG
      Optional server console log path override.
EOF
  exit 0
fi

cd "$(dirname "$0")/.."
. ./scripts/lib/local-env.sh

mp_start_server
