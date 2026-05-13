#define _POSIX_C_SOURCE 200809L

#include "internal/config/config.h"
#include "internal/security/security.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* Verify bootstrap-admin secret resolution and authentication. */
static void test_bootstrap_admin_authentication(void) {
    mp_cache_config_t config;
    mp_cache_security_t security;
    mp_cache_principal_t principal;

    assert(setenv("MP_TEST_BOOTSTRAP_ADMIN_TOKEN", "bootstrap-admin-token", 1) == 0);
    mp_cache_config_init_defaults(&config);
    (void)snprintf(
        config.bootstrap_admin_token_secret_ref,
        sizeof(config.bootstrap_admin_token_secret_ref),
        "env:MP_TEST_BOOTSTRAP_ADMIN_TOKEN");

    assert(mp_cache_security_init(&security, &config) == 0);
    assert(
        mp_cache_security_authenticate(&security, "bootstrap-admin-token", &principal) == MP_CACHE_SECURITY_STATUS_OK);
    assert(principal.role == MP_CACHE_ROLE_ADMIN);
    assert(strcmp(principal.client_id, "bootstrap-admin") == 0);

    mp_cache_security_destroy(&security);
}

/* Verify client registration, token rotation, invalidation, and reissue flows. */
static void test_client_registration_rotation_and_invalidation(void) {
    mp_cache_config_t config;
    mp_cache_security_t security;
    mp_cache_principal_t principal;
    char token[MP_CACHE_TOKEN_TEXT_CAP];
    char rotated_token[MP_CACHE_TOKEN_TEXT_CAP];
    char reissued_token[MP_CACHE_TOKEN_TEXT_CAP];
    const mp_cache_client_record_t *record = NULL;

    assert(setenv("MP_TEST_BOOTSTRAP_ADMIN_TOKEN", "bootstrap-admin-token", 1) == 0);
    mp_cache_config_init_defaults(&config);
    (void)snprintf(
        config.bootstrap_admin_token_secret_ref,
        sizeof(config.bootstrap_admin_token_secret_ref),
        "env:MP_TEST_BOOTSTRAP_ADMIN_TOKEN");

    assert(mp_cache_security_init(&security, &config) == 0);
    assert(
        mp_cache_security_register_client(&security, "client-one", MP_CACHE_ROLE_CLIENT, token, sizeof(token)) ==
        MP_CACHE_SECURITY_STATUS_OK);
    assert(
        mp_cache_security_authenticate(&security, token, &principal) == MP_CACHE_SECURITY_STATUS_OK);
    assert(principal.role == MP_CACHE_ROLE_CLIENT);
    assert(strcmp(principal.client_id, "client-one") == 0);

    assert(
        mp_cache_security_rotate_client_token(&security, "client-one", rotated_token, sizeof(rotated_token)) ==
        MP_CACHE_SECURITY_STATUS_OK);
    assert(strcmp(token, rotated_token) != 0);
    assert(
        mp_cache_security_authenticate(&security, token, &principal) == MP_CACHE_SECURITY_STATUS_UNAUTHORIZED);
    assert(
        mp_cache_security_authenticate(&security, rotated_token, &principal) == MP_CACHE_SECURITY_STATUS_OK);

    assert(
        mp_cache_security_invalidate_client_token(&security, "client-one") ==
        MP_CACHE_SECURITY_STATUS_OK);
    record = mp_cache_security_find_client(&security, "client-one");
    assert(record != NULL);
    assert(record->token_active == false);
    assert(
        mp_cache_security_authenticate(&security, rotated_token, &principal) ==
        MP_CACHE_SECURITY_STATUS_UNAUTHORIZED);

    assert(
        mp_cache_security_rotate_client_token(&security, "client-one", reissued_token, sizeof(reissued_token)) ==
        MP_CACHE_SECURITY_STATUS_OK);
    assert(strcmp(rotated_token, reissued_token) != 0);
    assert(
        mp_cache_security_authenticate(&security, reissued_token, &principal) ==
        MP_CACHE_SECURITY_STATUS_OK);
    assert(principal.role == MP_CACHE_ROLE_CLIENT);
    assert(strcmp(principal.client_id, "client-one") == 0);

    mp_cache_security_destroy(&security);
}

/* Run the security unit-test group. */
int main(void) {
    test_bootstrap_admin_authentication();
    test_client_registration_rotation_and_invalidation();
    return 0;
}
