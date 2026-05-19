#!/usr/bin/env bash
set -euo pipefail

# Creates a versioned distributable archive from an existing build directory.
#
# Usage examples:
#   ./scripts/package.sh
#   ./scripts/package.sh "$MP_PACKAGE_DIR" "$MP_BUILD_DIR" 1.2.3 abc123 2026-05-18T12:00:00Z

if [ "${1:-}" = "--help" ]; then
  cat <<'EOF'
Usage: ./scripts/package.sh [package-dir] [build-dir] [version] [commit] [build-time]

Creates a versioned distributable archive from an existing build directory.

Arguments:
  package-dir
      Output package directory. Default: MP_SCRIPT_DEFAULT_PACKAGE_DIR from configs/scripts/defaults.env.
  build-dir
      Build directory containing binaries. Default: MP_SCRIPT_DEFAULT_LOCAL_BUILD_DIR from configs/scripts/defaults.env.
  version
      Artifact version string. Default: service.build_version from server config.
  commit
      Artifact commit string. Default: MP_SCRIPT_DEFAULT_ARTIFACT_COMMIT from configs/scripts/defaults.env.
  build-time
      UTC build time. Default: MP_SCRIPT_DEFAULT_ARTIFACT_BUILD_TIME from configs/scripts/defaults.env.

Environment:
  MP_CONFIG_PATH
      Optional server config path whose service.build_version is used when version is omitted.
EOF
  exit 0
fi

cd "$(dirname "$0")/.."
. ./scripts/lib/version-env.sh

package_dir="${1:-$(mp_script_repo_path "$MP_SCRIPT_DEFAULT_PACKAGE_DIR")}"
build_dir="${2:-$(mp_script_repo_path "$MP_SCRIPT_DEFAULT_LOCAL_BUILD_DIR")}"
artifact_version="${3:-}"
artifact_commit="${4:-$MP_SCRIPT_DEFAULT_ARTIFACT_COMMIT}"
artifact_build_time="${5:-$MP_SCRIPT_DEFAULT_ARTIFACT_BUILD_TIME}"
if [ -z "$artifact_version" ]; then
  artifact_version="$(mp_read_build_version_from_config "$MP_CONFIG_PATH")"
else
  mp_require_valid_build_version "$artifact_version"
fi
artifact_root="$package_dir/mp-cache-$artifact_version"
archive_path="$package_dir/mp-cache-$artifact_version.tar.gz"

rm -rf "$artifact_root" "$archive_path" "$archive_path.sha256"
mkdir -p "$artifact_root/bin" "$artifact_root/configs" "$artifact_root/deploy" "$artifact_root/docs"
cp "$build_dir/mp-cache-server" "$artifact_root/bin/"
cp "$build_dir/mp-cachectl" "$artifact_root/bin/"
cp -r configs "$artifact_root/"
cp -r deploy "$artifact_root/"
cp -r docs "$artifact_root/"
cp README.md "$artifact_root/"

cat >"$artifact_root/build-info.txt" <<EOF
version=$artifact_version
commit=$artifact_commit
build_time=$artifact_build_time
EOF

tar -czf "$archive_path" -C "$package_dir" "mp-cache-$artifact_version"

if command -v sha256sum >/dev/null 2>&1; then
  sha256sum "$archive_path" >"$archive_path.sha256"
elif command -v shasum >/dev/null 2>&1; then
  shasum -a 256 "$archive_path" >"$archive_path.sha256"
fi

printf 'packaged artifact: %s\n' "$archive_path"
