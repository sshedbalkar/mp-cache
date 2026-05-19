# Doc Cards

## Architecture

- `architecture.mp-cache`
  - path: `docs/architecture/mp-cache-architecture.md`
  - status: accepted
  - purpose: service architecture, deployment posture, phased plan

## Standards

- `standards.engineering`
  - path: `docs/engineering-standards.md`
  - status: active
  - purpose: authoritative local engineering, API, security, build, test, and layout rules
- `standards.naming`
  - path: `docs/naming-strategy.md`
  - status: active
  - purpose: authoritative local naming strategy for entrypoints, scripts, interfaces, and exported names

## Workflow

- `workflow.commit-messages`
  - path: `docs/commit-messages.md`
  - status: active
  - purpose: authoritative local Git commit-message standard

## API

- `api.http.v1.cache-service`
  - path: `api/http/v1/cache-service.md`
  - status: active
  - purpose: implemented HTTP contract, coded error responses, role permissions, and request body shapes

## Runbooks

- `runbook.local-service-lifecycle`
  - path: `docs/runbooks/local-service-lifecycle.md`
  - status: active
  - purpose: start, stop, restart, and validation steps
- `runbook.deployment`
  - path: `docs/runbooks/deployment.md`
  - status: active
  - purpose: centralized script defaults, local auto-start service deployment, deployment reports, post-deployment smoke tests, and remote environment deployment
- `runbook.manual-end-to-end-testing`
  - path: `docs/runbooks/manual-end-to-end-testing.md`
  - status: active
  - purpose: full CachyOS host-side build, secret setup, API verification, and persistence validation commands
- `runbook.manual-api-testing-with-scripted-local-deploy`
  - path: `docs/runbooks/manual-api-testing-with-scripted-local-deploy.md`
  - status: active
  - purpose: faster local script-driven setup and deploy before manual HTTP API verification
- `runbook.valgrind-host-setup`
  - path: `docs/runbooks/valgrind-host-setup.md`
  - status: active
  - purpose: diagnose and fix host-side Valgrind startup failures on Arch and CachyOS systems
