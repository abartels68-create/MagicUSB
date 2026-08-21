#pragma once

#include <stdbool.h>

bool firmware_version_valid(const char *version);
int firmware_version_compare(const char *left, const char *right);
bool manifest_scope_valid(const char *site, const char *device_id);
bool manifest_scope_matches(const char *manifest_site, const char *manifest_device_id,
                            const char *provisioned_site, const char *provisioned_device_id);

