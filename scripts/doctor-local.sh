#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
. ./scripts/lib/local-env.sh

mp_report_local_environment
