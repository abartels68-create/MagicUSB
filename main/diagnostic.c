#include "diagnostic.h"

#include "esp_attr.h"

#define DIAGNOSTIC_MAGIC 0x4d555342U

RTC_NOINIT_ATTR static uint32_t retained_magic;
RTC_NOINIT_ATTR static uint32_t retained_stage;

void diagnostic_mark(uint32_t stage)
{
    retained_magic = DIAGNOSTIC_MAGIC;
    retained_stage = stage;
}

uint32_t diagnostic_last_stage(void)
{
    return retained_magic == DIAGNOSTIC_MAGIC ? retained_stage : 0;
}

const char *diagnostic_stage_label(uint32_t stage)
{
    switch (stage) {
        case 1: return "PANIC SD PROBE";
        case 2: return "PANIC STORAGE";
        case 3: return "PANIC IMAGE VERIFY";
        case 4: return "PANIC NVS INIT";
        case 10: return "PANIC USB START";
        case 20: return "PANIC WIFI NVS";
        case 21: return "PANIC NETIF INIT";
        case 22: return "PANIC EVENT LOOP";
        case 23: return "PANIC WIFI INIT";
        case 24: return "PANIC WIFI START";
        case 25: return "PANIC WIFI LOAD";
        case 26: return "PANIC WIFI CONFIG";
        case 27: return "PANIC WIFI CONNECT";
        case 28: return "PANIC WIFI WAIT";
        case 39: return "PANIC UPDATE UI";
        case 40: return "PANIC UPDATE NVS";
        case 41: return "PANIC HTTP INIT";
        case 42: return "PANIC MANIFEST GET";
        case 43: return "PANIC MANIFEST JSON";
        case 44: return "PANIC SLOT OPEN";
        case 45: return "PANIC HASH START";
        case 46: return "PANIC IMAGE GET";
        case 47: return "PANIC HASH FINISH";
        case 48: return "PANIC SLOT COMMIT";
        default: return "PANIC BOOT PATH";
    }
}
