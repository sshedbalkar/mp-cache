# systemd Deployment

Use these assets when packaging `mp-cache` under a long-running Linux service manager. For rootless local deployment, use `deploy/local/README.md` instead.

Included files:

1. `mp-cache.service`

Recommended deployment flow:

1. Install the packaged artifact under `/opt/mp-cache` or an equivalent immutable path.
2. Mount the secret files referenced by `configs/env/production.ini`.
3. Adjust `ExecStart` and `WorkingDirectory` if the artifact root differs from `/opt/mp-cache`.
4. Run `systemctl daemon-reload && systemctl enable --now mp-cache.service`.

Installing the system unit may require host administrator privileges even though the service itself is configured to run as a non-root identity.
