# Development Deployment

Development receives the first packaged artifact after local validation.

Requirements:

1. Deploy with `./scripts/deploy-development.sh <artifact.tar.gz> <ssh-target>`.
2. Let the deploy script create the `mp-cache` system user and group if they do not already exist.
3. Keep runtime secrets outside the artifact under `/run/secrets/mp-cache/`.
4. Expose HTTP through Nginx or a controlled external proxy only.
5. Keep the service Unix socket local to the host at `/run/mp-cache/mp-cache.sock`.
6. Stop or restart with `make stop-development SSH_TARGET=<ssh-target>` and `make restart-development SSH_TARGET=<ssh-target>`.
