#include <stdio.h>
#include "pico/stdlib.h"
#include "config.h"
#include "flash_settings.h"
#include "encoder.h"
#include "display.h"
#include "keyer.h"
#include "winkeyer.h"
#include "usb_device.h"

#define MENU_ITEM_COUNT 8

static int clampi(int value, int lo, int hi) {
    if (value < lo) {
        return lo;
    }
    if (value > hi) {
        return hi;
    }
    return value;
}

int main(void) {
    /* USB (CDC WinKeyer, audio sidetone, MIDI) runs entirely on core 1. It is
     * started first so core 1 is ready for flash lockout before settings_init()
     * can write defaults to flash on a first boot. */
    usb_start();

    settings_init();
    encoder_init();
    display_init();
    keyer_init();
    winkeyer_init();
    usb_app_ready();

    bool menu_active = false;
    uint8_t menu_item = 0;
    uint32_t last_ui_update = 0;

    while (1) {
        keyer_tick();

        int8_t delta = encoder_get_delta();
        if (delta != 0) {
            if (!menu_active) {
                sys_config.wpm = (uint8_t)clampi((int)sys_config.wpm + delta, 5, 60);
            } else if (menu_item == 0) {
                int mode = ((int)sys_config.mode + delta) % 5;
                if (mode < 0) {
                    mode += 5;
                }
                sys_config.mode = (uint8_t)mode;
            } else if (menu_item == 1) {
                int freq = (int)sys_config.sidetone_freq + (delta * 50);
                sys_config.sidetone_freq = (uint16_t)clampi(freq, 400, 1200);
                keyer_update_sidetone_freq();
            } else if (menu_item == 2) {
                sys_config.sidetone_en = delta > 0 ? 1 : 0;
            } else if (menu_item == 3) {
                sys_config.weighting = (uint8_t)clampi((int)sys_config.weighting + delta, 10, 90);
            } else if (menu_item == 4) {
                sys_config.dash_ratio = (uint8_t)clampi((int)sys_config.dash_ratio + delta, 20, 40);
            } else if (menu_item == 5) {
                sys_config.paddle_swap = delta > 0 ? 1 : 0;
            } else if (menu_item == 6) {
                sys_config.autospace = delta > 0 ? 1 : 0;
            } else {
                sys_config.first_extension = (uint8_t)clampi((int)sys_config.first_extension + delta, 0, 25);
            }
        }

        if (encoder_button_pressed()) {
            if (!menu_active) {
                menu_active = true;
                menu_item = 0;
            } else {
                menu_item++;
                if (menu_item >= MENU_ITEM_COUNT) {
                    menu_active = false;
                    menu_item = 0;
                    settings_save();
                }
            }
        }

        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now - last_ui_update > 100) {
            last_ui_update = now;
            display_update_ui(menu_active, menu_item);
        }
    }

    return 0;
}
