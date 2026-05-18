#!/usr/bin/env bash
set -euo pipefail

# Restarts the mp-cache systemd service on the Staging host.
#
# Usage examples:
#   ./scripts/restart-staging.sh deploy@staging-host
#   MP_REMOTE_DEPLOY_SERVICE_NAME=mp-cache ./scripts/restart-staging.sh deploy@staging-host

cd "$(dirname "$0")/.."
. ./scripts/lib/remote-deploy-env.sh

mp_remote_service_run staging restart "$@"
