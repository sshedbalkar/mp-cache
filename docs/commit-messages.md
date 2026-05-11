# Commit Message Standard

This document is the local commit message standard for `mp-cache`. It is authoritative for this repository.

Every commit must use both a subject and a body. One-line commits are not allowed.

## Required Format

```text
<type>(<scope>): <short summary>                          ← required
<empty line>
<body — explains the WHY, not the WHAT, under 40 words>   ← required
<empty line>
<footer>                                                  ← situational
```

## Allowed Types

- `feat`
- `fix`
- `refactor`
- `test`
- `docs`
- `build`
- `perf`
- `security`
- `db`
- `ops`
- `style`
- `ci`
- `chore`
- `revert`
- `hotfix`
- `infra`

## Allowed Footers

- `Fixes <ID>`
- `Closes <ID>`
- `Refs <ID>`
- `BREAKING CHANGE:`
- `Co-authored-by`
- `Reviewed-by`

## Examples

```text
feat(cache): add bounded TTL-aware key storage

Establish the in-memory cache core needed for health, storage,
and later authenticated data APIs.
```

```text
docs(runbook): document local Unix socket lifecycle

Make startup, shutdown, and health verification predictable for
operators and future agents.
```

## Discoverability

When work touches Git workflow, commits, PR preparation, or release history:

1. Read this file first.
2. Use the required format, allowed types, and allowed footers above.
3. Keep the body focused on why the change exists.
