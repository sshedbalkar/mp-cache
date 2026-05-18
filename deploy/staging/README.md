# Staging Deployment

Staging promotes the exact artifact approved in QA.

Requirements:

1. Same packaged artifact as QA.
2. Read-only mounted secret files preferred.
3. Readiness verification required before traffic exposure.
4. Deploy with `./scripts/deploy-staging.sh <artifact.tar.gz> <ssh-target>`.
5. The service runs as the dedicated non-root `mp-cache` identity by default.
6. The deploy script creates the `mp-cache` system user and group when they are missing.
7. Stop or restart with `make stop-staging SSH_TARGET=<ssh-target>` and `make restart-staging SSH_TARGET=<ssh-target>`.
