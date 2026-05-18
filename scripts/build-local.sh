#!/usr/bin/env bash
set -euo pipefail

# Builds the default local debug server and refreshes the local artifact.
#
# Usage examples:
#   ./scripts/build-local.sh
#   ./scripts/deploy-local.sh

cd "$(dirname "$0")/.."
. ./scripts/lib/local-env.sh

mp_build_server
./scripts/create-build-artifact.sh
