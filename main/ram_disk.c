#include "ram_disk.h"

#include <stdio.h>
#include <string.h>

static uint8_t disk[RAM_DISK_BLOCK_SIZE * RAM_DISK_BLOCK_COUNT];

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, (uint16_t)v); put16(p + 2, (uint16_t)(v >> 16)); }

void ram_disk_init(const char *diagnostic)
{
    memset(disk, 0, sizeof(disk));

    /* 64 KiB FAT12 super-floppy: reserved=1, FATs=1, root=16 entries. */
    uint8_t *b = disk;
    b[0] = 0xeb; b[1] = 0x3c; b[2] = 0x90;
    memcpy(b + 3, "MSDOS5.0", 8);
    put16(b + 11, RAM_DISK_BLOCK_SIZE);
    b[13] = 1; put16(b + 14, 1); b[16] = 1; put16(b + 17, 16);
    put16(b + 19, RAM_DISK_BLOCK_COUNT); b[21] = 0xf8; put16(b + 22, 1);
    put16(b + 24, 1); put16(b + 26, 1); put32(b + 28, 0);
    b[36] = 0x80; b[38] = 0x29; put32(b + 39, 0x4d555342);
    memcpy(b + 43, "MAGICUSB   ", 11); memcpy(b + 54, "FAT12   ", 8);
    b[510] = 0x55; b[511] = 0xaa;

    uint8_t *fat = disk + RAM_DISK_BLOCK_SIZE;
    fat[0] = 0xf8; fat[1] = 0xff; fat[2] = 0xff; /* media + clusters 0/1 */
    fat[3] = 0xff; fat[4] = 0x0f;                /* cluster 2 = EOC */

    char message[RAM_DISK_BLOCK_SIZE];
    const int message_length = snprintf(message, sizeof(message),
        "MagicUSB Phase 1\r\n"
        "Read-only USB mass storage is working.\r\n"
        "%s\r\n",
        diagnostic != NULL ? diagnostic : "SD diagnostic unavailable");
    const size_t stored_length = message_length > 0 && message_length < (int)sizeof(message)
        ? (size_t)message_length : sizeof(message) - 1;
    uint8_t *root = disk + 2 * RAM_DISK_BLOCK_SIZE;
    memcpy(root, "README  TXT", 11);
    root[11] = 0x21; /* read-only + archive */
    put16(root + 26, 2);
    put32(root + 28, (uint32_t)stored_length);
    memcpy(disk + 3 * RAM_DISK_BLOCK_SIZE, message, stored_length);
}

const uint8_t *ram_disk_data(void) { return disk; }
size_t ram_disk_size(void) { return sizeof(disk); }
