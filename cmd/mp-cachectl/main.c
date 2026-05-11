#include "internal/crypto/crypto.h"
#include "internal/httpserver/http_server.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
        "  export\n"
        "  import <path>\n"
        "  purge-all\n");
}

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
        char request_body[512];
        if (arg_index + 1 >= argc) {
            mp_cachectl_print_usage(stderr);
            return 1;
        }
        (void)snprintf(
            request_body,
            sizeof(request_body),
            "{\"client_id\":\"%s\",\"role\":\"%s\"}",
            argv[arg_index],
            argv[arg_index + 1]);
        return mp_cachectl_send_http_request(socket_path, "POST", "/v1/clients", auth_token, request_body) == 0 ? 0 : 1;
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
    if (strcmp(cli_command, "export") == 0) {
        return mp_cachectl_send_http_request(socket_path, "POST", "/v1/export", auth_token, "{}") == 0 ? 0 : 1;
    }
    if (strcmp(cli_command, "import") == 0) {
        char request_body[MP_CACHE_PATH_CAP + 32u];
        if (arg_index >= argc) {
            mp_cachectl_print_usage(stderr);
            return 1;
        }
        (void)snprintf(request_body, sizeof(request_body), "{\"path\":\"%s\"}", argv[arg_index]);
        return mp_cachectl_send_http_request(socket_path, "POST", "/v1/import", auth_token, request_body) == 0 ? 0 : 1;
    }
    if (strcmp(cli_command, "purge-all") == 0) {
        return mp_cachectl_send_http_request(socket_path, "POST", "/v1/purge/all", auth_token, "{}") == 0 ? 0 : 1;
    }

    mp_cachectl_print_usage(stderr);
    return 1;
}
