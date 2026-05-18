#!/usr/bin/env bash
set -euo pipefail

# Restarts the mp-cache systemd service on the Production host.
#
# Usage examples:
#   ./scripts/restart-production.sh deploy@prod-host
#   MP_REMOTE_DEPLOY_SERVICE_NAME=mp-cache ./scripts/restart-production.sh deploy@prod-host

cd "$(dirname "$0")/.."
. ./scripts/lib/remote-deploy-env.sh

mp_remote_service_run production restart "$@"
