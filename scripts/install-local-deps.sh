#!/usr/bin/env bash
set -euo pipefail

# Installs local dependencies through the supported host-specific installer.
#
# Usage examples:
#   ./scripts/install-local-deps.sh --dry-run
#   ./scripts/install-local-deps.sh --with-valgrind

cd "$(dirname "$0")/.."
. ./scripts/lib/local-env.sh

case "$(mp_detect_env_id)" in
  arch-cachyos)
    ./scripts/install-arch-cachyos.sh "$@"
    ;;
  *)
    mp_exit_with_error "unsupported local environment for package installation: $(mp_detect_env_id); install the required tools manually, then use the local build, deploy, and test scripts from a normal non-root shell"
    ;;
esac
