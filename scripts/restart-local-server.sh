#!/usr/bin/env bash
set -euo pipefail

# Restarts the local repository-managed mp-cache server process.
#
# Usage examples:
#   ./scripts/restart-local-server.sh
#   MP_SOCKET_PATH=.tmp/run/mp-cache.sock ./scripts/restart-local-server.sh

cd "$(dirname "$0")/.."
. ./scripts/lib/local-env.sh

mp_stop_server || true
mp_start_server
