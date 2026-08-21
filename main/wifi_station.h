#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
esp_err_t wifi_station_connect(char *connected_ssid, size_t connected_ssid_size);
bool wifi_station_get_connected_ssid(char *ssid, size_t ssid_size);
int wifi_station_active_profile(void);
