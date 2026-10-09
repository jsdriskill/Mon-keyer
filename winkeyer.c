#include "winkeyer.h"

#include "pico/stdlib.h"
#include "config.h"
#include "keyer.h"
#include "usb_device.h"

/*
 * WinKeyer (K1EL WK2/WK3 style) serial protocol over the USB CDC port.
 *
 * Implemented: host open/close/echo test, speed, sidetone, weighting, mode
 * register, text sending with buffer, status
 * byte, serial and paddle echo, break-in, clear buffer. Every other command is
 * accepted and its parameter bytes are consumed (so the stream never loses
 * sync) but has no effect. Not implemented yet: buffered commands (wait,
 * buffered speed change, merge, ...), PTT, Farnsworth, HSCW, key compensation,
 * paddle switchpoint, buffer pointer commands, EEPROM and the A2D/get-values
 * admin commands.
 *
 * The parser is non-blocking: it keeps the partly received command in a small
 * state machine instead of waiting for parameter bytes.
 */

#define WK_REVISION         23

/* immediate commands */
#define WK_ADMIN            0x00
#define WK_SIDE_FREQ        0x01
#define WK_SET_SPEED        0x02
#define WK_WEIGHTING        0x03
#define WK_GET_SPEED        0x07
#define WK_CLEAR            0x0A
#define WK_SET_MODE         0x0E
#define WK_STATUS_REQ       0x15

/* admin sub-commands */
#define ADM_RESET           0x01
#define ADM_HOST_OPEN       0x02
#define ADM_HOST_CLOSE      0x03
#define ADM_ECHO_TEST       0x04
#define ADM_LOAD_EEPROM     0x0D
#define ADM_SEND_MSG        0x0E
#define ADM_LOAD_XMODE      0x0F

/* pseudo command ids for the second stage of two-stage commands */
#define STAGE_ADMIN_BASE    0x200       /* + sub-command: waiting for its parameters */
#define STAGE_POINTER       0x300       /* buffer pointer command, waiting for the sub-command */

/* status byte: 11 W K B I X  (we never set WAIT or KEYED) */
#define ST_BASE             0xC0
#define ST_XOFF             0x01
#define ST_BREAKIN          0x02
#define ST_BUSY             0x04

#define XOFF_ON_LEVEL       85          /* 2/3 of the 128 character buffer */
#define XOFF_OFF_LEVEL      64
#define CMD_TIMEOUT_US      500000      /* abandon a half received command */
#define MAX_BYTES_PER_CALL  64

/* number of parameter bytes of each immediate command 0x01..0x1F */
static const uint8_t cmd_params[0x20] = {
    [0x00] = 1,                 /* admin: sub-command */
    [0x01] = 1, [0x02] = 1, [0x03] = 1, [0x04] = 2, [0x05] = 3, [0x06] = 1,
    [0x07] = 0, [0x08] = 0, [0x09] = 1, [0x0A] = 0, [0x0B] = 1, [0x0C] = 1,
    [0x0D] = 1, [0x0E] = 1, [0x0F] = 15, [0x10] = 1, [0x11] = 1, [0x12] = 1,
    [0x13] = 0, [0x14] = 1, [0x15] = 0, [0x16] = 1, [0x17] = 1, [0x18] = 1,
    [0x19] = 1, [0x1A] = 1, [0x1B] = 2, [0x1C] = 1, [0x1D] = 1, [0x1E] = 0,
    [0x1F] = 0,
};

static bool host_open;
static bool xoff;
static uint8_t last_status;

/* partly received command */
static uint16_t pend_cmd;
static uint16_t pend_need;
static uint16_t pend_have;
static uint8_t  pend_buf[16];
static uint32_t pend_last_us;

void winkeyer_init(void) {
    host_open = false;
    xoff = false;
    last_status = ST_BASE;
    pend_need = 0;
    keyer_set_echo(false, false);
}

static uint8_t current_status(void) {
    uint8_t pending = keyer_tx_pending();
    if (pending >= XOFF_ON_LEVEL) {
        xoff = true;
    } else if (pending < XOFF_OFF_LEVEL) {
        xoff = false;
    }
    uint8_t s = ST_BASE;
    if (xoff) {
        s |= ST_XOFF;
    }
    if (keyer_breakin()) {
        s |= ST_BREAKIN;
    }
    if (keyer_tx_busy()) {
        s |= ST_BUSY;
    }
    return s;
}

static void send_status(void) {
    last_status = current_status();
    usb_cdc_putc(last_status);
}

static void apply_mode_register(uint8_t bits) {
    /* bit7 paddle watchdog disable (ignored), bit6 paddle echo, bits5-4 keyer
     * mode, bit3 paddle swap, bit2 serial echo, bit1 autospace, bit0 CT spacing (ignored) */
    switch ((bits >> 4) & 0x03) {
        case 0: sys_config.mode = KEYER_MODE_IAMBIC_B; break;
        case 1: sys_config.mode = KEYER_MODE_IAMBIC_A; break;
        case 2: sys_config.mode = KEYER_MODE_ULTIMATE; break;
        default: sys_config.mode = KEYER_MODE_BUG; break;
    }
    sys_config.paddle_swap = (bits & 0x08) ? 1 : 0;
    sys_config.autospace = (bits & 0x02) ? 1 : 0;
    keyer_set_echo((bits & 0x04) != 0, (bits & 0x40) != 0);
}

static void host_close(void) {
    host_open = false;
    keyer_tx_clear();
}

/* A command with all its parameters has arrived. Admin is two stage. */
static void execute(uint16_t cmd, const uint8_t *p) {
    switch (cmd) {
        case WK_ADMIN:
            /* p[0] is the sub-command */
            switch (p[0]) {
                case ADM_RESET:
                    keyer_tx_clear();
                    break;
                case ADM_HOST_OPEN:
                    host_open = true;
                    keyer_tx_clear();
                    usb_cdc_putc(WK_REVISION);
                    last_status = current_status();
                    break;
                case ADM_HOST_CLOSE:
                    host_close();
                    break;
                case ADM_ECHO_TEST:
                    pend_cmd = STAGE_ADMIN_BASE + ADM_ECHO_TEST;
                    pend_need = 1;
                    pend_have = 0;
                    break;
                case ADM_LOAD_EEPROM:
                    pend_cmd = STAGE_ADMIN_BASE + ADM_LOAD_EEPROM;
                    pend_need = 256;
                    pend_have = 0;
                    break;
                case ADM_SEND_MSG:
                case ADM_LOAD_XMODE:
                    pend_cmd = STAGE_ADMIN_BASE + p[0];
                    pend_need = 1;
                    pend_have = 0;
                    break;
                default:
                    break;      /* calibrate, A2D, get values, ... : no effect */
            }
            return;

        case STAGE_ADMIN_BASE + ADM_ECHO_TEST:
            usb_cdc_putc(p[0]);
            return;

        case STAGE_POINTER:
            /* 16 00 resets the pointer; 16 01/02/03 take one more byte */
            if (p[0] != 0) {
                pend_cmd = STAGE_POINTER + 1;
                pend_need = 1;
                pend_have = 0;
            }
            return;

        case WK_SIDE_FREQ: {
            unsigned n = p[0] & 0x0F;       /* bit7 = paddle-only sidetone (ignored) */
            if (n >= 1 && n <= 10) {
                sys_config.sidetone_freq = (uint16_t)(4000 / n);
                keyer_update_sidetone_freq();
            }
            return;
        }
        case WK_SET_SPEED:
            if (p[0] >= 5 && p[0] <= 99) {      /* 0 = use the speed pot: there is none */
                sys_config.wpm = p[0];
            }
            return;
        case WK_WEIGHTING:
            if (p[0] >= 10 && p[0] <= 90) {
                sys_config.weighting = p[0];
            }
            return;
        case WK_GET_SPEED:
            usb_cdc_putc(0x80);             /* speed pot at minimum */
            return;
        case WK_CLEAR:
            keyer_tx_clear();
            return;
        case WK_SET_MODE:
            apply_mode_register(p[0]);
            return;
        case WK_STATUS_REQ:
            send_status();
            return;
        default:
            return;     /* accepted, parameters consumed, no effect */
    }
}

/* One byte from the host. */
static void parse_byte(uint8_t b) {
    if (pend_need > 0) {
        if (pend_have < sizeof(pend_buf)) {
            pend_buf[pend_have] = b;
        }
        pend_have++;
        if (pend_have < pend_need) {
            return;
        }
        uint16_t cmd = pend_cmd;
        uint8_t p[sizeof(pend_buf)];
        for (unsigned i = 0; i < sizeof(p); i++) {
            p[i] = pend_buf[i];
        }
        pend_need = 0;
        execute(cmd, p);
        return;
    }

    if (b == WK_ADMIN) {
        pend_cmd = WK_ADMIN;
        pend_need = 1;
        pend_have = 0;
        return;
    }

    if (!host_open) {
        return;                 /* ignore everything until the host opens us */
    }

    if (b >= 0x20) {
        if (b < 0x80) {
            keyer_tx_put(b);    /* text to send as Morse; dropped if the queue is full */
        }
        return;
    }

    uint8_t n = cmd_params[b];
    if (b == 0x16) {
        pend_cmd = STAGE_POINTER;
        pend_need = 1;
        pend_have = 0;
        return;
    }
    if (n == 0) {
        uint8_t none[sizeof(pend_buf)] = {0};
        execute(b, none);
        return;
    }
    pend_cmd = b;
    pend_need = n;
    pend_have = 0;
}

/* Called repeatedly from the USB core's main loop. */
void winkeyer_process(void) {
    /* Characters that were just sent, or decoded from the paddles. */
    int e;
    while ((e = keyer_echo_get()) >= 0) {
        if (host_open) {
            usb_cdc_putc((uint8_t)e);
        }
    }

    for (int i = 0; i < MAX_BYTES_PER_CALL; i++) {
        int c = usb_cdc_getc_timeout_us(0);
        if (c == PICO_ERROR_TIMEOUT) {
            break;
        }
        pend_last_us = time_us_32();
        parse_byte((uint8_t)c);
    }

    if (pend_need > 0 && (uint32_t)(time_us_32() - pend_last_us) > CMD_TIMEOUT_US) {
        pend_need = 0;          /* host stopped in the middle of a command: resync */
    }

    if (host_open) {
        uint8_t s = current_status();
        if (s != last_status) {
            last_status = s;
            usb_cdc_putc(s);
        }
    }
}
