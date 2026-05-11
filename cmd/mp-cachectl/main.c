#include "internal/crypto/crypto.h"
#include "internal/httpserver/http_server.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void mp_cachectl_print_usage(FILE *stream) {
    (void)fprintf(
        stream,
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

static int mp_cachectl_send(
    const char *socket_path,
    const char *method,
    const char *path,
    const char *token,
    const char *body) {
    return mp_cache_http_client_request(
        socket_path,
        method,
        path,
        token,
        body == NULL ? "application/json" : "application/json",
        body == NULL ? "" : body,
        stdout);
}

int main(int argc, char **argv) {
    const char *socket_path = "/tmp/mp-cache/run/mp-cache.sock";
    const char *token = getenv("MP_CACHE_TOKEN");
    const char *command = NULL;
    int index = 1;

    while (index < argc) {
        if (strcmp(argv[index], "--socket") == 0) {
            if (index + 1 >= argc) {
                mp_cachectl_print_usage(stderr);
                return 1;
            }
            socket_path = argv[index + 1];
            index += 2;
            continue;
        }
        if (strcmp(argv[index], "--token") == 0) {
            if (index + 1 >= argc) {
                mp_cachectl_print_usage(stderr);
                return 1;
            }
            token = argv[index + 1];
            index += 2;
            continue;
        }
        command = argv[index++];
        break;
    }

    if (command == NULL) {
        mp_cachectl_print_usage(stderr);
        return 1;
    }

    if (strcmp(command, "health") == 0) {
        return mp_cache_http_client_health(socket_path, stdout) == 0 ? 0 : 1;
    }
    if (strcmp(command, "get") == 0) {
        char path[512];
        if (index >= argc) {
            mp_cachectl_print_usage(stderr);
            return 1;
        }
        (void)snprintf(path, sizeof(path), "/v1/cache/%s", argv[index]);
        return mp_cachectl_send(socket_path, "GET", path, token, NULL) == 0 ? 0 : 1;
    }
    if (strcmp(command, "set") == 0) {
        char path[512];
        const char *value_text = NULL;
        char *value_base64 = NULL;
        size_t value_base64_length = 0u;
        char body[4096];
        uint32_t ttl_seconds = 0u;
        bool ttl_provided = false;
        size_t required_capacity = 0u;

        if (index + 1 >= argc) {
            mp_cachectl_print_usage(stderr);
            return 1;
        }
        (void)snprintf(path, sizeof(path), "/v1/cache/%s", argv[index]);
        value_text = argv[index + 1];
        index += 2;

        while (index < argc) {
            if (strcmp(argv[index], "--ttl") == 0 && index + 1 < argc) {
                ttl_seconds = (uint32_t)strtoul(argv[index + 1], NULL, 10);
                ttl_provided = true;
                index += 2;
                continue;
            }
            mp_cachectl_print_usage(stderr);
            return 1;
        }

        required_capacity = ((strlen(value_text) + 2u) / 3u) * 4u + 1u;
        value_base64 = malloc(required_capacity);
        if (value_base64 == NULL ||
            mp_cache_base64_encode(
                (const uint8_t *)value_text,
                strlen(value_text),
                value_base64,
                required_capacity,
                &value_base64_length) != 0) {
            free(value_base64);
            (void)fprintf(stderr, "error: failed to encode value\n");
            return 1;
        }

        if (ttl_provided) {
            (void)snprintf(body, sizeof(body), "{\"value_base64\":\"%s\",\"ttl_seconds\":%u}", value_base64, ttl_seconds);
        } else {
            (void)snprintf(body, sizeof(body), "{\"value_base64\":\"%s\"}", value_base64);
        }
        free(value_base64);
        return mp_cachectl_send(socket_path, "PUT", path, token, body) == 0 ? 0 : 1;
    }
    if (strcmp(command, "delete") == 0) {
        char path[512];
        if (index >= argc) {
            mp_cachectl_print_usage(stderr);
            return 1;
        }
        (void)snprintf(path, sizeof(path), "/v1/cache/%s", argv[index]);
        return mp_cachectl_send(socket_path, "DELETE", path, token, NULL) == 0 ? 0 : 1;
    }
    if (strcmp(command, "stats") == 0) {
        return mp_cachectl_send(socket_path, "GET", "/v1/stats", token, NULL) == 0 ? 0 : 1;
    }
    if (strcmp(command, "uptime") == 0) {
        return mp_cachectl_send(socket_path, "GET", "/v1/uptime", token, NULL) == 0 ? 0 : 1;
    }
    if (strcmp(command, "logs") == 0) {
        char path[512] = "/v1/logs";
        if (index < argc && strcmp(argv[index], "--tail") == 0 && index + 1 < argc) {
            (void)snprintf(path, sizeof(path), "/v1/logs?tail=%s", argv[index + 1]);
        }
        return mp_cachectl_send(socket_path, "GET", path, token, NULL) == 0 ? 0 : 1;
    }
    if (strcmp(command, "register-client") == 0) {
        char body[512];
        if (index + 1 >= argc) {
            mp_cachectl_print_usage(stderr);
            return 1;
        }
        (void)snprintf(body, sizeof(body), "{\"client_id\":\"%s\",\"role\":\"%s\"}", argv[index], argv[index + 1]);
        return mp_cachectl_send(socket_path, "POST", "/v1/clients", token, body) == 0 ? 0 : 1;
    }
    if (strcmp(command, "rotate-client") == 0) {
        char path[512];
        if (index >= argc) {
            mp_cachectl_print_usage(stderr);
            return 1;
        }
        (void)snprintf(path, sizeof(path), "/v1/clients/%s/rotate-token", argv[index]);
        return mp_cachectl_send(socket_path, "POST", path, token, "{}") == 0 ? 0 : 1;
    }
    if (strcmp(command, "export") == 0) {
        return mp_cachectl_send(socket_path, "POST", "/v1/export", token, "{}") == 0 ? 0 : 1;
    }
    if (strcmp(command, "import") == 0) {
        char body[MP_CACHE_PATH_CAP + 32u];
        if (index >= argc) {
            mp_cachectl_print_usage(stderr);
            return 1;
        }
        (void)snprintf(body, sizeof(body), "{\"path\":\"%s\"}", argv[index]);
        return mp_cachectl_send(socket_path, "POST", "/v1/import", token, body) == 0 ? 0 : 1;
    }
    if (strcmp(command, "purge-all") == 0) {
        return mp_cachectl_send(socket_path, "POST", "/v1/purge/all", token, "{}") == 0 ? 0 : 1;
    }

    mp_cachectl_print_usage(stderr);
    return 1;
}
