#!/usr/bin/env bash
set -euo pipefail

# Deploy the artifact approved in Development to QA.
# This script does not rebuild; it installs the supplied immutable package:
# Usage examples:
#   ./scripts/deploy-qa.sh dist/promotions/qa/mp-cache-<version>.tar.gz deploy@qa-host

cd "$(dirname "$0")/.."
. ./scripts/lib/remote-deploy-env.sh

mp_remote_deploy_run qa "$@"
