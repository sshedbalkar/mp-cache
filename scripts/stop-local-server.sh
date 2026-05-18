#!/usr/bin/env bash
set -euo pipefail

# Stops the local repository-managed mp-cache server process.
#
# Usage examples:
#   ./scripts/stop-local-server.sh
#   MP_PID_FILE=.tmp/run/mp-cache.pid ./scripts/stop-local-server.sh

cd "$(dirname "$0")/.."
. ./scripts/lib/local-env.sh

mp_stop_server
