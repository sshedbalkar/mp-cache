#!/usr/bin/env bash
set -euo pipefail

# Starts the local repository-managed mp-cache server process.
#
# Usage examples:
#   ./scripts/run-local-server.sh
#   MP_CONSOLE_LOG=.tmp/logs/console.log ./scripts/run-local-server.sh

cd "$(dirname "$0")/.."
. ./scripts/lib/local-env.sh

mp_start_server
