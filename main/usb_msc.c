#include "usb_msc.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "image_store.h"
#include "ram_disk.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tusb.h"

static const char *TAG = "usb_msc";
static bool media_present = true;
static uint32_t active_reads;
static int64_t last_read_us;
static portMUX_TYPE media_lock = portMUX_INITIALIZER_UNLOCKED;

/* esp_tinyusb calls these ownership hooks when MSC is enabled. Its bundled
 * whole-medium backend is excluded in the top-level CMakeLists, so the custom
 * immutable RAM/image backend has no mount transition to perform. */
void msc_storage_mount_to_app(void) {}
void msc_storage_mount_to_usb(void) {}

esp_err_t usb_msc_start(const char *diagnostic)
{
    ram_disk_init(diagnostic);
    const tinyusb_config_t config = TINYUSB_DEFAULT_CONFIG();
    ESP_LOGI(TAG, "starting read-only %u KiB MSC disk from %s",
             (unsigned)((image_store_ready() ? image_store_size() : ram_disk_size()) / 1024),
             image_store_ready() ? "verified SD image" : "RAM fallback");
    return tinyusb_driver_install(&config);
}

esp_err_t usb_msc_activate_pending(char *description, size_t description_size)
{
    if (!image_store_pending()) return ESP_ERR_INVALID_STATE;

    portENTER_CRITICAL(&media_lock);
    media_present = false;
    portEXIT_CRITICAL(&media_lock);
    tud_disconnect();

    const int64_t deadline = esp_timer_get_time() + 1000000;
    while (true) {
        portENTER_CRITICAL(&media_lock);
        const uint32_t readers = active_reads;
        portEXIT_CRITICAL(&media_lock);
        if (readers == 0) break;
        if (esp_timer_get_time() >= deadline) {
            portENTER_CRITICAL(&media_lock);
            media_present = true;
            portEXIT_CRITICAL(&media_lock);
            tud_connect();
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelay(pdMS_TO_TICKS(250));

    const esp_err_t result = image_store_activate_pending(description, description_size);
    portENTER_CRITICAL(&media_lock);
    media_present = true;
    last_read_us = esp_timer_get_time();
    portEXIT_CRITICAL(&media_lock);
    tud_connect();
    return result;
}

void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4])
{
    (void)lun;
    memcpy(vendor_id, "MAGICUSB", 8);
    memcpy(product_id, "WIRELESS DRIVE  ", 16);
    memcpy(product_rev, "0.1 ", 4);
}

bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
    (void)lun;
    if (!media_present) {
        tud_msc_set_sense(0, SCSI_SENSE_NOT_READY, 0x3a, 0x00);
        return false;
    }
    return true;
}
void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size)
{
    (void)lun;
    *block_count = (uint32_t)((image_store_ready() ? image_store_size() : ram_disk_size()) / RAM_DISK_BLOCK_SIZE);
    *block_size = RAM_DISK_BLOCK_SIZE;
}
bool tud_msc_is_writable_cb(uint8_t lun) { (void)lun; return false; }
bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject)
{
    (void)lun; (void)power_condition; (void)start; (void)load_eject; return true;
}
int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize)
{
    (void)lun;
    portENTER_CRITICAL(&media_lock);
    if (!media_present) {
        portEXIT_CRITICAL(&media_lock);
        return -1;
    }
    ++active_reads;
    portEXIT_CRITICAL(&media_lock);
    const size_t address = (size_t)lba * RAM_DISK_BLOCK_SIZE + offset;
    const size_t disk_size = image_store_ready() ? image_store_size() : ram_disk_size();
    int32_t result = -1;
    if (address <= disk_size && bufsize <= disk_size - address) {
        if (image_store_ready()) {
            if (image_store_read(address, buffer, bufsize) == ESP_OK) result = (int32_t)bufsize;
        } else {
            memcpy(buffer, ram_disk_data() + address, bufsize);
            result = (int32_t)bufsize;
        }
    }
    portENTER_CRITICAL(&media_lock);
    --active_reads;
    last_read_us = esp_timer_get_time();
    portEXIT_CRITICAL(&media_lock);
    return result;
}
int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize)
{
    (void)lun; (void)lba; (void)offset; (void)buffer; (void)bufsize;
    tud_msc_set_sense(0, SCSI_SENSE_DATA_PROTECT, 0x27, 0x00);
    return -1;
}
int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void *buffer, uint16_t bufsize)
{
    (void)scsi_cmd; (void)buffer; (void)bufsize;
    tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
    return -1;
}
