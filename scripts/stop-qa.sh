#!/usr/bin/env bash
set -euo pipefail

# Stops the mp-cache systemd service on the QA host.
#
# Usage examples:
#   ./scripts/stop-qa.sh deploy@qa-host
#   MP_REMOTE_DEPLOY_SERVICE_NAME=mp-cache ./scripts/stop-qa.sh deploy@qa-host

cd "$(dirname "$0")/.."
. ./scripts/lib/remote-deploy-env.sh

mp_remote_service_run qa stop "$@"
