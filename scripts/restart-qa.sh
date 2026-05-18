#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
. ./scripts/lib/remote-deploy-env.sh

mp_remote_service_run qa restart "$@"
