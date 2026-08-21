#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "tdongle_s3.h"
#include "diagnostic.h"
#include "storage.h"
#include "image_store.h"
#include "ram_disk.h"
#include "usb_msc.h"
#include "update_client.h"
#include "wifi_station.h"

static const char *TAG = "magicusb";
static bool network_connected;
static volatile bool activation_running;
static char network_line[24] = "WIFI OFFLINE";

typedef struct {
    TaskHandle_t caller;
    esp_err_t result;
    char description[96];
} update_work_t;

static void update_task(void *argument)
{
    update_work_t *work = argument;
    diagnostic_mark(40);
    work->result = update_client_stage(work->description, sizeof(work->description));
    xTaskNotifyGive(work->caller);
    vTaskDelete(NULL);
}

static void activation_task(void *argument)
{
    (void)argument;
    char description[96];
    char slot_line[24];
    const unsigned slot = image_store_pending_slot();
    snprintf(slot_line, sizeof(slot_line), "ACTIVATING %c", slot == 0 ? 'A' : 'B');
    board_set_status(BOARD_YELLOW);
    board_display_status("MAGICUSB", slot_line, "USB REFRESH");
    const esp_err_t result = usb_msc_activate_pending(description, sizeof(description));
    if (result == ESP_OK) {
        ESP_LOGI(TAG, "%s", description);
        snprintf(slot_line, sizeof(slot_line), "IMAGE %c ACTIVE", slot == 0 ? 'A' : 'B');
        board_set_status(BOARD_GREEN);
        board_display_status("MAGICUSB", slot_line, "USB REFRESHED");
    } else {
        ESP_LOGE(TAG, "slot B activation failed: %s", esp_err_to_name(result));
        board_set_status(BOARD_RED);
        board_display_status("MAGICUSB", "ACTIVATE ERROR", "IMAGE A SAFE");
    }
    vTaskDelay(pdMS_TO_TICKS(2000));
    board_set_status(network_connected ? BOARD_GREEN : BOARD_ORANGE);
    board_display_status("MAGICUSB", "SD READY", network_line);
    activation_running = false;
    vTaskDelete(NULL);
}

static const char *reset_reason_label(esp_reset_reason_t reason)
{
    switch (reason) {
        case ESP_RST_BROWNOUT: return "RESET BROWNOUT";
        case ESP_RST_TASK_WDT: return "RESET TASK WDT";
        case ESP_RST_INT_WDT: return "RESET INT WDT";
        case ESP_RST_PANIC: return "RESET PANIC";
        case ESP_RST_SW: return "RESET SOFTWARE";
        case ESP_RST_WDT: return "RESET WATCHDOG";
        default: return "RESET OTHER";
    }
}

static void button_task(void *argument)
{
    (void)argument;
    bool was_pressed = false;
    char last_ssid[33] = "";
    while (true) {
        const bool pressed = board_button_pressed();
        if (pressed && !was_pressed) {
            if (image_store_pending() && !activation_running) {
                activation_running = true;
                if (xTaskCreate(activation_task, "activate", 8192, NULL, 4, NULL) != pdPASS) {
                    activation_running = false;
                    board_set_status(BOARD_RED);
                    board_display_status("MAGICUSB", "ACTIVATE ERROR", "NO MEMORY");
                }
            } else if (!activation_running) {
                board_set_status(BOARD_PURPLE);
                board_display_status("MAGICUSB", "BUTTON OK", "USB READ ONLY");
            }
        } else if (!pressed && was_pressed && !activation_running) {
            vTaskDelay(pdMS_TO_TICKS(500));
            board_set_status(network_connected ? BOARD_GREEN : BOARD_ORANGE);
            board_display_status("MAGICUSB", "SD READY", network_line);
        }
        if (!pressed && !activation_running) {
            char current_ssid[33];
            const bool connected = wifi_station_get_connected_ssid(current_ssid, sizeof(current_ssid));
            if (connected != network_connected || strcmp(current_ssid, last_ssid) != 0) {
                network_connected = connected;
                strlcpy(last_ssid, current_ssid, sizeof(last_ssid));
                if (connected) {
                    strlcpy(network_line, wifi_station_active_profile() == 0 ? "WIFI A" : "WIFI B",
                            sizeof(network_line));
                    board_set_status(BOARD_GREEN);
                } else {
                    strlcpy(network_line, "WIFI CONNECT", sizeof(network_line));
                    board_set_status(BOARD_ORANGE);
                }
                board_display_status("MAGICUSB", image_store_pending() ? "UPDATE READY" : "SD READY",
                                     image_store_pending() ? "PRESS BUTTON" : network_line);
            }
        }
        was_pressed = pressed;
        vTaskDelay(pdMS_TO_TICKS(25));
    }
}

void app_main(void)
{
    char sd_diagnostic[96];
    char storage_diagnostic[96];
    char image_diagnostic[96];
    ESP_ERROR_CHECK(board_init());
    board_set_status(BOARD_BLUE);
    const esp_reset_reason_t reset_reason = esp_reset_reason();
    if (reset_reason != ESP_RST_POWERON) {
        const char *reason = reset_reason == ESP_RST_PANIC
            ? diagnostic_stage_label(diagnostic_last_stage()) : reset_reason_label(reset_reason);
        ESP_ERROR_CHECK(board_display_status("MAGICUSB", reason, "DIAGNOSTIC"));
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    ESP_ERROR_CHECK(board_display_status("MAGICUSB", "BOOTING", "SD CHECK"));

    diagnostic_mark(1);
    const esp_err_t sd_status = board_sd_probe(sd_diagnostic, sizeof(sd_diagnostic));
    if (sd_status != ESP_OK) {
        board_set_status(BOARD_RED);
        ESP_ERROR_CHECK(board_display_status("MAGICUSB", "SD ERROR", "USB CACHED"));
    } else {
        diagnostic_mark(2);
        const esp_err_t storage_status = storage_prepare(storage_diagnostic, sizeof(storage_diagnostic));
        if (storage_status != ESP_OK) {
            ESP_LOGW(TAG, "SD management storage unavailable: %s", storage_diagnostic);
            board_set_status(BOARD_RED);
            ESP_ERROR_CHECK(board_display_status("MAGICUSB", "SD STORAGE ERR", "USB CACHED"));
        } else {
            ESP_LOGI(TAG, "%s", storage_diagnostic);
            ram_disk_init(sd_diagnostic);
            diagnostic_mark(3);
            const esp_err_t image_status = image_store_prepare(ram_disk_data(), ram_disk_size(),
                                                                image_diagnostic, sizeof(image_diagnostic));
            if (image_status == ESP_OK) {
                ESP_LOGI(TAG, "%s", image_diagnostic);
                ESP_ERROR_CHECK(board_display_status("MAGICUSB", "IMAGE A READY", "USB START"));
            } else {
                ESP_LOGW(TAG, "SD image unavailable; using RAM fallback: %s", esp_err_to_name(image_status));
                ESP_ERROR_CHECK(board_display_status("MAGICUSB", "IMAGE FALLBACK", "USB START"));
            }
        }
    }

    diagnostic_mark(4);
    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs);

    /* Offline operation comes first: enumerate USB before network work. */
    diagnostic_mark(10);
    ESP_ERROR_CHECK(usb_msc_start(sd_diagnostic));
    board_set_status(BOARD_ORANGE);

    char connected_ssid[33];
    ESP_ERROR_CHECK(board_display_status("MAGICUSB", "USB READY", "WIFI CONNECT"));
    diagnostic_mark(20);
    const esp_err_t wifi = wifi_station_connect(connected_ssid, sizeof(connected_ssid));
    if (wifi == ESP_OK) {
        network_connected = true;
        strlcpy(network_line, wifi_station_active_profile() == 0 ? "WIFI A" : "WIFI B",
                sizeof(network_line));
        ESP_LOGI(TAG, "cached USB content ready; connected to %s", connected_ssid);
        board_set_status(BOARD_GREEN);
        board_set_status(BOARD_PURPLE);
        diagnostic_mark(39);
        ESP_ERROR_CHECK(board_display_status("MAGICUSB", "CHECK UPDATE", "USB READY"));
        update_work_t update_work = {
            .caller = xTaskGetCurrentTaskHandle(),
            .result = ESP_FAIL,
        };
        esp_err_t update = ESP_ERR_NO_MEM;
        if (xTaskCreate(update_task, "update", 12288, &update_work, 3, NULL) == pdPASS) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            update = update_work.result;
        }
        if (update == ESP_OK) {
            ESP_LOGI(TAG, "%s", update_work.description);
            board_set_status(BOARD_YELLOW);
            char slot_line[24];
            snprintf(slot_line, sizeof(slot_line), "SLOT %c SAFE",
                     image_store_pending_slot() == 0 ? 'A' : 'B');
            ESP_ERROR_CHECK(board_display_status("MAGICUSB", "UPDATE READY", slot_line));
            vTaskDelay(pdMS_TO_TICKS(1500));
        } else {
            ESP_LOGW(TAG, "no staged update: %s", esp_err_to_name(update));
        }
        board_set_status(BOARD_GREEN);
    } else {
        ESP_LOGW(TAG, "Wi-Fi unavailable; cached USB content remains ready: %s", esp_err_to_name(wifi));
        board_set_status(BOARD_ORANGE);
    }
    ESP_ERROR_CHECK(board_display_status("MAGICUSB", image_store_pending() ? "UPDATE READY" : "SD READY",
                                         image_store_pending() ? "PRESS BUTTON" : network_line));
    xTaskCreate(button_task, "button", 2048, NULL, 2, NULL);
}
