# Spec: mp-cache HTTP API v1

## Purpose

Define the Unix-socket HTTP API for `mp-cache`.

## Version

`v1`

## Authentication

Protected endpoints require:

- `Authorization: Bearer <token>`

Roles:

- `client`: cache read/write/delete
- `operator`: all client APIs plus stats, memory, uptime, logs
- `admin`: all operator APIs plus client registration, token rotation, token invalidation, export/import, selective purge, purge-all

Bootstrap admin access is sourced from `bootstrap_admin_token_secret_ref`.

## Implemented Endpoints

| Method | Path | Auth | Purpose |
|:-------|:-----|:-----|:--------|
| `GET` | `/health` | none | Liveness payload. |
| `GET` | `/v1/health` | none | Same as `/health`. |
| `GET` | `/v1/cache/{key}` | `client` | Fetch a cached value as Base64. |
| `PUT` | `/v1/cache/{key}` | `client` | Store a Base64 value with optional `ttl_seconds`. |
| `DELETE` | `/v1/cache/{key}` | `client` | Remove a cached value. |
| `GET` | `/v1/stats` | `operator` | Request, cache, and memory stats. |
| `GET` | `/v1/metrics/memory` | `operator` | Memory-only summary. |
| `GET` | `/v1/uptime` | `operator` | Uptime and start time. |
| `GET` | `/v1/logs?tail=<n>` | `operator` | Tail the latest log file up to configured bounds. |
| `POST` | `/v1/clients` | `admin` | Register a persisted client and issue a token. |
| `POST` | `/v1/clients/{id}/rotate-token` | `admin` | Rotate a persisted client token. |
| `POST` | `/v1/clients/{id}/invalidate-token` | `admin` | Invalidate a persisted client token without deleting the client principal. |
| `POST` | `/v1/export` | `admin` | Write an integrity-checked encrypted export file. |
| `POST` | `/v1/import` | `admin` | Import one export file after integrity validation. |
| `POST` | `/v1/purge/keys` | `admin` | Delete one selected set of cache keys and report purge counts. |
| `POST` | `/v1/purge/all` | `admin` | Clear all cached entries. |

## Request Bodies

`PUT /v1/cache/{key}`

```json
{
  "value_base64": "aGVsbG8=",
  "ttl_seconds": 60
}
```

`POST /v1/clients`

```json
{
  "client_id": "client-one",
  "role": "client"
}
```

`POST /v1/import`

```json
{
  "path": ".tmp/exports/mp-cache-export-123-456.bin"
}
```

`POST /v1/purge/keys`

```json
{
  "keys": ["alpha", "beta"]
}
```

## Representative Response Fields

Cache fetch:

| Field | Type | Description |
|:------|:-----|:------------|
| `key` | string | Requested key. |
| `value_base64` | string | Stored value payload. |
| `value_bytes` | integer | Decoded value length. |
| `expires_at_utc_seconds` | integer | Absolute UTC expiry timestamp. |

Stats:

| Field | Type | Description |
|:------|:-----|:------------|
| `uptime_seconds` | integer | Whole-process uptime. |
| `entry_count` | integer | Current in-memory entry count. |
| `bytes_used` | integer | Current cache footprint. |
| `memory_limit_bytes` | integer | Configured memory ceiling. |
| `cache_hits` | integer | Successful cache reads. |
| `cache_misses` | integer | `not_found` or expired reads. |
| `rate_limited_requests` | integer | Authenticated requests rejected by the limiter. |
| `token_invalidations` | integer | Successful admin token invalidation operations. |

## HTTP Status Codes

- `200 OK`
- `400 Bad Request`
- `401 Unauthorized`
- `403 Forbidden`
- `404 Not Found`
- `405 Method Not Allowed`
- `409 Conflict`
- `413 Payload Too Large`
- `429 Too Many Requests`
- `500 Internal Server Error`

## Error Response Body

Every application error response is JSON and includes a project-defined machine code plus a human-oriented description:

```json
{
  "error_code": "invalid_argument",
  "error_description": "value_base64 is invalid"
}
```

Clients must branch on `error_code`, not on `error_description`. Descriptions are for humans, diagnostics, and logs only and may be reworded without changing the API decision contract.

For `v1` compatibility, error responses also include legacy aliases:

- `error`: same value as `error_code`
- `message`: same value as `error_description`

## Error Codes

- `invalid_argument`
- `not_found`
- `unauthorized`
- `forbidden`
- `limit_exceeded`
- `conflict`
- `internal_error`

## Compatibility

- `v1` stays Unix-socket HTTP.
- Remote access remains external to the service process through SSH tunnel and proxy infrastructure.
