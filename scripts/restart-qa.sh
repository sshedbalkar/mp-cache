#!/usr/bin/env bash
set -euo pipefail

# Restarts the mp-cache systemd service on the QA host.
#
# Usage examples:
#   ./scripts/restart-qa.sh deploy@qa-host
#   MP_REMOTE_DEPLOY_SERVICE_NAME=mp-cache ./scripts/restart-qa.sh deploy@qa-host

cd "$(dirname "$0")/.."
. ./scripts/lib/remote-deploy-env.sh

mp_remote_service_run qa restart "$@"
