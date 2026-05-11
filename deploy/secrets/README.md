# Secrets Deployment

Secrets are never committed.

Approved secret source contracts:

- `env:<name>` for local development only
- `file:<absolute_path>` for QA, staging, and production

Planned sensitive values:

- bootstrap admin token
- storage encryption key
- future operator credentials if needed
