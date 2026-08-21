#pragma once

#include <stdbool.h>

typedef enum {
    FAULT_STAGE_DOWNLOAD_CHUNK = 1,
    FAULT_STAGE_DOWNLOAD_VERIFIED = 2,
    FAULT_STAGE_INACTIVE_REPLACED = 3,
    FAULT_STAGE_METADATA_COMMITTED = 4,
} fault_stage_t;

bool fault_injection_latched(void);
void fault_injection_maybe_restart(fault_stage_t stage);

