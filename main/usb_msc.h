#pragma once
#include <stddef.h>
#include "esp_err.h"
esp_err_t usb_msc_start(const char *diagnostic);
esp_err_t usb_msc_activate_pending(char *description, size_t description_size);
