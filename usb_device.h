#ifndef USB_DEVICE_H
#define USB_DEVICE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * USB composite device: CDC serial (WinKeyer), UAC2 audio source (sidetone
 * mirror) and USB MIDI (key events).
 *
 * All TinyUSB calls run on core 1. Core 0 (keyer, display, encoder) only
 * touches usb_key_event(), which passes one flag across, so slow display I2C
 * transfers on core 0 can never starve the 1 ms USB audio cadence.
 */

/* Launch core 1 and wait until it is ready for flash lockout. Call first in
 * main(), before settings_init() (settings_save() pauses core 1 while it
 * erases flash). */
void usb_start(void);

/* Tell core 1 that settings and keyer are initialised, so it may start
 * processing WinKeyer commands. */
void usb_app_ready(void);

/* Key state changed (true = key down). Safe to call every tick; only core 0
 * calls it. Drives the mirrored sidetone and the MIDI note. */
void usb_key_event(bool down);

/* CDC helpers for winkeyer.c (core 1 only). Return PICO_ERROR_TIMEOUT if no
 * byte arrived within timeout_us. */
int  usb_cdc_getc_timeout_us(uint32_t timeout_us);
void usb_cdc_putc(uint8_t c);

#endif
