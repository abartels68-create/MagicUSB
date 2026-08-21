#include "update_client.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "psa/crypto.h"
#include "diagnostic.h"
#include "image_store.h"
#include "release_version.h"

#define MANIFEST_LIMIT 2048
#define IMAGE_LIMIT (64U * 1024U * 1024U)
#define TRANSFER_ATTEMPTS 3U
#define IMAGE_A_PARTIAL "/sdcard/magicusb/image-a.partial"
#define IMAGE_B_PARTIAL "/sdcard/magicusb/image-b.partial"
#define IMAGE_A "/sdcard/magicusb/image-a.fat"
#define IMAGE_B "/sdcard/magicusb/image-b.fat"

static const char *TAG = "update";
static const char *const partial_paths[] = { IMAGE_A_PARTIAL, IMAGE_B_PARTIAL };
static const char *const image_paths[] = { IMAGE_A, IMAGE_B };

static void configure_tls(esp_http_client_config_t *config, const char *url)
{
    if (strncmp(url, "https://", 8) == 0) config->crt_bundle_attach = esp_crt_bundle_attach;
}

static void retry_delay(unsigned attempt)
{
    vTaskDelay(pdMS_TO_TICKS(500U << attempt));
}

typedef struct {
    char data[MANIFEST_LIMIT + 1];
    size_t length;
    bool overflow;
} manifest_buffer_t;

typedef struct {
    FILE *file;
    psa_hash_operation_t hash;
    size_t bytes;
    bool failed;
} download_context_t;

static esp_err_t manifest_event(esp_http_client_event_t *event)
{
    manifest_buffer_t *buffer = event->user_data;
    if (event->event_id == HTTP_EVENT_ON_DATA && event->data_len > 0) {
        if ((size_t)event->data_len > MANIFEST_LIMIT - buffer->length) {
            buffer->overflow = true;
            return ESP_FAIL;
        }
        memcpy(buffer->data + buffer->length, event->data, event->data_len);
        buffer->length += event->data_len;
        buffer->data[buffer->length] = '\0';
    }
    return ESP_OK;
}

static esp_err_t download_event(esp_http_client_event_t *event)
{
    download_context_t *download = event->user_data;
    if (event->event_id == HTTP_EVENT_ON_DATA && event->data_len > 0) {
        if (download->bytes > IMAGE_LIMIT - (size_t)event->data_len ||
            fwrite(event->data, 1, event->data_len, download->file) != (size_t)event->data_len ||
            psa_hash_update(&download->hash, event->data, event->data_len) != PSA_SUCCESS) {
            download->failed = true;
            return ESP_FAIL;
        }
        download->bytes += event->data_len;
    }
    return ESP_OK;
}

static bool decode_hash(const char *hex, uint8_t output[32])
{
    if (hex == NULL || strlen(hex) != 64) return false;
    for (size_t i = 0; i < 32; ++i) {
        unsigned value;
        if (sscanf(hex + i * 2, "%2x", &value) != 1) return false;
        output[i] = (uint8_t)value;
    }
    return true;
}

static esp_err_t download_verified(const char *url, const char *partial_path,
                                   size_t expected_size, const uint8_t expected_hash[32])
{
    for (unsigned attempt = 0; attempt < TRANSFER_ATTEMPTS; ++attempt) {
        diagnostic_mark(44);
        FILE *file = fopen(partial_path, "wb");
        if (file == NULL) return ESP_FAIL;
        download_context_t download = { .file = file, .hash = PSA_HASH_OPERATION_INIT };
        diagnostic_mark(45);
        if (psa_hash_setup(&download.hash, PSA_ALG_SHA_256) != PSA_SUCCESS) {
            fclose(file);
            return ESP_FAIL;
        }
        esp_http_client_config_t config = {
            .url = url, .event_handler = download_event, .user_data = &download,
            .timeout_ms = 15000, .buffer_size = 1024,
        };
        configure_tls(&config, url);
        esp_http_client_handle_t client = esp_http_client_init(&config);
        diagnostic_mark(46);
        const esp_err_t transfer = client != NULL ? esp_http_client_perform(client) : ESP_ERR_NO_MEM;
        const int status = client != NULL ? esp_http_client_get_status_code(client) : 0;
        if (client != NULL) esp_http_client_cleanup(client);
        uint8_t actual_hash[32];
        size_t actual_hash_size = 0;
        diagnostic_mark(47);
        const psa_status_t hash_status = psa_hash_finish(&download.hash, actual_hash,
                                                         sizeof(actual_hash), &actual_hash_size);
        const bool flushed = fflush(file) == 0 && fsync(fileno(file)) == 0;
        fclose(file);
        const bool verified = transfer == ESP_OK && status == 200 && !download.failed && flushed &&
            download.bytes == expected_size && hash_status == PSA_SUCCESS && actual_hash_size == 32 &&
            memcmp(actual_hash, expected_hash, 32) == 0;
        if (verified) return ESP_OK;
        remove(partial_path);
        ESP_LOGW(TAG, "image attempt %u failed: err=%s status=%d bytes=%u", attempt + 1,
                 esp_err_to_name(transfer), status, (unsigned)download.bytes);
        if (attempt + 1 < TRANSFER_ATTEMPTS) retry_delay(attempt);
    }
    return ESP_ERR_INVALID_CRC;
}

esp_err_t update_client_stage(char *description, size_t description_size)
{
    if (description == NULL || description_size == 0) return ESP_ERR_INVALID_ARG;
    diagnostic_mark(40);
    nvs_handle_t nvs;
    if (nvs_open("magicusb", NVS_READONLY, &nvs) != ESP_OK) return ESP_ERR_NOT_FOUND;
    char manifest_url[256];
    size_t url_size = sizeof(manifest_url);
    uint8_t allow_http = 0;
    esp_err_t err = nvs_get_str(nvs, "update_url", manifest_url, &url_size);
    nvs_get_u8(nvs, "allow_http", &allow_http);
    nvs_close(nvs);
    if (err != ESP_OK) return err;
    if (strncmp(manifest_url, "https://", 8) != 0 &&
        !(allow_http == 1 && strncmp(manifest_url, "http://", 7) == 0)) return ESP_ERR_INVALID_ARG;

    manifest_buffer_t manifest = {0};
    esp_http_client_config_t config = {
        .url = manifest_url, .event_handler = manifest_event, .user_data = &manifest,
        .timeout_ms = 10000, .buffer_size = 1024,
    };
    configure_tls(&config, manifest_url);
    int status = 0;
    err = ESP_FAIL;
    for (unsigned attempt = 0; attempt < TRANSFER_ATTEMPTS; ++attempt) {
        memset(&manifest, 0, sizeof(manifest));
        diagnostic_mark(41);
        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (client == NULL) return ESP_ERR_NO_MEM;
        diagnostic_mark(42);
        err = esp_http_client_perform(client);
        status = esp_http_client_get_status_code(client);
        esp_http_client_cleanup(client);
        if (err == ESP_OK && status == 200 && !manifest.overflow) break;
        ESP_LOGW(TAG, "manifest attempt %u failed: err=%s status=%d", attempt + 1,
                 esp_err_to_name(err), status);
        if (attempt + 1 < TRANSFER_ATTEMPTS) retry_delay(attempt);
    }
    if (err != ESP_OK || status != 200 || manifest.overflow) return ESP_FAIL;

    diagnostic_mark(43);
    cJSON *root = cJSON_ParseWithLength(manifest.data, manifest.length);
    if (root == NULL) return ESP_ERR_INVALID_RESPONSE;
    const cJSON *schema = cJSON_GetObjectItemCaseSensitive(root, "schema_version");
    const cJSON *release = cJSON_GetObjectItemCaseSensitive(root, "release");
    const cJSON *size = cJSON_GetObjectItemCaseSensitive(root, "size");
    const cJSON *hash = cJSON_GetObjectItemCaseSensitive(root, "sha256");
    const cJSON *url = cJSON_GetObjectItemCaseSensitive(root, "download_url");
    uint8_t expected_hash[32];
    const bool valid = cJSON_IsNumber(schema) && schema->valueint == 1 && cJSON_IsString(release) &&
        release_version_valid(release->valuestring) && strlen(release->valuestring) < 24 &&
        cJSON_IsNumber(size) && size->valuedouble >= 512 && size->valuedouble <= IMAGE_LIMIT &&
        ((uint32_t)size->valuedouble % 512) == 0 && cJSON_IsString(hash) &&
        decode_hash(hash->valuestring, expected_hash) && cJSON_IsString(url) &&
        (strncmp(url->valuestring, "https://", 8) == 0 ||
         (allow_http == 1 && strncmp(url->valuestring, "http://", 7) == 0));
    if (!valid) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    const size_t expected_size = (size_t)size->valuedouble;
    if (image_store_active_matches(expected_size, expected_hash)) {
        snprintf(description, description_size, "Release %s already active", release->valuestring);
        cJSON_Delete(root);
        return ESP_ERR_NOT_FOUND;
    }
    const char *active_release = image_store_active_release();
    if (active_release[0] != '\0' && release_version_compare(release->valuestring, active_release) <= 0) {
        snprintf(description, description_size, "Release %s is not newer", release->valuestring);
        cJSON_Delete(root);
        return ESP_ERR_NOT_FOUND;
    }
    const unsigned inactive_slot = image_store_active_slot() ^ 1U;
    const char *partial_path = partial_paths[inactive_slot];
    const char *image_path = image_paths[inactive_slot];

    err = download_verified(url->valuestring, partial_path, expected_size, expected_hash);
    diagnostic_mark(48);
    if (err != ESP_OK) {
        cJSON_Delete(root);
        return err;
    }
    /* Replace only the inactive slot after the partial image is fully flushed
     * and verified. A power loss still leaves the active metadata/image pair. */
    remove(image_path);
    if (rename(partial_path, image_path) != 0) {
        remove(partial_path);
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    err = image_store_register_pending(inactive_slot, expected_size, expected_hash, release->valuestring);
    if (err != ESP_OK) {
        cJSON_Delete(root);
        return err;
    }
    snprintf(description, description_size, "Update %s verified in slot %c", release->valuestring,
             inactive_slot == 0 ? 'A' : 'B');
    ESP_LOGI(TAG, "%s", description);
    cJSON_Delete(root);
    return ESP_OK;
}
