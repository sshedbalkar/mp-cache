#!/usr/bin/env bash
set -euo pipefail

# Stops the mp-cache systemd service on the Staging host.
#
# Usage examples:
#   ./scripts/stop-staging.sh deploy@staging-host
#   MP_REMOTE_DEPLOY_SERVICE_NAME=mp-cache ./scripts/stop-staging.sh deploy@staging-host

cd "$(dirname "$0")/.."
. ./scripts/lib/remote-deploy-env.sh

mp_remote_service_run staging stop "$@"
