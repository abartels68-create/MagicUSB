#pragma once

#include <stddef.h>
#include "esp_err.h"

esp_err_t storage_prepare(char *description, size_t description_size);
