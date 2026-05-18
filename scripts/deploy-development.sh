#!/usr/bin/env bash
set -euo pipefail

# Deploy the already-packaged mp-cache artifact to Development.
# Build and package locally first, then pass the tarball and SSH target:
# Usage examples:
#   make package VERSION=<version> COMMIT=<sha> BUILD_TIME=<utc>
#   ./scripts/deploy-development.sh dist/mp-cache-<version>.tar.gz deploy@dev-host

cd "$(dirname "$0")/.."
. ./scripts/lib/remote-deploy-env.sh

mp_remote_deploy_run development "$@"
