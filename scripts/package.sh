#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

package_dir="${1:-dist}"
build_dir="${2:-build/local-debug}"
version="${3:-dev}"
commit="${4:-local}"
build_time="${5:-1970-01-01T00:00:00Z}"
artifact_root="$package_dir/mp-cache-$version"
archive_path="$package_dir/mp-cache-$version.tar.gz"

mkdir -p "$artifact_root/bin" "$artifact_root/configs" "$artifact_root/deploy" "$artifact_root/docs"
cp "$build_dir/mp-cache-server" "$artifact_root/bin/"
cp "$build_dir/mp-cachectl" "$artifact_root/bin/"
cp -r configs "$artifact_root/"
cp -r deploy "$artifact_root/"
cp -r docs "$artifact_root/"
cp README.md "$artifact_root/"

cat >"$artifact_root/build-info.txt" <<EOF
version=$version
commit=$commit
build_time=$build_time
EOF

tar -czf "$archive_path" -C "$package_dir" "mp-cache-$version"

if command -v sha256sum >/dev/null 2>&1; then
  sha256sum "$archive_path" >"$archive_path.sha256"
elif command -v shasum >/dev/null 2>&1; then
  shasum -a 256 "$archive_path" >"$archive_path.sha256"
fi

printf 'packaged artifact: %s\n' "$archive_path"
