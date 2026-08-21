#include "fault_injection.h"

#include "sdkconfig.h"

#if CONFIG_MAGICUSB_FAULT_INJECTION
#include "esp_log.h"
#include "esp_system.h"
#include "nvs.h"

static const char *TAG = "fault_test";

bool fault_injection_latched(void)
{
    nvs_handle_t nvs;
    uint8_t latched = 0;
    if (nvs_open("magicusb", NVS_READONLY, &nvs) != ESP_OK) return false;
    nvs_get_u8(nvs, "fault_latch", &latched);
    nvs_close(nvs);
    return latched != 0;
}

void fault_injection_maybe_restart(fault_stage_t stage)
{
    nvs_handle_t nvs;
    uint8_t armed = 0;
    if (nvs_open("magicusb", NVS_READWRITE, &nvs) != ESP_OK) return;
    const bool trigger = nvs_get_u8(nvs, "fault_stage", &armed) == ESP_OK && armed == (uint8_t)stage;
    if (trigger) {
        nvs_erase_key(nvs, "fault_stage");
        nvs_set_u8(nvs, "fault_latch", armed);
        if (nvs_commit(nvs) == ESP_OK) {
            nvs_close(nvs);
            ESP_LOGW(TAG, "triggering one-shot reset at storage stage %u", (unsigned)stage);
            esp_restart();
        }
    }
    nvs_close(nvs);
}

#else

bool fault_injection_latched(void) { return false; }
void fault_injection_maybe_restart(fault_stage_t stage) { (void)stage; }

#endif

