#include "storage.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>

#include "esp_log.h"

#define STORAGE_ROOT "/sdcard/magicusb"
#define LAYOUT_MARKER STORAGE_ROOT "/layout-v1.txt"

static const char *TAG = "storage";

esp_err_t storage_prepare(char *description, size_t description_size)
{
    if (description == NULL || description_size == 0) return ESP_ERR_INVALID_ARG;

    struct stat info;
    bool created = false;
    if (stat(STORAGE_ROOT, &info) != 0) {
        if (mkdir(STORAGE_ROOT, 0755) != 0) {
            snprintf(description, description_size, "SD storage: directory error %d", errno);
            return ESP_FAIL;
        }
        created = true;
    } else if (!S_ISDIR(info.st_mode)) {
        snprintf(description, description_size, "SD storage: magicusb is not a directory");
        return ESP_ERR_INVALID_STATE;
    }

    if (stat(LAYOUT_MARKER, &info) != 0) {
        FILE *marker = fopen(LAYOUT_MARKER, "wb");
        if (marker == NULL) {
            snprintf(description, description_size, "SD storage: marker open error %d", errno);
            return ESP_FAIL;
        }
        static const char contents[] =
            "MagicUSB management storage v1\r\n"
            "This filesystem is private to firmware and must never be exposed over USB.\r\n";
        const size_t written = fwrite(contents, 1, sizeof(contents) - 1, marker);
        const int close_result = fclose(marker);
        if (written != sizeof(contents) - 1 || close_result != 0) {
            snprintf(description, description_size, "SD storage: marker write error");
            return ESP_FAIL;
        }
        created = true;
    }

    snprintf(description, description_size, "SD storage: %s, isolated from USB",
             created ? "initialized" : "ready");
    ESP_LOGI(TAG, "%s", description);
    return ESP_OK;
}
