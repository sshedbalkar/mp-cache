#!/usr/bin/env bash
set -euo pipefail

# Stops the local repository-managed mp-cache server process.
#
# Usage examples:
#   ./scripts/stop-local-server.sh
#   MP_PID_FILE=.tmp/run/mp-cache.pid ./scripts/stop-local-server.sh

if [ "${1:-}" = "--help" ]; then
  cat <<'EOF'
Usage: ./scripts/stop-local-server.sh

Stops the local repository-managed mp-cache server process.

Environment:
  MP_PID_FILE
      Optional local pid file path override.
EOF
  exit 0
fi

cd "$(dirname "$0")/.."
. ./scripts/lib/local-env.sh

mp_stop_server
