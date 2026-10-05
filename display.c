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

void display_update_ui(bool menu_active, uint8_t menu_item) {
    memset(frame_buffer, 0x00, sizeof(frame_buffer));
    char line[32];

    if (!menu_active) {
        snprintf(line, sizeof(line), "CW SPEED: %d WPM", sys_config.wpm);
        display_render_text(line, 0, 0);
        snprintf(line, sizeof(line), "MODE: %s", mode_name(sys_config.mode));
        display_render_text(line, 0, 16);
        display_render_text("CLICK FOR MENU", 0, 48);
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
