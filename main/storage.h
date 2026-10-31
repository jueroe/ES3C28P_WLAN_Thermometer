/*
 * littlefs partition "storage": contents of the wallpaper image.
 *
 * The image is generated from the project folder data/ by the build (see
 * main/CMakeLists.txt), so everything below data/ appears here with the same
 * relative path:
 *
 *     data/backgrounds/spring/day/1.jpg  ->  /littlefs/backgrounds/spring/day/1.jpg
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#define STORAGE_BASE_PATH "/littlefs"
#define STORAGE_PARTITION "storage"

/** Mount the partition, formatting it on the first use. */
esp_err_t storage_init(void);

/** True once storage_init() succeeded. */
bool storage_is_available(void);

/** Capacity and usage of the mounted file system, in bytes. */
esp_err_t storage_info(size_t *total_bytes, size_t *used_bytes);

/** Count the files below `path`, `suffix` may be NULL to count everything. */
int storage_count(const char *path, const char *suffix);

/** Log a recursive directory listing with file count and size per folder. */
void storage_log_summary(void);
