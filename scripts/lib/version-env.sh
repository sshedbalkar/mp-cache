#!/usr/bin/env bash
set -euo pipefail

# Provides build-version config parsing, validation, and bump helpers.
#
# Usage examples:
#   . ./scripts/lib/version-env.sh
#   MP_CONFIG_PATH=configs/bootstrap.yaml . ./scripts/lib/version-env.sh

if [ "${BASH_SOURCE[0]}" = "$0" ] && [ "${1:-}" = "--help" ]; then
  cat <<'EOF'
Usage: . ./scripts/lib/version-env.sh

Provides helpers for reading and updating the configured build version.

Environment:
  MP_SCRIPT_CONFIG_FILE
      Optional script defaults file override.
  MP_CONFIG_PATH
      Optional server config path override. Default: MP_SCRIPT_DEFAULT_CONFIG_PATH from configs/scripts/defaults.env.
EOF
  exit 0
fi

. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/script-config-env.sh"

mp_version_detect_repo_root() {
  local version_script_dir
  version_script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
  cd "$version_script_dir/../.." && pwd
}

MP_REPO_ROOT="${MP_REPO_ROOT:-$(mp_version_detect_repo_root)}"
MP_CONFIG_PATH="${MP_CONFIG_PATH:-$(mp_script_repo_path "$MP_SCRIPT_DEFAULT_CONFIG_PATH")}"
MP_BUILD_VERSION_SECTION="service"
MP_BUILD_VERSION_KEY="build_version"

mp_version_exit_with_error() {
  printf 'error: %s\n' "$1" >&2
  exit 1
}

mp_validate_build_version() {
  local build_version_value="$1"

  [[ "$build_version_value" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]
}

mp_require_valid_build_version() {
  local build_version_value="$1"

  mp_validate_build_version "$build_version_value" ||
    mp_version_exit_with_error "build version must follow MAJOR.MINOR.HOTFIX: $build_version_value"
}

mp_read_build_version_from_config() {
  local version_config_file="${1:-$MP_CONFIG_PATH}"
  local build_version_value=""

  [ -f "$version_config_file" ] ||
    mp_version_exit_with_error "server config file not found: $version_config_file"

  build_version_value="$(
    awk -F: -v target_section="$MP_BUILD_VERSION_SECTION" -v target_key="$MP_BUILD_VERSION_KEY" '
      function trim(value) {
        gsub(/^[[:space:]]+/, "", value)
        gsub(/[[:space:]]+$/, "", value)
        gsub(/^"/, "", value)
        gsub(/"$/, "", value)
        return value
      }

      /^[[:space:]]*[#]/ || /^[[:space:]]*$/ {
        next
      }

      /^[^[:space:]][^:]*:[[:space:]]*$/ {
        section = $0
        gsub(/:[[:space:]]*$/, "", section)
        in_section = (section == target_section)
        next
      }

      in_section == 1 {
        key = trim($1)
        if (key == target_key) {
          print trim(substr($0, index($0, ":") + 1))
          found = 1
          exit
        }
      }

      END {
        if (found != 1) {
          exit 1
        }
      }' "$version_config_file"
  )" || mp_version_exit_with_error "missing $MP_BUILD_VERSION_SECTION.$MP_BUILD_VERSION_KEY in $version_config_file"

  mp_require_valid_build_version "$build_version_value"
  printf '%s\n' "$build_version_value"
}

mp_increment_minor_build_version() {
  local build_version_value="$1"
  local major_version=""
  local minor_version=""
  local ignored_hotfix_version=""

  mp_require_valid_build_version "$build_version_value"
  IFS=. read -r major_version minor_version ignored_hotfix_version <<EOF
$build_version_value
EOF
  printf '%s.%s.0\n' "$major_version" "$((minor_version + 1))"
}

mp_write_build_version_to_config() {
  local next_build_version="$1"
  local version_config_file="${2:-$MP_CONFIG_PATH}"
  local version_config_directory=""
  local version_config_temp_file=""

  mp_require_valid_build_version "$next_build_version"
  [ -f "$version_config_file" ] ||
    mp_version_exit_with_error "server config file not found: $version_config_file"

  version_config_directory="$(dirname "$version_config_file")"
  version_config_temp_file="$(mktemp "$version_config_directory/.build-version.XXXXXX")"

  awk -v target_section="$MP_BUILD_VERSION_SECTION" \
      -v target_key="$MP_BUILD_VERSION_KEY" \
      -v next_value="$next_build_version" '
    function emit_value() {
      print "  " target_key ": \"" next_value "\""
      wrote = 1
    }

    /^[^[:space:]][^:]*:[[:space:]]*$/ {
      section = $0
      gsub(/:[[:space:]]*$/, "", section)
      if (in_section == 1 && wrote != 1) {
        emit_value()
      }
      in_section = (section == target_section)
      saw_section = saw_section || in_section
      print
      next
    }

    in_section == 1 {
      line = $0
      split(line, parts, ":")
      key = parts[1]
      gsub(/^[[:space:]]+/, "", key)
      gsub(/[[:space:]]+$/, "", key)
      if (key == target_key) {
        emit_value()
        next
      }
    }

    {
      print
    }

    END {
      if (in_section == 1 && wrote != 1) {
        emit_value()
      } else if (saw_section != 1) {
        print ""
        print target_section ":"
        emit_value()
      }
    }' "$version_config_file" >"$version_config_temp_file"

  mv "$version_config_temp_file" "$version_config_file"
}

mp_increment_config_build_version() {
  local version_config_file="${1:-$MP_CONFIG_PATH}"
  local current_build_version=""
  local next_build_version=""

  current_build_version="$(mp_read_build_version_from_config "$version_config_file")"
  next_build_version="$(mp_increment_minor_build_version "$current_build_version")"
  mp_write_build_version_to_config "$next_build_version" "$version_config_file"
  printf '%s\n' "$next_build_version"
}
