# Production Deployment

Production promotes the exact artifact approved in staging.

Requirements:

1. Non-root process identity.
2. Least-privilege filesystem and network access.
3. Runtime limits for memory, file descriptors, and writable paths.
4. SSH tunnel and proxy controls audited separately from the service process.
