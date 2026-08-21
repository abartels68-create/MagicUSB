#include <assert.h>

#include "manifest_policy.h"

int main(void)
{
    assert(firmware_version_valid("0.1.0"));
    assert(firmware_version_valid("12.345.6789"));
    assert(!firmware_version_valid("0.1"));
    assert(!firmware_version_valid("v0.1.0"));
    assert(!firmware_version_valid("4294967296.0.0"));
    assert(firmware_version_compare("0.1.0", "0.1.0") == 0);
    assert(firmware_version_compare("0.1.1", "0.1.0") > 0);
    assert(firmware_version_compare("0.0.9", "0.1.0") < 0);

    assert(manifest_scope_valid("", ""));
    assert(manifest_scope_valid("LAB_1", "POS-001"));
    assert(!manifest_scope_valid("lab", "POS-001"));
    assert(!manifest_scope_valid("LAB 1", "POS-001"));
    assert(!manifest_scope_valid("LAB", "bad device"));

    assert(manifest_scope_matches("", "", "LAB", "POS-001"));
    assert(manifest_scope_matches("LAB", "", "LAB", "POS-001"));
    assert(manifest_scope_matches("LAB", "POS-001", "LAB", "POS-001"));
    assert(!manifest_scope_matches("OTHER", "", "LAB", "POS-001"));
    assert(!manifest_scope_matches("LAB", "POS-002", "LAB", "POS-001"));
    assert(!manifest_scope_matches("LAB", "POS-001", "", ""));
    return 0;
}
