# Validators

## Fast Validation

- `./scripts/check-context.sh`
  - verifies required local context and durable docs exist

## Standards Validation

- `./scripts/check-standards.sh .tmp/test-reports 85`
  - scores scaffold completeness against local required artifacts and rejects hardcoded script fallback defaults that belong in `configs/scripts/defaults.env`
- `./scripts/test-naming-strategy.sh .tmp/test-reports`
  - verifies main entrypoints, scripts, and internal interfaces avoid low-signal standalone names

## Full Validation

- `make test`
  - config tests
  - cache tests
  - naming strategy check
  - standards check
  - sanitizer hardening checks
- `make test-valgrind`
  - host-prepared Valgrind validation for the native test binaries
- `make test-local-deployment`
  - post-deployment smoke validation for the installed local systemd service through Nginx
