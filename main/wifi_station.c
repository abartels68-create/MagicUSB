#include "wifi_station.h"

#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs.h"
#include "diagnostic.h"

#define CONNECTED_BIT BIT0
#define PROFILE_COUNT 2

static const char *TAG = "wifi";
static EventGroupHandle_t events;
static wifi_config_t profiles[PROFILE_COUNT];
static bool profile_valid[PROFILE_COUNT];
static volatile int active_profile = -1;
static volatile bool switching_profile;
static char active_ssid[33];
static portMUX_TYPE status_lock = portMUX_INITIALIZER_UNLOCKED;

static void network_event(void *argument, esp_event_base_t base, int32_t id, void *data)
{
    (void)argument;
    (void)data;
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        portENTER_CRITICAL(&status_lock);
        if (active_profile >= 0) strlcpy(active_ssid, (const char *)profiles[active_profile].sta.ssid, sizeof(active_ssid));
        portEXIT_CRITICAL(&status_lock);
        xEventGroupSetBits(events, CONNECTED_BIT);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(events, CONNECTED_BIT);
        portENTER_CRITICAL(&status_lock);
        active_ssid[0] = '\0';
        portEXIT_CRITICAL(&status_lock);
        if (!switching_profile && active_profile >= 0) esp_wifi_connect();
    }
}

static void priority_task(void *argument)
{
    (void)argument;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(60000));
        if (active_profile <= 0 || !profile_valid[0] ||
            (xEventGroupGetBits(events) & CONNECTED_BIT) == 0) continue;

        wifi_scan_config_t scan = { .show_hidden = false };
        if (esp_wifi_scan_start(&scan, true) != ESP_OK) continue;
        uint16_t count = 12;
        wifi_ap_record_t access_points[12];
        if (esp_wifi_scan_get_ap_records(&count, access_points) != ESP_OK) continue;
        bool preferred_visible = false;
        for (uint16_t i = 0; i < count; ++i) {
            if (strcmp((const char *)access_points[i].ssid, (const char *)profiles[0].sta.ssid) == 0) {
                preferred_visible = true;
                break;
            }
        }
        if (!preferred_visible) continue;

        const int fallback = active_profile;
        switching_profile = true;
        xEventGroupClearBits(events, CONNECTED_BIT);
        esp_wifi_disconnect();
        active_profile = 0;
        esp_wifi_set_config(WIFI_IF_STA, &profiles[0]);
        esp_wifi_connect();
        EventBits_t result = xEventGroupWaitBits(events, CONNECTED_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(8000));
        if ((result & CONNECTED_BIT) == 0) {
            ESP_LOGW(TAG, "preferred profile unavailable after scan; restoring fallback");
            esp_wifi_disconnect();
            active_profile = fallback;
            esp_wifi_set_config(WIFI_IF_STA, &profiles[fallback]);
            esp_wifi_connect();
        } else {
            ESP_LOGI(TAG, "promoted to preferred profile");
        }
        switching_profile = false;
    }
}

static esp_err_t load_string(nvs_handle_t nvs, const char *key, char *value, size_t capacity)
{
    size_t length = capacity;
    const esp_err_t err = nvs_get_str(nvs, key, value, &length);
    if (err == ESP_OK && length > 0) value[capacity - 1] = '\0';
    return err;
}

esp_err_t wifi_station_connect(char *connected_ssid, size_t connected_ssid_size)
{
    if (connected_ssid == NULL || connected_ssid_size == 0) return ESP_ERR_INVALID_ARG;
    connected_ssid[0] = '\0';

    nvs_handle_t provision;
    diagnostic_mark(20);
    if (nvs_open("magicusb", NVS_READONLY, &provision) != ESP_OK) return ESP_ERR_NOT_FOUND;

    diagnostic_mark(21);
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init");
    diagnostic_mark(22);
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop");
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    diagnostic_mark(23);
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "Wi-Fi init");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "station mode");
    events = xEventGroupCreate();
    if (events == NULL) return ESP_ERR_NO_MEM;
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, network_event, NULL), TAG, "IP handler");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, network_event, NULL), TAG, "Wi-Fi handler");
    diagnostic_mark(24);
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "Wi-Fi start");

    diagnostic_mark(25);
    for (unsigned index = 0; index < PROFILE_COUNT; ++index) {
        char ssid_key[8];
        char pass_key[8];
        snprintf(ssid_key, sizeof(ssid_key), "ssid%u", index);
        snprintf(pass_key, sizeof(pass_key), "pass%u", index);
        if (load_string(provision, ssid_key, (char *)profiles[index].sta.ssid, sizeof(profiles[index].sta.ssid)) != ESP_OK ||
            load_string(provision, pass_key, (char *)profiles[index].sta.password, sizeof(profiles[index].sta.password)) != ESP_OK) continue;
        profiles[index].sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        profiles[index].sta.pmf_cfg.capable = true;
        profiles[index].sta.pmf_cfg.required = false;
        profile_valid[index] = true;
    }
    nvs_close(provision);

    for (unsigned index = 0; index < PROFILE_COUNT; ++index) {
        if (!profile_valid[index]) continue;
        ESP_LOGI(TAG, "trying provisioned profile %u (%s)", index + 1, profiles[index].sta.ssid);
        xEventGroupClearBits(events, CONNECTED_BIT);
        switching_profile = true;
        active_profile = (int)index;
        diagnostic_mark(26);
        ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &profiles[index]), TAG, "set profile");
        esp_wifi_disconnect();
        diagnostic_mark(27);
        ESP_RETURN_ON_ERROR(esp_wifi_connect(), TAG, "connect");
        diagnostic_mark(28);
        const EventBits_t result = xEventGroupWaitBits(events, CONNECTED_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(8000));
        if ((result & CONNECTED_BIT) != 0) {
            switching_profile = false;
            strlcpy(connected_ssid, (const char *)profiles[index].sta.ssid, connected_ssid_size);
            xTaskCreate(priority_task, "wifi_priority", 4096, NULL, 3, NULL);
            ESP_LOGI(TAG, "connected to %s", connected_ssid);
            return ESP_OK;
        }
        ESP_LOGW(TAG, "profile %u did not connect", index + 1);
    }
    switching_profile = false;
    return ESP_ERR_TIMEOUT;
}

bool wifi_station_get_connected_ssid(char *ssid, size_t ssid_size)
{
    if (ssid == NULL || ssid_size == 0) return false;
    portENTER_CRITICAL(&status_lock);
    strlcpy(ssid, active_ssid, ssid_size);
    portEXIT_CRITICAL(&status_lock);
    return ssid[0] != '\0';
}

int wifi_station_active_profile(void)
{
    return (xEventGroupGetBits(events) & CONNECTED_BIT) != 0 ? active_profile : -1;
}
