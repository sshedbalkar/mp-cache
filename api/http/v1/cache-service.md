# Spec: mp-cache HTTP API v1

## Purpose

Define the Unix-socket HTTP API for `mp-cache`.

## Version

`v1`

## Authentication and Authorization

Planned role model:

- `client`: data APIs and health
- `operator`: health, logs, memory, uptime, client management
- `admin`: all APIs

Bootstrap scaffold implementation status:

- implemented now:
  - `GET /health`
  - `GET /v1/health`
- planned:
  - authenticated data and management APIs

## Implemented Endpoints

| Method | Path | Auth | Status | Notes |
|:-------|:-----|:-----|:-------|:------|
| `GET` | `/health` | none in scaffold | implemented | Basic JSON health payload. |
| `GET` | `/v1/health` | none in scaffold | implemented | Same response as `/health`. |

## Planned Endpoints

| Method | Path | Role | Purpose |
|:-------|:-----|:-----|:--------|
| `GET` | `/v1/cache/{key}` | `client` | Fetch cached value. |
| `PUT` | `/v1/cache/{key}` | `client` | Store cached value with optional TTL override. |
| `DELETE` | `/v1/cache/{key}` | `client` | Remove cached value. |
| `GET` | `/v1/metrics/memory` | `operator` | Memory usage summary. |
| `GET` | `/v1/uptime` | `operator` | Process uptime. |
| `GET` | `/v1/logs` | `operator` | Read deployment log files with bounded filters. |
| `POST` | `/v1/clients` | `admin` | Register a new client and issue a token. |
| `POST` | `/v1/clients/{id}/rotate-token` | `admin` | Rotate a client token. |
| `POST` | `/v1/export` | `admin` | Export current state into one or more files. |
| `POST` | `/v1/import` | `admin` | Import one exported state file. |
| `POST` | `/v1/purge/all` | `admin` | Purge all cache data. |
| `POST` | `/v1/purge/keys` | `admin` | Purge selected keys. |

## Response Fields For Health

| Field | Type | Description |
|:------|:-----|:------------|
| `status` | string | `ok` when the process is serving requests. |
| `service` | string | Service name. |
| `environment` | string | Active environment name. |
| `socket_path` | string | Active Unix socket path. |
| `uptime_seconds` | integer | Process uptime in whole seconds. |
| `default_ttl_seconds` | integer | Effective configured default TTL. |
| `memory_limit_bytes` | integer | Effective configured memory limit. |
| `entry_count` | integer | Current in-memory entry count. |

## HTTP Status Codes

- `200 OK`
- `400 Bad Request`
- `404 Not Found`
- `405 Method Not Allowed`
- `500 Internal Server Error`

## Error Codes

Planned stable error codes:

- `invalid_argument`
- `not_found`
- `unauthorized`
- `forbidden`
- `limit_exceeded`
- `conflict`
- `internal_error`

## Idempotency and Retry Rules

- `GET` and `DELETE` are naturally idempotent.
- planned export and client-registration operations will define explicit idempotency keys before implementation.

## Compatibility

- `v1` stays Unix-socket HTTP.
- Remote access remains external to the service process through SSH tunnel and proxy infrastructure.
