# Staging Deployment

Staging promotes the exact artifact approved in QA.

Requirements:

1. Same packaged artifact as QA.
2. Read-only mounted secret files preferred.
3. Readiness verification required before traffic exposure.
