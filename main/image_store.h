#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t image_store_prepare(const uint8_t *seed_image, size_t seed_size,
                              char *description, size_t description_size);
esp_err_t image_store_register_pending(unsigned slot, size_t size, const uint8_t sha256[32], const char *release);
bool image_store_pending(void);
unsigned image_store_pending_slot(void);
unsigned image_store_active_slot(void);
bool image_store_active_matches(size_t size, const uint8_t sha256[32]);
const char *image_store_active_release(void);
esp_err_t image_store_activate_pending(char *description, size_t description_size);
bool image_store_ready(void);
size_t image_store_size(void);
esp_err_t image_store_read(size_t offset, void *buffer, size_t length);
