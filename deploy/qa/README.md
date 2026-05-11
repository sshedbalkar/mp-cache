# QA Deployment

QA promotes the same artifact that passed local validation.

Requirements:

1. Unix socket remains local to the QA host.
2. Remote access uses SSH tunnel plus controlled proxying only.
3. QA secrets are materialized outside the application process.
