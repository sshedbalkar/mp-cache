# Runbook: Valgrind Host Setup

## Symptoms

- `make test-valgrind` or `./scripts/test-valgrind.sh` fails before any unit test logic runs
- `.tmp/test-reports/valgrind.txt` shows `Fatal error at startup` and a missing `memcmp` redirection in `ld-linux-x86-64.so.2`
- after loader debuginfo is available, `.tmp/test-reports/valgrind.txt` may instead show `Unrecognised instruction` or `Illegal opcode` in `_dl_start`

## Impact

Native memory validation is blocked even though the project binaries build and the unit tests may otherwise pass.

## Affected Host Profiles

- Arch Linux and CachyOS hosts using `valgrind`
- especially CachyOS `znver4` systems where `glibc` is installed from `cachyos-znver4` with architecture `x86_64_v4`

## Root Cause

- Valgrind must inspect symbols from the system dynamic loader before the target process starts.
- If loader debuginfo is missing, Valgrind cannot install the required startup redirection for `memcmp` and aborts immediately.
- On CachyOS `znver4`, the optimized `x86_64_v4` `glibc` loader can still fail after debuginfo is present because it executes instructions that current Valgrind cannot emulate in `_dl_start`.
- These failures happen before `mp-cache` code runs. They are host toolchain and package compatibility issues, not service logic defects.

## Preconditions

- `valgrind`, `debuginfod-find`, `cmake`, and `make` installed
- host access to `https://debuginfod.archlinux.org` and, if applicable, `https://debuginfod.cachyos.org`
- a non-sandboxed shell when validating the host setup, because restricted runners may block debuginfod access

## Diagnosis

1. Run `make test-valgrind`.
2. Inspect `.tmp/test-reports/valgrind.txt`.
3. If the log contains `Fatal error at startup`, `memcmp`, and `ld-linux-x86-64.so.2`, the loader debuginfo is missing.
4. If the log contains `Unrecognised instruction`, `Illegal opcode`, or `_dl_start`, the host loader is too optimized for the installed Valgrind build.
5. Check the installed `glibc` source and architecture:

```sh
pacman -Qi glibc | rg 'Installed From|Architecture'
```

Problematic CachyOS output looks like:

```text
Installed From  : cachyos-znver4
Architecture    : x86_64_v4
```

## Mitigation

1. Fetch the dynamic loader debuginfo:

```sh
DEBUGINFOD_URLS="${DEBUGINFOD_URLS:-https://debuginfod.archlinux.org https://debuginfod.cachyos.org}" debuginfod-find debuginfo /lib64/ld-linux-x86-64.so.2
```

2. Rerun `make test-valgrind`.
3. If Valgrind still fails with `Unrecognised instruction` and `glibc` is installed from `cachyos-znver4`, switch the loader packages to the generic Arch `core` packages:

```sh
sudo pacman -S core/glibc core/lib32-glibc
DEBUGINFOD_URLS="${DEBUGINFOD_URLS:-https://debuginfod.archlinux.org}" make test-valgrind
```

4. If the host must keep the optimized CachyOS loader, run the Valgrind step inside a generic Arch container or chroot instead of on the main host.

## Verification

- `make test-valgrind` completes successfully
- `.tmp/test-reports/valgrind-report.md` shows `Status | PASS`
- `.tmp/test-reports/valgrind.txt` contains four `ERROR SUMMARY: 0 errors from 0 contexts` lines for `mp_cache_store_tests`, `mp_cache_security_tests`, `mp_cache_storage_tests`, and `mp_cache_http_tests`

## Rollback

- If the optimized CachyOS loader is required after validation, reinstall `glibc` and `lib32-glibc` from the `cachyos-znver4` repository.
- If frequent Valgrind runs are expected, prefer a dedicated generic Arch container or chroot so the main host does not need package swaps.

## Notes

- `scripts/test-valgrind.sh` is expected to report both host-level blocker variants directly.
- `scripts/test-valgrind.sh` relocates any Valgrind-generated `vgcore.*` crash dumps into `.tmp/valgrind/cores/` so repository-root temp files do not accumulate.
- A sandboxed or network-restricted environment may still report the loader-symbol failure even after the host itself is fixed.
- If that happens, rerun `make test-valgrind` in a less-restricted host shell and treat the direct host rerun as the source of truth for closeout and report artifacts.
