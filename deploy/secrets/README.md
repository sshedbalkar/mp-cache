# Secrets Deployment

Secrets are never committed.

Approved secret source contracts:

- `env:<name>` for local development only
- `file:<absolute_path>` for QA, staging, and production

Planned sensitive values:

- bootstrap admin token
- storage encryption key
- future operator credentials if needed

Local template variables:

- `MP_SECRET_LOCAL_BOOTSTRAP_ADMIN_TOKEN`
- `MP_SECRET_LOCAL_STORAGE_KEY`

Local helper:

- `./scripts/write-local-secret-env.sh` writes a usable `.tmp/secrets/local.env`
- `./scripts/write-local-secret-env.sh --force` intentionally rotates those local development values
