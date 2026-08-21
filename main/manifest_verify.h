#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t manifest_verify_ed25519(const uint8_t public_key[32], int schema_version,
                                  const char *release, const char *minimum_firmware,
                                  size_t image_size, const char *sha256,
                                  const char *download_url, const char *site,
                                  const char *device_id, const char *signature_base64);
