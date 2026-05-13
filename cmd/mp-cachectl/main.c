#include "internal/crypto/crypto.h"
#include "internal/httpserver/http_server.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Escape arbitrary CLI text for safe embedding inside a JSON string literal. */
static char *mp_cachectl_escape_json(const char *json_text) {
    size_t text_length = 0u;
    size_t capacity = 0u;
    char *escaped = NULL;
    size_t input_index = 0u;
    size_t output_index = 0u;

    if (json_text == NULL) {
        return NULL;
    }

    text_length = strlen(json_text);
    capacity = text_length * 6u + 1u;
    escaped = malloc(capacity);
    if (escaped == NULL) {
        return NULL;
    }

    for (input_index = 0u; input_index < text_length; input_index++) {
        unsigned char character = (unsigned char)json_text[input_index];

        switch (character) {
            case '\\':
            case '"':
                escaped[output_index++] = '\\';
                escaped[output_index++] = (char)character;
                break;
            case '\n':
                escaped[output_index++] = '\\';
                escaped[output_index++] = 'n';
                break;
            case '\r':
                escaped[output_index++] = '\\';
                escaped[output_index++] = 'r';
                break;
            case '\t':
                escaped[output_index++] = '\\';
                escaped[output_index++] = 't';
                break;
            default:
                if (character < 0x20u) {
                    (void)snprintf(escaped + output_index, capacity - output_index, "\\u%04x", character);
                    output_index += 6u;
                } else {
                    escaped[output_index++] = (char)character;
                }
                break;
        }
    }

    escaped[output_index] = '\0';
    return escaped;
}

/* Print the supported control CLI flags and commands. */
static void mp_cachectl_print_usage(FILE *usage_stream) {
    (void)fprintf(
        usage_stream,
        "usage: mp-cachectl [--socket <path>] [--token <token>] <command> [args]\n"
        "default socket path: /tmp/mp-cache/run/mp-cache.sock\n"
        "commands:\n"
        "  health\n"
        "  get <key>\n"
        "  set <key> <value> [--ttl <seconds>]\n"
        "  delete <key>\n"
        "  stats\n"
        "  uptime\n"
        "  logs [--tail <lines>]\n"
        "  register-client <client_id> <role>\n"
        "  rotate-client <client_id>\n"
        "  invalidate-client <client_id>\n"
        "  export\n"
        "  import <path>\n"
        "  purge-keys <key> [<key> ...]\n"
        "  purge-all\n");
}

/* Forward one CLI command as a local Unix-socket HTTP request and stream the response. */
static int mp_cachectl_send_http_request(
    const char *socket_path,
    const char *method,
    const char *request_path,
    const char *auth_token,
    const char *request_body) {
    return mp_cache_http_client_request(
        socket_path,
        method,
        request_path,
        auth_token,
        "application/json",
        request_body == NULL ? "" : request_body,
        stdout);
}

/* Parse CLI flags, map the command to an HTTP request, and execute it locally. */
int main(int argc, char **argv) {
    const char *socket_path = "/tmp/mp-cache/run/mp-cache.sock";
    const char *auth_token = getenv("MP_CACHE_TOKEN");
    const char *cli_command = NULL;
    int arg_index = 1;

    while (arg_index < argc) {
        if (strcmp(argv[arg_index], "--socket") == 0) {
            if (arg_index + 1 >= argc) {
                mp_cachectl_print_usage(stderr);
                return 1;
            }
            socket_path = argv[arg_index + 1];
            arg_index += 2;
            continue;
        }
        if (strcmp(argv[arg_index], "--token") == 0) {
            if (arg_index + 1 >= argc) {
                mp_cachectl_print_usage(stderr);
                return 1;
            }
            auth_token = argv[arg_index + 1];
            arg_index += 2;
            continue;
        }
        cli_command = argv[arg_index++];
        break;
    }

    if (cli_command == NULL) {
        mp_cachectl_print_usage(stderr);
        return 1;
    }

    if (strcmp(cli_command, "health") == 0) {
        return mp_cache_http_client_health(socket_path, stdout) == 0 ? 0 : 1;
    }
    if (strcmp(cli_command, "get") == 0) {
        char request_path[512];
        if (arg_index >= argc) {
            mp_cachectl_print_usage(stderr);
            return 1;
        }
        (void)snprintf(request_path, sizeof(request_path), "/v1/cache/%s", argv[arg_index]);
        return mp_cachectl_send_http_request(socket_path, "GET", request_path, auth_token, NULL) == 0 ? 0 : 1;
    }
    if (strcmp(cli_command, "set") == 0) {
        char request_path[512];
        const char *cache_value_text = NULL;
        char *cache_value_base64 = NULL;
        size_t cache_value_base64_length = 0u;
        char request_body[4096];
        uint32_t ttl_seconds = 0u;
        bool is_ttl_provided = false;
        size_t required_value_base64_capacity = 0u;

        if (arg_index + 1 >= argc) {
            mp_cachectl_print_usage(stderr);
            return 1;
        }
        (void)snprintf(request_path, sizeof(request_path), "/v1/cache/%s", argv[arg_index]);
        cache_value_text = argv[arg_index + 1];
        arg_index += 2;

        while (arg_index < argc) {
            if (strcmp(argv[arg_index], "--ttl") == 0 && arg_index + 1 < argc) {
                ttl_seconds = (uint32_t)strtoul(argv[arg_index + 1], NULL, 10);
                is_ttl_provided = true;
                arg_index += 2;
                continue;
            }
            mp_cachectl_print_usage(stderr);
            return 1;
        }

        required_value_base64_capacity = ((strlen(cache_value_text) + 2u) / 3u) * 4u + 1u;
        cache_value_base64 = malloc(required_value_base64_capacity);
        if (cache_value_base64 == NULL ||
            mp_cache_base64_encode(
                (const uint8_t *)cache_value_text,
                strlen(cache_value_text),
                cache_value_base64,
                required_value_base64_capacity,
                &cache_value_base64_length) != 0) {
            free(cache_value_base64);
            (void)fprintf(stderr, "error: failed to encode value\n");
            return 1;
        }

        if (is_ttl_provided) {
            (void)snprintf(
                request_body,
                sizeof(request_body),
                "{\"value_base64\":\"%s\",\"ttl_seconds\":%u}",
                cache_value_base64,
                ttl_seconds);
        } else {
            (void)snprintf(request_body, sizeof(request_body), "{\"value_base64\":\"%s\"}", cache_value_base64);
        }
        free(cache_value_base64);
        return mp_cachectl_send_http_request(socket_path, "PUT", request_path, auth_token, request_body) == 0 ? 0 : 1;
    }
    if (strcmp(cli_command, "delete") == 0) {
        char request_path[512];
        if (arg_index >= argc) {
            mp_cachectl_print_usage(stderr);
            return 1;
        }
        (void)snprintf(request_path, sizeof(request_path), "/v1/cache/%s", argv[arg_index]);
        return mp_cachectl_send_http_request(socket_path, "DELETE", request_path, auth_token, NULL) == 0 ? 0 : 1;
    }
    if (strcmp(cli_command, "stats") == 0) {
        return mp_cachectl_send_http_request(socket_path, "GET", "/v1/stats", auth_token, NULL) == 0 ? 0 : 1;
    }
    if (strcmp(cli_command, "uptime") == 0) {
        return mp_cachectl_send_http_request(socket_path, "GET", "/v1/uptime", auth_token, NULL) == 0 ? 0 : 1;
    }
    if (strcmp(cli_command, "logs") == 0) {
        char request_path[512] = "/v1/logs";
        if (arg_index < argc && strcmp(argv[arg_index], "--tail") == 0 && arg_index + 1 < argc) {
            (void)snprintf(request_path, sizeof(request_path), "/v1/logs?tail=%s", argv[arg_index + 1]);
        }
        return mp_cachectl_send_http_request(socket_path, "GET", request_path, auth_token, NULL) == 0 ? 0 : 1;
    }
    if (strcmp(cli_command, "register-client") == 0) {
        char *client_id_json = NULL;
        char *role_json = NULL;
        char *request_body = NULL;
        int request_body_length = 0;
        int request_status = 1;
        if (arg_index + 1 >= argc) {
            mp_cachectl_print_usage(stderr);
            return 1;
        }
        client_id_json = mp_cachectl_escape_json(argv[arg_index]);
        role_json = mp_cachectl_escape_json(argv[arg_index + 1]);
        if (client_id_json == NULL || role_json == NULL) {
            free(client_id_json);
            free(role_json);
            (void)fprintf(stderr, "error: failed to build request body\n");
            return 1;
        }

        request_body_length = snprintf(NULL, 0, "{\"client_id\":\"%s\",\"role\":\"%s\"}", client_id_json, role_json);
        request_body = malloc((size_t)request_body_length + 1u);
        if (request_body == NULL) {
            free(client_id_json);
            free(role_json);
            (void)fprintf(stderr, "error: failed to build request body\n");
            return 1;
        }
        (void)snprintf(request_body, (size_t)request_body_length + 1u, "{\"client_id\":\"%s\",\"role\":\"%s\"}", client_id_json, role_json);
        free(client_id_json);
        free(role_json);

        request_status = mp_cachectl_send_http_request(socket_path, "POST", "/v1/clients", auth_token, request_body) == 0 ? 0 : 1;
        free(request_body);
        return request_status;
    }
    if (strcmp(cli_command, "rotate-client") == 0) {
        char request_path[512];
        if (arg_index >= argc) {
            mp_cachectl_print_usage(stderr);
            return 1;
        }
        (void)snprintf(request_path, sizeof(request_path), "/v1/clients/%s/rotate-token", argv[arg_index]);
        return mp_cachectl_send_http_request(socket_path, "POST", request_path, auth_token, "{}") == 0 ? 0 : 1;
    }
    if (strcmp(cli_command, "invalidate-client") == 0) {
        char request_path[512];
        if (arg_index >= argc) {
            mp_cachectl_print_usage(stderr);
            return 1;
        }
        (void)snprintf(request_path, sizeof(request_path), "/v1/clients/%s/invalidate-token", argv[arg_index]);
        return mp_cachectl_send_http_request(socket_path, "POST", request_path, auth_token, "{}") == 0 ? 0 : 1;
    }
    if (strcmp(cli_command, "export") == 0) {
        return mp_cachectl_send_http_request(socket_path, "POST", "/v1/export", auth_token, "{}") == 0 ? 0 : 1;
    }
    if (strcmp(cli_command, "import") == 0) {
        char *import_path_json = NULL;
        char *request_body = NULL;
        int request_body_length = 0;
        int request_status = 1;
        if (arg_index >= argc) {
            mp_cachectl_print_usage(stderr);
            return 1;
        }
        import_path_json = mp_cachectl_escape_json(argv[arg_index]);
        if (import_path_json == NULL) {
            (void)fprintf(stderr, "error: failed to build request body\n");
            return 1;
        }

        request_body_length = snprintf(NULL, 0, "{\"path\":\"%s\"}", import_path_json);
        request_body = malloc((size_t)request_body_length + 1u);
        if (request_body == NULL) {
            free(import_path_json);
            (void)fprintf(stderr, "error: failed to build request body\n");
            return 1;
        }
        (void)snprintf(request_body, (size_t)request_body_length + 1u, "{\"path\":\"%s\"}", import_path_json);
        free(import_path_json);

        request_status = mp_cachectl_send_http_request(socket_path, "POST", "/v1/import", auth_token, request_body) == 0 ? 0 : 1;
        free(request_body);
        return request_status;
    }
    if (strcmp(cli_command, "purge-keys") == 0) {
        char *request_body = NULL;
        int first_key_arg_index = arg_index;
        int request_status = 1;
        size_t request_body_capacity = 16u;
        size_t request_body_length = 0u;
        bool is_first_key = true;

        if (arg_index >= argc) {
            mp_cachectl_print_usage(stderr);
            return 1;
        }
        while (first_key_arg_index < argc) {
            request_body_capacity += (strlen(argv[first_key_arg_index]) * 6u) + 4u;
            first_key_arg_index++;
        }

        request_body = malloc(request_body_capacity);
        if (request_body == NULL) {
            (void)fprintf(stderr, "error: failed to build request body\n");
            return 1;
        }

        request_body_length = (size_t)snprintf(request_body, request_body_capacity, "{\"keys\":[");
        while (arg_index < argc) {
            char *escaped_key = mp_cachectl_escape_json(argv[arg_index]);
            int written = 0;

            if (escaped_key == NULL) {
                free(request_body);
                (void)fprintf(stderr, "error: failed to build request body\n");
                return 1;
            }

            written = snprintf(
                request_body + request_body_length,
                request_body_capacity - request_body_length,
                "%s\"%s\"",
                is_first_key ? "" : ",",
                escaped_key);
            free(escaped_key);
            if (written < 0 || (size_t)written >= request_body_capacity - request_body_length) {
                free(request_body);
                (void)fprintf(stderr, "error: failed to build request body\n");
                return 1;
            }
            request_body_length += (size_t)written;
            is_first_key = false;
            arg_index++;
        }

        if (snprintf(request_body + request_body_length, request_body_capacity - request_body_length, "]}") < 0 ||
            request_body_length + 2u >= request_body_capacity) {
            free(request_body);
            (void)fprintf(stderr, "error: failed to build request body\n");
            return 1;
        }

        request_status = mp_cachectl_send_http_request(socket_path, "POST", "/v1/purge/keys", auth_token, request_body) == 0 ? 0 : 1;
        free(request_body);
        return request_status;
    }
    if (strcmp(cli_command, "purge-all") == 0) {
        return mp_cachectl_send_http_request(socket_path, "POST", "/v1/purge/all", auth_token, "{}") == 0 ? 0 : 1;
    }

    mp_cachectl_print_usage(stderr);
    return 1;
}
