#include "winkeyer.h"

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "config.h"
#include "keyer.h"
#include "flash_settings.h"

#define WK_ADMIN        0x00
#define WK_SIDE_FREQ    0x01
#define WK_SET_SPEED    0x02
#define WK_WEIGHTING    0x03
#define WK_PTT_TIMING   0x04
#define WK_PAUSE        0x05
#define WK_GET_SPEED    0x07
#define WK_BACKSPACE    0x08
#define WK_PINCFG       0x09
#define WK_CLEAR        0x0A
#define WK_KEY_IMMED    0x0B
#define WK_SET_MODE     0x0E

static uint8_t host_open = 0;

void winkeyer_init(void) {
    stdio_set_translate_crlf(&stdio_usb, false);
}

static void send_resp(uint8_t val) {
    putchar_raw(val);
}

static int read_param(void) {
    return getchar_timeout_us(10000);
}

void winkeyer_process(void) {
    int c = getchar_timeout_us(0);
    if (c == PICO_ERROR_TIMEOUT) {
        return;
    }

    uint8_t cmd = (uint8_t)c;

    if (cmd == WK_ADMIN) {
        int sub = read_param();
        if (sub == PICO_ERROR_TIMEOUT) {
            return;
        }
        switch ((uint8_t)sub) {
            case 0x02:
                send_resp(0x55);
                break;
            case 0x03:
                send_resp(23);
                break;
            case 0x04:
                host_open = 1;
                send_resp(0xc0);
                break;
            case 0x05:
                host_open = 0;
                break;
            default:
                break;
        }
        return;
    }

    if (!host_open && cmd < 0x20) {
        return;
    }

    switch (cmd) {
        case WK_SET_SPEED: {
            int speed = read_param();
            if (speed != PICO_ERROR_TIMEOUT && speed >= 5 && speed <= 99) {
                sys_config.wpm = (uint8_t)speed;
            }
            break;
        }
        case WK_SIDE_FREQ: {
            int f = read_param();
            if (f != PICO_ERROR_TIMEOUT) {
                static const uint16_t freqs[] = {400, 480, 600, 700, 800, 900, 1000, 1200};
                if (f <= 7) {
                    sys_config.sidetone_freq = freqs[f];
                    keyer_update_sidetone_freq();
                }
            }
            break;
        }
        case WK_WEIGHTING: {
            int w = read_param();
            if (w != PICO_ERROR_TIMEOUT && w >= 10 && w <= 90) {
                sys_config.weighting = (uint8_t)w;
            }
            break;
        }
        case WK_GET_SPEED:
            send_resp(sys_config.wpm);
            break;
        case WK_SET_MODE: {
            int m = read_param();
            if (m != PICO_ERROR_TIMEOUT) {
                uint8_t bits = (uint8_t)m;
                if (bits & 0x10) {
                    sys_config.paddle_swap = 1;
                } else {
                    sys_config.paddle_swap = 0;
                }
                uint8_t mode_bits = bits & 0x07;
                if (mode_bits == 0x00) {
                    sys_config.mode = KEYER_MODE_IAMBIC_B;
                } else if (mode_bits == 0x01) {
                    sys_config.mode = KEYER_MODE_IAMBIC_A;
                } else if (mode_bits == 0x02) {
                    sys_config.mode = KEYER_MODE_ULTIMATE;
                } else if (mode_bits == 0x03) {
                    sys_config.mode = KEYER_MODE_BUG;
                } else if (mode_bits == 0x04) {
                    sys_config.mode = KEYER_MODE_STRAIGHT;
                }
            }
            break;
        }
        case WK_PTT_TIMING:
        case WK_PAUSE:
        case WK_PINCFG:
        case WK_KEY_IMMED:
            (void)read_param();
            break;
        case WK_CLEAR:
        case WK_BACKSPACE:
            break;
        default:
            break;
    }
}
