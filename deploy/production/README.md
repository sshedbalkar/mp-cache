# Production Deployment

Production promotes the exact artifact approved in staging.

Requirements:

1. Non-root process identity.
2. Least-privilege filesystem and network access.
3. Runtime limits for memory, file descriptors, and writable paths.
4. SSH tunnel and proxy controls audited separately from the service process.
5. `deploy/systemd/` assets reviewed and adjusted for the target host paths and secret mounts.
6. Deploy with `./scripts/deploy-production.sh <artifact.tar.gz> <ssh-target>`.
7. The default runtime identity is the dedicated `mp-cache` system user and group.
8. The deploy script creates the `mp-cache` system user and group when they are missing.
9. Stop or restart with `make stop-production SSH_TARGET=<ssh-target>` and `make restart-production SSH_TARGET=<ssh-target>`.
