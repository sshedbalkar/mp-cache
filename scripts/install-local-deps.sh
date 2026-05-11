#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
. ./scripts/lib/local-env.sh

case "$(mp_detect_env_id)" in
  arch-cachyos)
    ./scripts/install-arch-cachyos.sh "$@"
    ;;
  *)
    mp_die "unsupported local environment: $(mp_detect_env_id)"
    ;;
esac
