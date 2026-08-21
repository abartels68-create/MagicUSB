#pragma once

#include <stdint.h>

void diagnostic_mark(uint32_t stage);
uint32_t diagnostic_last_stage(void);
const char *diagnostic_stage_label(uint32_t stage);
