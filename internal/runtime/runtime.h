#ifndef MP_CACHE_INTERNAL_RUNTIME_RUNTIME_H
#define MP_CACHE_INTERNAL_RUNTIME_RUNTIME_H

/* Run the full service lifecycle or print the effective config when print_config_only is non-zero. */
int mp_cache_runtime_run(const char *config_path, int print_config_only);

#endif
