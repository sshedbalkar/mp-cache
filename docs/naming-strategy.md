# Naming Strategy

This document is the local naming standard for `mp-cache`. It is authoritative for this repository.

This rule is a compact local adaptation of `multi-player-app-ai/docs/MPSTD14_-_Hierarchical_Naming_Strategy.md`.

## Core Rule

Choose names that narrow from:

1. boundary or domain
2. concept
3. role or action

Path context counts. Do not repeat `mp-cache` everywhere when the directory or module already provides that context.

Examples:

```text
bad:  path
good: request_path

bad:  command
good: cli_command

bad:  started_here
good: server_started_by_script
```

## Stable Domain Terms

Use stable terms that already match the service:

- cache
- client
- config
- export
- health
- http
- log
- runtime
- server
- socket
- storage
- token
- ttl

Avoid switching between synonyms unless the concepts are actually different.

## Files, Scripts, And Modules

- Prefer names that make the action and target obvious: `run-local-server.sh`, `promote-artifact.sh`, `test-naming-strategy.sh`.
- Entry-point helpers should include service or CLI context when a flat name would be ambiguous: `mp_cache_server_print_usage`, `mp_cachectl_send_http_request`.
- Within a narrowly scoped directory, compact names are acceptable when nearby files and call sites already make the target obvious. Do not add redundant prefixes just to repeat folder context.

## Functions, Variables, And Collections

- Prefer business or boundary meaning over raw type.
- Boolean names should read as predicates or states: `is_ttl_provided`, `server_started_by_script`.
- Request, response, config, and filesystem values should carry their boundary context: `request_body`, `auth_token`, `config_path`.
- Collections should describe entries, not container type.

## External Names

- Metrics should use hierarchical dotted names such as `<domain>.<component>.<measurement>`.
- Stable exported names such as JSON fields, config keys, and artifact metadata should stay specific and version-safe.
- Future database object names should use `snake_case`.

## Anti-Patterns

Avoid low-signal standalone names when local context does not narrow them enough:

- `path`
- `body`
- `index`
- `command`
- `data`
- `info`
- `helper`
- `manager`
- `list`
- `map`
- `score`

## Required Validation

- Changes under `cmd/` or `scripts/` must pass `./scripts/test-naming-strategy.sh`.
- `make test` includes naming-strategy validation.
