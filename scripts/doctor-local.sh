#!/usr/bin/env bash
set -euo pipefail

# Prints the detected local environment, paths, and required tool status.
#
# Usage examples:
#   ./scripts/doctor-local.sh
#   MP_CONFIG_PATH=configs/bootstrap.ini ./scripts/doctor-local.sh

if [ "${1:-}" = "--help" ]; then
  cat <<'EOF'
Usage: ./scripts/doctor-local.sh

Prints the detected local environment, configured paths, and required tool status.

Environment:
  MP_CONFIG_PATH
      Optional config path override.
EOF
  exit 0
fi

cd "$(dirname "$0")/.."
. ./scripts/lib/local-env.sh

mp_report_local_environment
