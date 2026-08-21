#include "manifest_verify.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "mbedtls/base64.h"
#include "psa/crypto.h"

#define CANONICAL_LIMIT 768

static bool safe_field(const char *value)
{
    if (value == NULL) return false;
    for (const unsigned char *cursor = (const unsigned char *)value; *cursor != '\0'; ++cursor) {
        if (*cursor < 0x20 || *cursor == 0x7f) return false;
    }
    return true;
}

esp_err_t manifest_verify_ed25519(const uint8_t public_key[32], int schema_version,
                                  const char *release, const char *minimum_firmware,
                                  size_t image_size, const char *sha256,
                                  const char *download_url, const char *site,
                                  const char *device_id, const char *signature_base64)
{
    if (public_key == NULL || !safe_field(release) || !safe_field(minimum_firmware) ||
        !safe_field(sha256) || !safe_field(download_url) || !safe_field(site) ||
        !safe_field(device_id) || !safe_field(signature_base64)) return ESP_ERR_INVALID_ARG;

    char canonical[CANONICAL_LIMIT];
    const int length = snprintf(canonical, sizeof(canonical),
        "schema_version=%d\nrelease=%s\nminimum_firmware=%s\nsize=%u\nsha256=%s\n"
        "download_url=%s\nsite=%s\ndevice_id=%s\n",
        schema_version, release, minimum_firmware, (unsigned)image_size, sha256,
        download_url, site, device_id);
    if (length < 0 || (size_t)length >= sizeof(canonical)) return ESP_ERR_INVALID_SIZE;

    uint8_t signature[64];
    size_t signature_length = 0;
    if (mbedtls_base64_decode(signature, sizeof(signature), &signature_length,
                              (const uint8_t *)signature_base64, strlen(signature_base64)) != 0 ||
        signature_length != sizeof(signature)) return ESP_ERR_INVALID_RESPONSE;

    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_TWISTED_EDWARDS));
    psa_set_key_bits(&attributes, 255);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_VERIFY_MESSAGE);
    psa_set_key_algorithm(&attributes, PSA_ALG_PURE_EDDSA);
    psa_key_id_t key = 0;
    psa_status_t status = psa_import_key(&attributes, public_key, 32, &key);
    psa_reset_key_attributes(&attributes);
    if (status != PSA_SUCCESS) return ESP_ERR_NOT_SUPPORTED;
    status = psa_verify_message(key, PSA_ALG_PURE_EDDSA, (const uint8_t *)canonical,
                                (size_t)length, signature, sizeof(signature));
    psa_destroy_key(key);
    return status == PSA_SUCCESS ? ESP_OK : ESP_ERR_INVALID_CRC;
}
