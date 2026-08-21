#include "manifest_policy.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static bool version_component(const char **cursor, uint32_t *value, bool final)
{
    uint32_t result = 0;
    unsigned digits = 0;
    while (**cursor >= '0' && **cursor <= '9') {
        if (result > (UINT32_MAX - 9U) / 10U) return false;
        result = result * 10U + (uint32_t)(**cursor - '0');
        ++*cursor;
        ++digits;
    }
    if (digits == 0 || (final ? **cursor != '\0' : **cursor != '.')) return false;
    if (!final) ++*cursor;
    *value = result;
    return true;
}

static bool parse_version(const char *version, uint32_t values[3])
{
    if (version == NULL) return false;
    const char *cursor = version;
    return version_component(&cursor, &values[0], false) &&
           version_component(&cursor, &values[1], false) &&
           version_component(&cursor, &values[2], true);
}

bool firmware_version_valid(const char *version)
{
    uint32_t values[3];
    return parse_version(version, values);
}

int firmware_version_compare(const char *left, const char *right)
{
    uint32_t a[3], b[3];
    if (!parse_version(left, a) || !parse_version(right, b)) return 0;
    for (unsigned i = 0; i < 3; ++i) {
        if (a[i] < b[i]) return -1;
        if (a[i] > b[i]) return 1;
    }
    return 0;
}

bool manifest_scope_valid(const char *site, const char *device_id)
{
    if (site == NULL || device_id == NULL || strlen(site) > 32 || strlen(device_id) > 64) return false;
    for (const char *cursor = site; *cursor != '\0'; ++cursor) {
        if (!((*cursor >= 'A' && *cursor <= 'Z') || (*cursor >= '0' && *cursor <= '9') ||
              *cursor == '_' || *cursor == '-')) return false;
    }
    for (const char *cursor = device_id; *cursor != '\0'; ++cursor) {
        if ((unsigned char)*cursor < 0x21 || (unsigned char)*cursor > 0x7e) return false;
    }
    return true;
}

bool manifest_scope_matches(const char *manifest_site, const char *manifest_device_id,
                            const char *provisioned_site, const char *provisioned_device_id)
{
    if (!manifest_scope_valid(manifest_site, manifest_device_id) ||
        provisioned_site == NULL || provisioned_device_id == NULL) return false;
    if (manifest_site[0] != '\0' && strcmp(manifest_site, provisioned_site) != 0) return false;
    if (manifest_device_id[0] != '\0' && strcmp(manifest_device_id, provisioned_device_id) != 0) return false;
    return true;
}

