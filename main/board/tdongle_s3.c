#include "tdongle_s3.h"

#include <stdio.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/sdmmc_default_configs.h"
#include "driver/sdmmc_host.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

/* Verified against LILYGO's T-Dongle-S3 documentation. */
#define APA102_DATA_GPIO  40
#define APA102_CLOCK_GPIO 39
#define TFT_BACKLIGHT_GPIO 38
#define TFT_CS_GPIO         4
#define TFT_MOSI_GPIO       3
#define TFT_CLOCK_GPIO      5
#define TFT_DC_GPIO         2
#define TFT_RESET_GPIO      1
#define BUTTON_GPIO         0
#define SD_CLK_GPIO        12
#define SD_CMD_GPIO        16
#define SD_D0_GPIO         14
#define SD_D1_GPIO         17
#define SD_D2_GPIO         21
#define SD_D3_GPIO         18

static const char *TAG = "tdongle_s3";
static sdmmc_card_t *card;
static esp_lcd_panel_io_handle_t panel_io;
static uint16_t framebuffer[160 * 80];

static const uint8_t font5x7[36][5] = {
    {0x3e,0x51,0x49,0x45,0x3e},{0x00,0x42,0x7f,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},
    {0x21,0x41,0x45,0x4b,0x31},{0x18,0x14,0x12,0x7f,0x10},{0x27,0x45,0x45,0x45,0x39},
    {0x3c,0x4a,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},{0x36,0x49,0x49,0x49,0x36},
    {0x06,0x49,0x49,0x29,0x1e},{0x7e,0x11,0x11,0x11,0x7e},{0x7f,0x49,0x49,0x49,0x36},
    {0x3e,0x41,0x41,0x41,0x22},{0x7f,0x41,0x41,0x22,0x1c},{0x7f,0x49,0x49,0x49,0x41},
    {0x7f,0x09,0x09,0x09,0x01},{0x3e,0x41,0x49,0x49,0x7a},{0x7f,0x08,0x08,0x08,0x7f},
    {0x00,0x41,0x7f,0x41,0x00},{0x20,0x40,0x41,0x3f,0x01},{0x7f,0x08,0x14,0x22,0x41},
    {0x7f,0x40,0x40,0x40,0x40},{0x7f,0x02,0x0c,0x02,0x7f},{0x7f,0x04,0x08,0x10,0x7f},
    {0x3e,0x41,0x41,0x41,0x3e},{0x7f,0x09,0x09,0x09,0x06},{0x3e,0x41,0x51,0x21,0x5e},
    {0x7f,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7f,0x01,0x01},
    {0x3f,0x40,0x40,0x40,0x3f},{0x1f,0x20,0x40,0x20,0x1f},{0x3f,0x40,0x38,0x40,0x3f},
    {0x63,0x14,0x08,0x14,0x63},{0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43}
};

static uint16_t wire_color(uint16_t color) { return (uint16_t)((color << 8) | (color >> 8)); }

static void draw_text(int x, int y, const char *text, uint16_t color, int scale)
{
    if (text == NULL) return;
    color = wire_color(color);
    while (*text != '\0') {
        const char ch = *text++;
        const uint8_t *glyph = NULL;
        if (ch >= '0' && ch <= '9') glyph = font5x7[ch - '0'];
        else if (ch >= 'A' && ch <= 'Z') glyph = font5x7[10 + ch - 'A'];
        if (glyph != NULL) {
            for (int col = 0; col < 5; ++col) for (int row = 0; row < 7; ++row) {
                if ((glyph[col] & (1u << row)) == 0) continue;
                for (int dx = 0; dx < scale; ++dx) for (int dy = 0; dy < scale; ++dy) {
                    const int px = x + col * scale + dx;
                    const int py = y + row * scale + dy;
                    if (px >= 0 && px < 160 && py >= 0 && py < 80) framebuffer[py * 160 + px] = color;
                }
            }
        }
        x += 6 * scale;
    }
}

static esp_err_t display_init(void)
{
    const spi_bus_config_t bus = {
        .mosi_io_num = TFT_MOSI_GPIO, .miso_io_num = -1, .sclk_io_num = TFT_CLOCK_GPIO,
        .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = sizeof(framebuffer),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "TFT SPI init");
    const esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = TFT_CS_GPIO, .dc_gpio_num = TFT_DC_GPIO, .spi_mode = 0,
        .pclk_hz = 40 * 1000 * 1000, .trans_queue_depth = 1,
        .lcd_cmd_bits = 8, .lcd_param_bits = 8,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &panel_io), TAG, "TFT IO");
    gpio_set_direction(TFT_RESET_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(TFT_RESET_GPIO, 0);
    esp_rom_delay_us(10000);
    gpio_set_level(TFT_RESET_GPIO, 1);
    esp_rom_delay_us(120000);
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(panel_io, 0x11, NULL, 0), TAG, "TFT sleep out");
    esp_rom_delay_us(120000);
    const struct { uint8_t cmd; uint8_t len; uint8_t data[16]; } init[] = {
        {0xb1,3,{0x05,0x3a,0x3a}}, {0xb2,3,{0x05,0x3a,0x3a}},
        {0xb3,6,{0x05,0x3a,0x3a,0x05,0x3a,0x3a}}, {0xb4,1,{0x03}},
        {0xc0,3,{0x62,0x02,0x04}}, {0xc1,1,{0xc0}}, {0xc2,2,{0x0d,0x00}},
        {0xc3,2,{0x8d,0x6a}}, {0xc4,2,{0x8d,0xee}}, {0xc5,1,{0x0e}},
        {0x36,1,{0xa8}}, {0x3a,1,{0x05}},
        {0xe0,16,{0x10,0x0e,0x02,0x03,0x0e,0x07,0x02,0x07,0x0a,0x12,0x27,0x37,0x00,0x0d,0x0e,0x10}},
        {0xe1,16,{0x10,0x0e,0x03,0x03,0x0f,0x06,0x02,0x08,0x0a,0x13,0x26,0x36,0x00,0x0d,0x0e,0x10}},
    };
    for (size_t i = 0; i < sizeof(init) / sizeof(init[0]); ++i) {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(panel_io, init[i].cmd, init[i].data, init[i].len), TAG, "TFT command");
    }
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(panel_io, 0x21, NULL, 0), TAG, "TFT invert");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(panel_io, 0x13, NULL, 0), TAG, "TFT normal");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(panel_io, 0x29, NULL, 0), TAG, "TFT display on");
    gpio_set_level(TFT_BACKLIGHT_GPIO, 0);
    return ESP_OK;
}

static void pulse(int gpio)
{
    gpio_set_level(gpio, 1);
    esp_rom_delay_us(1);
    gpio_set_level(gpio, 0);
    esp_rom_delay_us(1);
}

static void send_byte(uint8_t value)
{
    for (int bit = 7; bit >= 0; --bit) {
        gpio_set_level(APA102_DATA_GPIO, (value >> bit) & 1u);
        pulse(APA102_CLOCK_GPIO);
    }
}

esp_err_t board_init(void)
{
    const gpio_config_t output = {
        .pin_bit_mask = (1ULL << APA102_DATA_GPIO) | (1ULL << APA102_CLOCK_GPIO) |
                        (1ULL << TFT_BACKLIGHT_GPIO),
        .mode = GPIO_MODE_OUTPUT,
    };
    esp_err_t err = gpio_config(&output);
    gpio_set_level(TFT_BACKLIGHT_GPIO, 1);
    if (err != ESP_OK) return err;
    const gpio_config_t button = {
        .pin_bit_mask = 1ULL << BUTTON_GPIO, .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&button), TAG, "button init");
    return display_init();
}

esp_err_t board_display_status(const char *heading, const char *line1, const char *line2)
{
    if (panel_io == NULL) return ESP_ERR_INVALID_STATE;
    for (size_t i = 0; i < 160 * 80; ++i) framebuffer[i] = 0;
    for (int y = 0; y < 3; ++y) for (int x = 0; x < 160; ++x) framebuffer[y * 160 + x] = wire_color(0x04ff);
    draw_text(7, 9, heading, 0xffff, 2);
    draw_text(7, 36, line1, 0x07e0, 1);
    draw_text(7, 55, line2, 0xffe0, 1);
    const uint8_t columns[] = {0x00, 0x01, 0x00, 0xa0};
    const uint8_t rows[] = {0x00, 0x1a, 0x00, 0x69};
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(panel_io, 0x2a, columns, sizeof(columns)), TAG, "TFT columns");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(panel_io, 0x2b, rows, sizeof(rows)), TAG, "TFT rows");
    return esp_lcd_panel_io_tx_color(panel_io, 0x2c, framebuffer, sizeof(framebuffer));
}

bool board_button_pressed(void) { return gpio_get_level(BUTTON_GPIO) == 0; }

esp_err_t board_sd_probe(char *description, size_t description_size)
{
    if (description == NULL || description_size == 0) return ESP_ERR_INVALID_ARG;

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4;
    slot.clk = SD_CLK_GPIO;
    slot.cmd = SD_CMD_GPIO;
    slot.d0 = SD_D0_GPIO;
    slot.d1 = SD_D1_GPIO;
    slot.d2 = SD_D2_GPIO;
    slot.d3 = SD_D3_GPIO;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    const esp_vfs_fat_sdmmc_mount_config_t mount = {
        .format_if_mount_failed = false,
        .max_files = 6,
        .allocation_unit_size = 0,
    };
    esp_err_t err = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot, &mount, &card);
    if (err != ESP_OK) {
        snprintf(description, description_size, "SD card: NOT DETECTED (%s)", esp_err_to_name(err));
        ESP_LOGW(TAG, "%s", description);
        return err;
    }

    const uint64_t bytes = (uint64_t)card->csd.capacity * card->csd.sector_size;
    snprintf(description, description_size, "SD card: detected, %llu MiB, ID %.5s",
             (unsigned long long)(bytes / (1024ULL * 1024ULL)), card->cid.name);
    ESP_LOGI(TAG, "%s", description);
    return ESP_OK;
}

void board_set_status(board_color_t color)
{
    static const uint8_t rgb[][3] = {
        {0, 0, 255}, {160, 0, 255}, {255, 180, 0},
        {0, 255, 0}, {255, 70, 0}, {255, 0, 0},
    };
    for (int i = 0; i < 4; ++i) send_byte(0x00);
    send_byte(0xe8); /* low brightness */
    send_byte(rgb[color][2]); send_byte(rgb[color][1]); send_byte(rgb[color][0]);
    for (int i = 0; i < 4; ++i) send_byte(0xff);
}
