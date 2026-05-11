# Validators

## Fast Validation

- `./scripts/check-context.sh`
  - verifies required local context and durable docs exist

## Standards Validation

- `./scripts/check-standards.sh .tmp/test-reports 85`
  - scores scaffold completeness against local required artifacts

## Full Validation

- `make test`
  - config tests
  - cache tests
  - standards check
