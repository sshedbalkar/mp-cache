#include "internal/runtime/runtime.h"

#include <stdio.h>
#include <string.h>

/* Print the supported server CLI flags and their defaults. */
static void mp_cache_server_print_usage(FILE *usage_stream) {
    (void)fprintf(
        usage_stream,
        "usage: mp-cache-server [--config <path>] [--print-config]\n"
        "default config path: configs/bootstrap.ini\n");
}

/* Parse the small server CLI surface and hand execution to the runtime module. */
int main(int argc, char **argv) {
    const char *config_path = "configs/bootstrap.ini";
    int print_config_only = 0;
    int arg_index = 0;

    for (arg_index = 1; arg_index < argc; arg_index++) {
        if (strcmp(argv[arg_index], "--config") == 0) {
            if (arg_index + 1 >= argc) {
                mp_cache_server_print_usage(stderr);
                return 1;
            }
            config_path = argv[++arg_index];
            continue;
        }
        if (strcmp(argv[arg_index], "--print-config") == 0) {
            print_config_only = 1;
            continue;
        }
        if (strcmp(argv[arg_index], "--help") == 0 || strcmp(argv[arg_index], "-h") == 0) {
            mp_cache_server_print_usage(stdout);
            return 0;
        }

        mp_cache_server_print_usage(stderr);
        return 1;
    }

    return mp_cache_runtime_run(config_path, print_config_only);
}
