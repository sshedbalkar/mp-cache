#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

report_dir="${1:-.tmp/test-reports}"
valgrind_core_dir=".tmp/valgrind/cores"
mkdir -p "$report_dir"
mkdir -p "$valgrind_core_dir"

summary_report="$report_dir/valgrind-report.md"
detail_report="$report_dir/valgrind.txt"
valgrind_loader_binary="/lib64/ld-linux-x86-64.so.2"
glibc_package_repository=""
glibc_package_architecture=""

relocate_valgrind_core_dumps() {
  local core_dump_path=""

  shopt -s nullglob
  for core_dump_path in vgcore.*; do
    if [ -f "$core_dump_path" ]; then
      mv -f "$core_dump_path" "$valgrind_core_dir/"
    fi
  done
  shopt -u nullglob
}

trap relocate_valgrind_core_dumps EXIT

if [ ! -e "$valgrind_loader_binary" ]; then
  valgrind_loader_binary="$(realpath /usr/lib/ld-linux-x86-64.so.2 2>/dev/null || printf '/usr/lib/ld-linux-x86-64.so.2')"
fi

if command -v pacman >/dev/null 2>&1; then
  glibc_package_repository="$(
    pacman -Qi glibc 2>/dev/null | awk -F': *' '/^Installed From/ { print $2; exit }'
  )"
  glibc_package_architecture="$(
    pacman -Qi glibc 2>/dev/null | awk -F': *' '/^Architecture/ { print $2; exit }'
  )"
fi

rm -f "$summary_report" "$detail_report"

if ! command -v valgrind >/dev/null 2>&1; then
  {
    printf '# Valgrind Report\n\n'
    printf '| Field | Value |\n'
    printf '|:------|:------|\n'
    printf '| Status | SKIPPED |\n'
    printf '| Reason | `valgrind` not installed |\n'
  } > "$summary_report"
  exit 0
fi

cmake --fresh --preset local-debug
cmake --build --preset local-debug

prefetch_valgrind_loader_debuginfo() {
  if ! command -v debuginfod-find >/dev/null 2>&1; then
    return 0
  fi

  if [ ! -e "$valgrind_loader_binary" ]; then
    return 0
  fi

  debuginfod-find debuginfo "$valgrind_loader_binary" >/dev/null 2>&1 || true
}

run_valgrind_for_test_binary() {
  local test_binary="$1"

  valgrind --error-exitcode=1 --leak-check=full "$test_binary" >>"$detail_report" 2>&1
}

write_valgrind_host_blocker_report() {
  {
    printf '# Valgrind Report\n\n'
    printf '| Field | Value |\n'
    printf '|:------|:------|\n'
    printf '| Status | SKIPPED |\n'
    printf '| Reason | Valgrind is installed but unusable on this host; glibc debuginfo or an unstripped dynamic loader is required |\n'
    printf '| Dynamic Loader | `%s` |\n\n' "$valgrind_loader_binary"
    printf 'Detailed output: `%s`\n\n' "$detail_report"

    if command -v pacman >/dev/null 2>&1; then
      printf 'Recommended next steps on Arch or CachyOS:\n\n'
      printf '```sh\n'
      printf 'DEBUGINFOD_URLS="${DEBUGINFOD_URLS:-https://debuginfod.archlinux.org https://debuginfod.cachyos.org}" debuginfod-find debuginfo %s\n' "$valgrind_loader_binary"
      printf 'make test-valgrind\n'
      printf '```\n\n'
      printf 'If `debuginfod-find` still cannot fetch the loader symbols, install the matching `glibc-debug` package from your enabled debug repository and rerun `make test-valgrind`.\n'
      return
    fi

    printf 'Install the glibc debuginfo package that matches `%s`, then rerun `make test-valgrind`.\n' "$valgrind_loader_binary"
  } > "$summary_report"
}

write_valgrind_loader_instruction_report() {
  {
    printf '# Valgrind Report\n\n'
    printf '| Field | Value |\n'
    printf '|:------|:------|\n'
    printf '| Status | SKIPPED |\n'
    printf '| Reason | Valgrind cannot emulate an instruction executed by the host dynamic loader before any project code runs |\n'
    printf '| Dynamic Loader | `%s` |\n' "$valgrind_loader_binary"

    if [ -n "$glibc_package_repository" ]; then
      printf '| glibc Repository | `%s` |\n' "$glibc_package_repository"
    fi

    if [ -n "$glibc_package_architecture" ]; then
      printf '| glibc Architecture | `%s` |\n' "$glibc_package_architecture"
    fi

    printf '\nDetailed output: `%s`\n\n' "$detail_report"

    if [ "$glibc_package_repository" = "cachyos-znver4" ] || [ "$glibc_package_architecture" = "x86_64_v4" ]; then
      printf 'Recommended next steps on CachyOS znver4:\n\n'
      printf '```sh\n'
      printf 'sudo pacman -S core/glibc core/lib32-glibc\n'
      printf 'DEBUGINFOD_URLS="${DEBUGINFOD_URLS:-https://debuginfod.archlinux.org}" make test-valgrind\n'
      printf '```\n\n'
      printf 'That replaces the optimized `x86_64_v4` loader with the generic Arch loader that Valgrind can decode. If you need to keep the optimized host packages, run Valgrind inside a generic Arch container or chroot instead.\n'
      return
    fi

    printf 'Use a glibc build whose dynamic loader avoids instructions unsupported by your installed Valgrind, or run Valgrind inside a container or chroot with a more conservative glibc build.\n'
  } > "$summary_report"
}

prefetch_valgrind_loader_debuginfo

if ! run_valgrind_for_test_binary ./build/local-debug/mp_cache_store_tests ||
   ! run_valgrind_for_test_binary ./build/local-debug/mp_cache_security_tests ||
   ! run_valgrind_for_test_binary ./build/local-debug/mp_cache_storage_tests ||
   ! run_valgrind_for_test_binary ./build/local-debug/mp_cache_http_tests; then
  if rg -q "Fatal error at startup|install glibc's debuginfo|Cannot continue -- exiting now" "$detail_report"; then
    write_valgrind_host_blocker_report
    exit 0
  fi

  if rg -q "Unrecognised instruction|Illegal opcode|_dl_start|ld-linux-x86-64.so.2" "$detail_report"; then
    write_valgrind_loader_instruction_report
    exit 0
  fi

  {
    printf '# Valgrind Report\n\n'
    printf '| Field | Value |\n'
    printf '|:------|:------|\n'
    printf '| Status | FAIL |\n\n'
    printf 'Detailed output: `%s`\n' "$detail_report"
  } > "$summary_report"
  exit 1
fi

{
  printf '# Valgrind Report\n\n'
  printf '| Field | Value |\n'
  printf '|:------|:------|\n'
  printf '| Status | PASS |\n\n'
  printf 'Detailed output: `%s`\n' "$detail_report"
} > "$summary_report"
