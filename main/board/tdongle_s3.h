#pragma once
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum { BOARD_BLUE, BOARD_PURPLE, BOARD_YELLOW, BOARD_GREEN, BOARD_ORANGE, BOARD_RED } board_color_t;
esp_err_t board_init(void);
void board_set_status(board_color_t color);
esp_err_t board_sd_probe(char *description, size_t description_size);
esp_err_t board_display_status(const char *heading, const char *line1, const char *line2);
bool board_button_pressed(void);
