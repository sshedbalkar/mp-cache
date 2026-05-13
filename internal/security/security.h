#ifndef MP_CACHE_INTERNAL_SECURITY_H
#define MP_CACHE_INTERNAL_SECURITY_H

#include "internal/config/config.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MP_CACHE_CLIENT_ID_CAP 64u
#define MP_CACHE_TOKEN_TEXT_CAP 65u
#define MP_CACHE_TOKEN_HASH_SIZE 32u

/* Enumerates the authorization roles recognized by the service. */
typedef enum {
    MP_CACHE_ROLE_NONE = 0,
    MP_CACHE_ROLE_CLIENT = 1,
    MP_CACHE_ROLE_OPERATOR = 2,
    MP_CACHE_ROLE_ADMIN = 3
} mp_cache_role_t;

/* Names the stable outcomes returned by security and secret-loading operations. */
typedef enum {
    MP_CACHE_SECURITY_STATUS_OK = 0,
    MP_CACHE_SECURITY_STATUS_NOT_FOUND = 1,
    MP_CACHE_SECURITY_STATUS_CONFLICT = 2,
    MP_CACHE_SECURITY_STATUS_INVALID_ARGUMENT = 3,
    MP_CACHE_SECURITY_STATUS_SECRET_ERROR = 4,
    MP_CACHE_SECURITY_STATUS_NO_MEMORY = 5,
    MP_CACHE_SECURITY_STATUS_IO_ERROR = 6,
    MP_CACHE_SECURITY_STATUS_UNAUTHORIZED = 7,
    MP_CACHE_SECURITY_STATUS_LIMIT_EXCEEDED = 8
} mp_cache_security_status_t;

/* Stores one persisted client principal and whether its bearer token is currently active. */
typedef struct {
    char client_id[MP_CACHE_CLIENT_ID_CAP];
    mp_cache_role_t role;
    bool token_active;
    uint8_t token_hash[MP_CACHE_TOKEN_HASH_SIZE];
} mp_cache_client_record_t;

/*
 * Owns the bootstrap admin hash and the mutable registered-client set.
 * Call mp_cache_security_init() before use and mp_cache_security_destroy() on shutdown.
 */
typedef struct {
    mp_cache_client_record_t *records;
    size_t count;
    size_t capacity;
    uint8_t bootstrap_admin_hash[MP_CACHE_TOKEN_HASH_SIZE];
    bool bootstrap_admin_loaded;
} mp_cache_security_t;

/* Carries the authenticated caller identity used by request handlers. */
typedef struct {
    mp_cache_role_t role;
    const char *client_id;
} mp_cache_principal_t;

/* Visits one registered client during mp_cache_security_for_each(). */
typedef int (*mp_cache_security_client_visit_fn)(const mp_cache_client_record_t *record, void *context);

/* Resolve an env: or file: secret reference into out_secret. */
int mp_cache_secret_resolve(const char *secret_ref, char *out_secret, size_t out_capacity);

/* Derive the fixed-width storage key bytes from a secret string. */
void mp_cache_secret_derive_key(const char *secret, uint8_t out_key[MP_CACHE_TOKEN_HASH_SIZE]);

/* Initialize security from config, including the bootstrap admin secret. */
int mp_cache_security_init(mp_cache_security_t *security, const mp_cache_config_t *config);

/* Release all client records and clear any loaded hashes. */
void mp_cache_security_destroy(mp_cache_security_t *security);

/* Authenticate auth_token and report the matched principal when successful. */
mp_cache_security_status_t mp_cache_security_authenticate(
    const mp_cache_security_t *security,
    const char *auth_token,
    mp_cache_principal_t *out_principal);

/* Register a new client principal and return a freshly minted bearer token. */
mp_cache_security_status_t mp_cache_security_register_client(
    mp_cache_security_t *security,
    const char *client_id,
    mp_cache_role_t role,
    char *out_client_token,
    size_t out_client_token_capacity);

/* Replace an existing client token and return the new bearer token text. */
mp_cache_security_status_t mp_cache_security_rotate_client_token(
    mp_cache_security_t *security,
    const char *client_id,
    char *out_client_token,
    size_t out_client_token_capacity);

/* Mark an existing client token inactive without removing the stored client principal. */
mp_cache_security_status_t mp_cache_security_invalidate_client_token(
    mp_cache_security_t *security,
    const char *client_id);

/* Import one client principal during restore or import workflows. */
mp_cache_security_status_t mp_cache_security_import_client_hash(
    mp_cache_security_t *security,
    const char *client_id,
    mp_cache_role_t role,
    const uint8_t token_hash[MP_CACHE_TOKEN_HASH_SIZE],
    bool is_token_active);

/* Remove every registered non-bootstrap client. */
void mp_cache_security_clear_clients(mp_cache_security_t *security);

/* Return the current number of registered non-bootstrap clients. */
size_t mp_cache_security_client_count(const mp_cache_security_t *security);

/* Walk every registered client until visit returns non-zero or the walk completes. */
int mp_cache_security_for_each_client(
    const mp_cache_security_t *security,
    mp_cache_security_client_visit_fn visit,
    void *context);

/* Return the stored client record for client_id or NULL when absent. */
const mp_cache_client_record_t *mp_cache_security_find_client(const mp_cache_security_t *security, const char *client_id);

/* Report whether actual_role satisfies the required minimum role. */
bool mp_cache_role_allows(mp_cache_role_t actual_role, mp_cache_role_t required_role);

/* Return the stable lowercase role name used in JSON and config. */
const char *mp_cache_role_name(mp_cache_role_t role);

/* Parse a lowercase role name into its enum value, or MP_CACHE_ROLE_NONE on failure. */
mp_cache_role_t mp_cache_role_from_string(const char *role_text);

/* Return a stable name for status suitable for logs and diagnostics. */
const char *mp_cache_security_status_name(mp_cache_security_status_t status);

#endif
