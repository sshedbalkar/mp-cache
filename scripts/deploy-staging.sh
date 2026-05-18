#!/usr/bin/env bash
set -euo pipefail

# Deploy the artifact approved in QA to Staging.
# Keep environment differences in configs/env/staging.ini and runtime secrets:
# Usage examples:
#   ./scripts/deploy-staging.sh dist/promotions/staging/mp-cache-<version>.tar.gz deploy@staging-host

cd "$(dirname "$0")/.."
. ./scripts/lib/remote-deploy-env.sh

mp_remote_deploy_run staging "$@"
