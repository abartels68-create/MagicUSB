#pragma once

#include <stdbool.h>

bool release_version_valid(const char *release);
int release_version_compare(const char *left, const char *right);
