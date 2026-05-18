#!/usr/bin/env bash
set -euo pipefail

# Restarts the mp-cache systemd service on the Development host.
#
# Usage examples:
#   ./scripts/restart-development.sh deploy@dev-host
#   MP_REMOTE_DEPLOY_SERVICE_NAME=mp-cache ./scripts/restart-development.sh deploy@dev-host

cd "$(dirname "$0")/.."
. ./scripts/lib/remote-deploy-env.sh

mp_remote_service_run development restart "$@"
