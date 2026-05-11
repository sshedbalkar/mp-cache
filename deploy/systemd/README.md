# systemd Deployment

Use these assets when packaging `mp-cache` under a long-running Linux service manager.

Included files:

1. `mp-cache.service`

Recommended deployment flow:

1. Install the packaged artifact under `/opt/mp-cache` or an equivalent immutable path.
2. Mount the secret files referenced by `configs/env/production.ini`.
3. Adjust `ExecStart` and `WorkingDirectory` if the artifact root differs from `/opt/mp-cache`.
4. Run `systemctl daemon-reload && systemctl enable --now mp-cache.service`.
