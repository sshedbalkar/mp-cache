# QA Deployment

QA promotes the same artifact that passed local validation.

Requirements:

1. Deploy with `./scripts/deploy-qa.sh <artifact.tar.gz> <ssh-target>`.
2. Unix socket remains local to the QA host.
3. Remote access uses SSH tunnel plus controlled proxying only.
4. QA secrets are materialized outside the application process.
5. The service runs as the dedicated non-root `mp-cache` identity by default.
6. The deploy script creates the `mp-cache` system user and group when they are missing.
7. Stop or restart with `make stop-qa SSH_TARGET=<ssh-target>` and `make restart-qa SSH_TARGET=<ssh-target>`.
