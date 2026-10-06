#include "display.h"
#include "font5x8.h"

#include <stdio.h>
#include <string.h>
#include "hardware/i2c.h"
#include "pico/stdlib.h"

static uint8_t frame_buffer[1024];

static const char *mode_name(uint8_t mode) {
    switch (mode) {
        case KEYER_MODE_IAMBIC_B: return "IAMBIC-B";
        case KEYER_MODE_IAMBIC_A: return "IAMBIC-A";
        case KEYER_MODE_ULTIMATE: return "ULTIMATIC";
        case KEYER_MODE_BUG:      return "BUG";
        case KEYER_MODE_STRAIGHT: return "STRAIGHT";
        default:                  return "UNKNOWN";
    }
}

static void send_cmd(uint8_t cmd) {
    uint8_t buf[2] = {0x00, cmd};
    i2c_write_blocking(I2C_PORT, SSD1306_ADDR, buf, 2, false);
}

void display_init(void) {
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(PIN_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(PIN_I2C_SDA);
    gpio_pull_up(PIN_I2C_SCL);

    uint8_t init_cmds[] = {
        0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
        0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x12,
        0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF
    };
    for (size_t i = 0; i < sizeof(init_cmds); i++) {
        send_cmd(init_cmds[i]);
    }
    memset(frame_buffer, 0, sizeof(frame_buffer));
}

static void flush_buffer(void) {
    for (uint8_t page = 0; page < 8; page++) {
        send_cmd(0xB0 + page);
        send_cmd(0x00);
        send_cmd(0x10);
        uint8_t buf[129];
        buf[0] = 0x40;
        memcpy(&buf[1], &frame_buffer[page * 128], 128);
        i2c_write_blocking(I2C_PORT, SSD1306_ADDR, buf, 129, false);
    }
}

void display_render_text(const char *str, uint8_t x, uint8_t y) {
    uint8_t page = y / 8;
    if (page > 7) {
        return;
    }
    while (*str && x < 128) {
        unsigned char c = (unsigned char)*str++;
        if (c < 32 || c > 126) {
            c = '?';
        }
        const uint8_t *glyph = font5x8[c - 32];
        for (int col = 0; col < 5 && x < 128; col++, x++) {
            frame_buffer[page * 128 + x] = glyph[col];
        }
        if (x < 128) {
            frame_buffer[page * 128 + x] = 0x00;
            x++;
        }
    }
}

#define DISPLAY_W        128
#define DISPLAY_H        64
#define MODE_LINE_Y      56   /* bottom text line (one 8-px page) */
#define GLYPH_W          5
#define GLYPH_H          7    /* font5x8 glyphs use 7 rows */
#define NUMBER_MARGIN    1    /* blank rows kept above/below the number */

static void set_pixel(int x, int y) {
    if (x < 0 || x >= DISPLAY_W || y < 0 || y >= DISPLAY_H) {
        return;
    }
    frame_buffer[(y / 8) * DISPLAY_W + x] |= (uint8_t)(1u << (y % 8));
}

/* Draw str scaled up by the largest integer factor that fits in the
 * area above the mode line, centered horizontally and vertically. */
static void render_number_centered(const char *str, int area_h) {
    int n = (int)strlen(str);
    if (n == 0) {
        return;
    }
    int scale_h = (area_h - 2 * NUMBER_MARGIN) / GLYPH_H;
    int scale_w = DISPLAY_W / ((GLYPH_W + 1) * n - 1);
    int scale = scale_h < scale_w ? scale_h : scale_w;
    if (scale < 1) {
        scale = 1;
    }
    int total_w = scale * ((GLYPH_W + 1) * n - 1);
    int x0 = (DISPLAY_W - total_w) / 2;
    int y0 = (area_h - GLYPH_H * scale) / 2;

    for (int i = 0; i < n; i++) {
        unsigned char c = (unsigned char)str[i];
        if (c < 32 || c > 126) {
            c = '?';
        }
        const uint8_t *glyph = font5x8[c - 32];
        for (int col = 0; col < GLYPH_W; col++) {
            for (int row = 0; row < GLYPH_H; row++) {
                if (!((glyph[col] >> row) & 1)) {
                    continue;
                }
                int px = x0 + (i * (GLYPH_W + 1) + col) * scale;
                int py = y0 + row * scale;
                for (int dy = 0; dy < scale; dy++) {
                    for (int dx = 0; dx < scale; dx++) {
                        set_pixel(px + dx, py + dy);
                    }
                }
            }
        }
    }
}

void display_update_ui(bool menu_active, uint8_t menu_item) {
    memset(frame_buffer, 0x00, sizeof(frame_buffer));
    char line[32];

    if (!menu_active) {
        snprintf(line, sizeof(line), "%d", sys_config.wpm);
        render_number_centered(line, MODE_LINE_Y);
        display_render_text(mode_name(sys_config.mode), 0, MODE_LINE_Y);
    } else {
        display_render_text("SETUP MENU", 0, 0);
        switch (menu_item) {
            case 0:
                snprintf(line, sizeof(line), "MODE: %s", mode_name(sys_config.mode));
                break;
            case 1:
                snprintf(line, sizeof(line), "SIDE FREQ: %dHz", sys_config.sidetone_freq);
                break;
            case 2:
                snprintf(line, sizeof(line), "SIDE EN: %s", sys_config.sidetone_en ? "ON" : "OFF");
                break;
            case 3:
                snprintf(line, sizeof(line), "WEIGHT: %d%%", sys_config.weighting);
                break;
            case 4:
                snprintf(line, sizeof(line), "DASH RATIO: %d", sys_config.dash_ratio);
                break;
            case 5:
                snprintf(line, sizeof(line), "PADDLE SWAP: %s", sys_config.paddle_swap ? "YES" : "NO");
                break;
            case 6:
                snprintf(line, sizeof(line), "AUTOSPACE: %s", sys_config.autospace ? "ON" : "OFF");
                break;
            default:
                snprintf(line, sizeof(line), "1ST EXT: %dms", sys_config.first_extension);
                break;
        }
        display_render_text(line, 0, 24);
        snprintf(line, sizeof(line), "ITEM %d/8  CLICK=NEXT", menu_item + 1);
        display_render_text(line, 0, 48);
    }
    flush_buffer();
}
