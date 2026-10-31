#include "storage.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_littlefs.h"
#include "esp_log.h"

static const char *TAG = "storage";

static bool s_available;

esp_err_t storage_init(void)
{
    const esp_vfs_littlefs_conf_t config = {
        .base_path = STORAGE_BASE_PATH,
        .partition_label = STORAGE_PARTITION,
        .format_if_mount_failed = true, /* first boot: format the partition */
        .dont_mount = false,
    };

    esp_err_t err = esp_vfs_littlefs_register(&config);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "partition \"%s\" not found - is the partition table flashed?", STORAGE_PARTITION);
        } else {
            ESP_LOGE(TAG, "mounting %s failed: %s", STORAGE_BASE_PATH, esp_err_to_name(err));
        }
        return err;
    }

    size_t total = 0;
    size_t used = 0;
    if (esp_littlefs_info(STORAGE_PARTITION, &total, &used) == ESP_OK) {
        ESP_LOGI(TAG, "littlefs mounted at %s: %u KiB total, %u KiB free", STORAGE_BASE_PATH,
                 (unsigned)(total / 1024), (unsigned)((total - used) / 1024));
    }

    s_available = true;
    return ESP_OK;
}

bool storage_is_available(void)
{
    return s_available;
}

esp_err_t storage_info(size_t *total_bytes, size_t *used_bytes)
{
    if (!s_available) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_littlefs_info(STORAGE_PARTITION, total_bytes, used_bytes);
}

/* Counts the files below `path`; returns -1 if the directory cannot be opened. */
static int count_files(const char *path, const char *suffix, size_t *bytes)
{
    DIR *dir = opendir(path);
    if (dir == NULL) {
        return -1;
    }

    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char full[160];
        const int written = snprintf(full, sizeof(full), "%s/%s", path, entry->d_name);
        if (written <= 0 || written >= (int)sizeof(full)) {
            continue;
        }

        struct stat info;
        if (stat(full, &info) != 0) {
            continue;
        }

        if (S_ISDIR(info.st_mode)) {
            const int nested = count_files(full, suffix, bytes);
            if (nested > 0) {
                count += nested;
            }
            continue;
        }

        if (suffix == NULL || strstr(entry->d_name, suffix) != NULL) {
            count++;
            if (bytes != NULL) {
                *bytes += (size_t)info.st_size;
            }
        }
    }

    closedir(dir);
    return count;
}

int storage_count(const char *path, const char *suffix)
{
    if (!s_available) {
        return -1;
    }
    return count_files(path, suffix, NULL);
}

/* Logs one line per directory that contains files, then a total. */
static void walk_and_log(const char *path, int depth, int *files, size_t *bytes)
{
    DIR *dir = opendir(path);
    if (dir == NULL) {
        return;
    }

    int files_here = 0;
    size_t bytes_here = 0;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char full[160];
        const int written = snprintf(full, sizeof(full), "%s/%s", path, entry->d_name);
        if (written <= 0 || written >= (int)sizeof(full)) {
            continue;
        }

        struct stat info;
        if (stat(full, &info) != 0) {
            continue;
        }

        if (S_ISDIR(info.st_mode)) {
            walk_and_log(full, depth + 1, files, bytes);
        } else {
            files_here++;
            bytes_here += (size_t)info.st_size;
            (*files)++;
            *bytes += (size_t)info.st_size;
        }
    }

    closedir(dir);

    if (files_here > 0) {
        ESP_LOGI(TAG, "%s: %d files, %u bytes", path, files_here, (unsigned)bytes_here);
    }
}

void storage_log_summary(void)
{
    if (!s_available) {
        ESP_LOGW(TAG, "no file system mounted");
        return;
    }

    int files = 0;
    size_t bytes = 0;
    walk_and_log(STORAGE_BASE_PATH, 0, &files, &bytes);

    const int jpgs = storage_count(STORAGE_BASE_PATH, ".jpg") + storage_count(STORAGE_BASE_PATH, ".JPG");
    ESP_LOGI(TAG, "%d files in total (%d JPEG), %u bytes", files, jpgs, (unsigned)bytes);
}
