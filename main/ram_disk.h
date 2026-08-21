#pragma once

#include <stddef.h>
#include <stdint.h>

#define RAM_DISK_BLOCK_SIZE 512u
#define RAM_DISK_BLOCK_COUNT 128u

void ram_disk_init(const char *diagnostic);
const uint8_t *ram_disk_data(void);
size_t ram_disk_size(void);
