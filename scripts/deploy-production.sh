#!/usr/bin/env bash
set -euo pipefail

# Deploy the exact artifact approved in Staging to Production.
# Review the configured host, proxy binding, and mounted secret files first:
#   ./scripts/deploy-production.sh dist/promotions/production/mp-cache-<version>.tar.gz deploy@prod-host

cd "$(dirname "$0")/.."
. ./scripts/lib/remote-deploy-env.sh

mp_remote_deploy_run production "$@"
