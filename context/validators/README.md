# Validators

## Fast Validation

- `./scripts/check-context.sh`
  - verifies required local context and durable docs exist

## Standards Validation

- `./scripts/check-standards.sh .tmp/test-reports 85`
  - scores scaffold completeness against local required artifacts
- `./scripts/test-naming-strategy.sh .tmp/test-reports`
  - verifies main entrypoints, scripts, and internal interfaces avoid low-signal standalone names

## Full Validation

- `make test`
  - config tests
  - cache tests
  - naming strategy check
  - standards check
