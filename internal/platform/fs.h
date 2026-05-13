#ifndef MP_CACHE_INTERNAL_PLATFORM_FS_H
#define MP_CACHE_INTERNAL_PLATFORM_FS_H

#include <sys/types.h>

/* Create filesystem_path and any missing parent directories. */
int mp_cache_fs_ensure_directory(const char *filesystem_path);

/* Create the parent directory chain for filesystem_path when one exists. */
int mp_cache_fs_ensure_parent_directory(const char *filesystem_path);

/* Unlink filesystem_path and treat a missing path as success. */
int mp_cache_fs_remove_path_if_exists(const char *filesystem_path);

/* Write pid into pid_file_path after ensuring the parent directory exists. */
int mp_cache_fs_write_pid_file(const char *pid_file_path, pid_t pid);

#endif
