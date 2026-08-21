#include "release_version.h"

#include <stddef.h>
#include <stdint.h>

static bool component(const char **cursor, unsigned digits, uint32_t *value, bool final)
{
    uint32_t result = 0;
    unsigned count = 0;
    while (**cursor >= '0' && **cursor <= '9') {
        if (count == digits || result > (UINT32_MAX - 9U) / 10U) return false;
        result = result * 10U + (uint32_t)(**cursor - '0');
        ++*cursor;
        ++count;
    }
    if (count == 0 || (!final && count != digits) || (final && **cursor != '\0')) return false;
    if (!final) ++*cursor;
    *value = result;
    return true;
}

static bool parse(const char *release, uint32_t values[4])
{
    if (release == NULL) return false;
    const char *cursor = release;
    if (!component(&cursor, 4, &values[0], false) || cursor[-1] != '.' ||
        !component(&cursor, 2, &values[1], false) || cursor[-1] != '.' ||
        !component(&cursor, 2, &values[2], false) || cursor[-1] != '.' ||
        !component(&cursor, 10, &values[3], true)) return false;
    return values[1] >= 1 && values[1] <= 12 && values[2] >= 1 && values[2] <= 31;
}

bool release_version_valid(const char *release)
{
    uint32_t values[4];
    return parse(release, values);
}

int release_version_compare(const char *left, const char *right)
{
    uint32_t a[4], b[4];
    if (!parse(left, a) || !parse(right, b)) return 0;
    for (unsigned i = 0; i < 4; ++i) {
        if (a[i] < b[i]) return -1;
        if (a[i] > b[i]) return 1;
    }
    return 0;
}
