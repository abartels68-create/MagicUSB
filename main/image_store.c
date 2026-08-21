#include "image_store.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "esp_check.h"
#include "esp_log.h"
#include "fault_injection.h"
#include "psa/crypto.h"

#define STORE_ROOT "/sdcard/magicusb"
#define META_MAGIC 0x4253554dU
#define META_VERSION 2U
#define RELEASE_LENGTH 24U

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    uint64_t generation;
    uint32_t active_slot;
    uint32_t image_size;
    uint8_t sha256[32];
    char release[RELEASE_LENGTH];
    uint32_t crc32;
} image_metadata_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    uint64_t generation;
    uint32_t active_slot;
    uint32_t image_size;
    uint8_t sha256[32];
    uint32_t crc32;
} image_metadata_v1_t;

static const char *TAG = "image_store";
static const char *const image_paths[] = {
    STORE_ROOT "/image-a.fat", STORE_ROOT "/image-b.fat",
};
static const char *const metadata_paths[] = {
    STORE_ROOT "/metadata.0.bin", STORE_ROOT "/metadata.1.bin",
};
static const char *const metadata_temps[] = {
    STORE_ROOT "/metadata.0.partial", STORE_ROOT "/metadata.1.partial",
};

static FILE *active_image;
static size_t active_size;
static image_metadata_t active_metadata;
static unsigned active_record;
static bool pending;
static unsigned pending_slot;
static size_t pending_size;
static uint8_t pending_hash[32];
static char pending_release[RELEASE_LENGTH];

static uint32_t crc32(const void *data, size_t length)
{
    uint32_t crc = 0xffffffffU;
    const uint8_t *bytes = data;
    while (length-- > 0) {
        crc ^= *bytes++;
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

static esp_err_t hash_file(FILE *file, uint8_t digest[32], size_t expected_size)
{
    if (fseek(file, 0, SEEK_SET) != 0) return ESP_FAIL;
    psa_hash_operation_t context = PSA_HASH_OPERATION_INIT;
    if (psa_hash_setup(&context, PSA_ALG_SHA_256) != PSA_SUCCESS) return ESP_FAIL;
    uint8_t buffer[1024];
    size_t total = 0;
    while (true) {
        const size_t count = fread(buffer, 1, sizeof(buffer), file);
        if (count > 0) {
            if (psa_hash_update(&context, buffer, count) != PSA_SUCCESS) {
                psa_hash_abort(&context);
                return ESP_FAIL;
            }
            total += count;
        }
        if (count < sizeof(buffer)) break;
    }
    size_t digest_length = 0;
    if (ferror(file) || total != expected_size ||
        psa_hash_finish(&context, digest, 32, &digest_length) != PSA_SUCCESS || digest_length != 32) {
        psa_hash_abort(&context);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t flush_file(FILE *file)
{
    return fflush(file) == 0 && fsync(fileno(file)) == 0 ? ESP_OK : ESP_FAIL;
}

static bool read_metadata(unsigned record, image_metadata_t *metadata)
{
    FILE *file = fopen(metadata_paths[record], "rb");
    if (file == NULL) return false;
    uint32_t header[2];
    bool valid = fread(header, 1, sizeof(header), file) == sizeof(header) && header[0] == META_MAGIC;
    if (valid && header[1] == 1U) {
        image_metadata_v1_t old;
        rewind(file);
        valid = fread(&old, 1, sizeof(old), file) == sizeof(old) &&
                old.active_slot < 2 && old.image_size >= 512 && old.image_size % 512 == 0 &&
                old.crc32 == crc32(&old, offsetof(image_metadata_v1_t, crc32));
        if (valid) {
            memset(metadata, 0, sizeof(*metadata));
            metadata->magic = old.magic;
            metadata->version = old.version;
            metadata->generation = old.generation;
            metadata->active_slot = old.active_slot;
            metadata->image_size = old.image_size;
            memcpy(metadata->sha256, old.sha256, sizeof(old.sha256));
        }
    } else if (valid && header[1] == META_VERSION) {
        rewind(file);
        valid = fread(metadata, 1, sizeof(*metadata), file) == sizeof(*metadata) &&
                metadata->active_slot < 2 && metadata->image_size >= 512 &&
                metadata->image_size % 512 == 0 && metadata->release[RELEASE_LENGTH - 1] == '\0' &&
                metadata->crc32 == crc32(metadata, offsetof(image_metadata_t, crc32));
    } else {
        valid = false;
    }
    fclose(file);
    return valid;
}

static FILE *open_verified_image(const image_metadata_t *metadata)
{
    FILE *file = fopen(image_paths[metadata->active_slot], "rb");
    uint8_t digest[32];
    if (file == NULL || hash_file(file, digest, metadata->image_size) != ESP_OK ||
        memcmp(digest, metadata->sha256, sizeof(digest)) != 0) {
        if (file != NULL) fclose(file);
        return NULL;
    }
    return file;
}

static esp_err_t write_metadata(unsigned record, const image_metadata_t *metadata)
{
    FILE *file = fopen(metadata_temps[record], "wb");
    if (file == NULL) return ESP_FAIL;
    const bool ok = fwrite(metadata, 1, sizeof(*metadata), file) == sizeof(*metadata) &&
                    flush_file(file) == ESP_OK;
    fclose(file);
    if (!ok) {
        remove(metadata_temps[record]);
        return ESP_FAIL;
    }
    remove(metadata_paths[record]);
    if (rename(metadata_temps[record], metadata_paths[record]) != 0) {
        remove(metadata_temps[record]);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t seed_slot_a(const uint8_t *image, size_t size)
{
    static const char temp_image[] = STORE_ROOT "/image-a.partial";
    FILE *file = fopen(temp_image, "wb");
    if (file == NULL) return ESP_FAIL;
    const bool image_ok = fwrite(image, 1, size, file) == size && flush_file(file) == ESP_OK;
    fclose(file);
    if (!image_ok) return ESP_FAIL;
    remove(image_paths[0]);
    if (rename(temp_image, image_paths[0]) != 0) return ESP_FAIL;

    image_metadata_t metadata = {
        .magic = META_MAGIC, .version = META_VERSION, .generation = 1,
        .active_slot = 0, .image_size = (uint32_t)size,
    };
    file = fopen(image_paths[0], "rb");
    if (file == NULL || hash_file(file, metadata.sha256, size) != ESP_OK) {
        if (file != NULL) fclose(file);
        return ESP_FAIL;
    }
    fclose(file);
    metadata.crc32 = crc32(&metadata, offsetof(image_metadata_t, crc32));
    return write_metadata(0, &metadata);
}

esp_err_t image_store_prepare(const uint8_t *seed_image, size_t seed_size,
                              char *description, size_t description_size)
{
    if (seed_image == NULL || seed_size == 0 || seed_size > UINT32_MAX ||
        description == NULL || description_size == 0) return ESP_ERR_INVALID_ARG;

    image_metadata_t records[2];
    bool valid[2] = { read_metadata(0, &records[0]), read_metadata(1, &records[1]) };
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        int selected = -1;
        for (unsigned record = 0; record < 2; ++record) {
            if (valid[record] && (selected < 0 || records[record].generation > records[selected].generation)) {
                selected = (int)record;
            }
        }
        if (selected < 0) break;
        FILE *verified = open_verified_image(&records[selected]);
        if (verified != NULL) {
            active_image = verified;
            active_size = records[selected].image_size;
            active_metadata = records[selected];
            active_record = (unsigned)selected;
            snprintf(description, description_size, "Image %c verified, %u KiB",
                     records[selected].active_slot == 0 ? 'A' : 'B', (unsigned)(active_size / 1024));
            ESP_LOGI(TAG, "%s", description);
            return ESP_OK;
        }
        ESP_LOGW(TAG, "metadata %d image failed verification; trying older record", selected);
        valid[selected] = false;
    }

    ESP_LOGW(TAG, "no valid metadata/image pair; seeding slot A from immutable fallback");
    ESP_RETURN_ON_ERROR(seed_slot_a(seed_image, seed_size), TAG, "seed slot A");
    return image_store_prepare(seed_image, seed_size, description, description_size);
}

esp_err_t image_store_register_pending(unsigned slot, size_t size, const uint8_t sha256[32], const char *release)
{
    if (size < 512 || size > UINT32_MAX || size % 512 != 0 || sha256 == NULL || release == NULL ||
        strlen(release) >= RELEASE_LENGTH ||
        slot >= 2 || active_metadata.active_slot == slot) return ESP_ERR_INVALID_ARG;
    pending_slot = slot;
    pending_size = size;
    memcpy(pending_hash, sha256, sizeof(pending_hash));
    strlcpy(pending_release, release, sizeof(pending_release));
    pending = true;
    return ESP_OK;
}

bool image_store_pending(void) { return pending; }
unsigned image_store_pending_slot(void) { return pending_slot; }
unsigned image_store_active_slot(void) { return active_metadata.active_slot; }

bool image_store_active_matches(size_t size, const uint8_t sha256[32])
{
    return active_image != NULL && sha256 != NULL && size == active_metadata.image_size &&
           memcmp(sha256, active_metadata.sha256, sizeof(active_metadata.sha256)) == 0;
}

const char *image_store_active_release(void) { return active_metadata.release; }

esp_err_t image_store_activate_pending(char *description, size_t description_size)
{
    if (!pending || description == NULL || description_size == 0) return ESP_ERR_INVALID_STATE;
    image_metadata_t next = {
        .magic = META_MAGIC, .version = META_VERSION,
        .generation = active_metadata.generation + 1,
        .active_slot = pending_slot, .image_size = (uint32_t)pending_size,
    };
    memcpy(next.sha256, pending_hash, sizeof(next.sha256));
    strlcpy(next.release, pending_release, sizeof(next.release));
    FILE *verified = open_verified_image(&next);
    if (verified == NULL) return ESP_ERR_INVALID_CRC;
    next.crc32 = crc32(&next, offsetof(image_metadata_t, crc32));
    const unsigned next_record = active_record ^ 1U;
    const esp_err_t metadata_status = write_metadata(next_record, &next);
    if (metadata_status != ESP_OK) {
        fclose(verified);
        return metadata_status;
    }
    fault_injection_maybe_restart(FAULT_STAGE_METADATA_COMMITTED);

    FILE *old = active_image;
    active_image = verified;
    active_size = next.image_size;
    active_metadata = next;
    active_record = next_record;
    pending = false;
    if (old != NULL) fclose(old);
    snprintf(description, description_size, "Image %c active, generation %llu",
             next.active_slot == 0 ? 'A' : 'B', (unsigned long long)next.generation);
    return ESP_OK;
}

bool image_store_ready(void) { return active_image != NULL; }
size_t image_store_size(void) { return active_size; }

esp_err_t image_store_read(size_t offset, void *buffer, size_t length)
{
    if (active_image == NULL || buffer == NULL || offset > active_size || length > active_size - offset) {
        return ESP_ERR_INVALID_ARG;
    }
    if (fseek(active_image, (long)offset, SEEK_SET) != 0) return ESP_FAIL;
    return fread(buffer, 1, length, active_image) == length ? ESP_OK : ESP_FAIL;
}
