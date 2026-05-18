# systemd Deployment

Use these assets when packaging `mp-cache` under a long-running Linux service manager. For local auto-start deployment, prefer `./scripts/deploy-local.sh` because it generates a host-specific unit that runs as the current local user and points at the repository `build/` directory.

Included files:

1. `mp-cache.service`

Recommended deployment flow:

1. Install the packaged artifact under `/opt/mp-cache/releases/<release-id>`.
2. Update `/opt/mp-cache/current` to point at the selected release.
3. Mount the secret files referenced by `configs/env/<environment>.ini`.
4. Adjust `ExecStart` and `WorkingDirectory` if the artifact root differs from `/opt/mp-cache/current`.
5. Run `systemctl daemon-reload && systemctl enable --now mp-cache.service`.

The unit uses `RequiresMountsFor=` on the install, state, log, and secret paths so systemd orders service startup after the backing filesystem mounts, including SSD-backed volumes.

Installing the system unit may require host administrator privileges even though the service itself is configured to run as the non-root `mp-cache` identity.
