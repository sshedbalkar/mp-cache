#!/usr/bin/env bash
set -euo pipefail

# Prints the detected local environment, paths, and required tool status.
#
# Usage examples:
#   ./scripts/doctor-local.sh
#   MP_CONFIG_PATH=configs/bootstrap.ini ./scripts/doctor-local.sh

cd "$(dirname "$0")/.."
. ./scripts/lib/local-env.sh

mp_report_local_environment
