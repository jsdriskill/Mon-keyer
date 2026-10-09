#include "usb_device.h"

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/flash.h"
#include "tusb.h"

#include "config.h"
#include "usb_audio.h"
#include "winkeyer.h"

/* ------------------------------------------------------------------------
 * MIDI key events (Vail / VBand adapter protocol)
 *
 * Channel 1. Note 0 is the straight-key / keyed-element note: this keyer does
 * its own iambic timing, so every key-down element is "note on", key-up is
 * "note off". Notes 1 and 2 (raw dit / dah) are only used by adapters in
 * passthrough mode and are deliberately not sent, so a host that also runs
 * its own keyer does not see every element twice.
 * Spec: https://github.com/Vail-CW/vail-adapter/blob/master/docs/MIDI_INTEGRATION_SPEC.md
 * ---------------------------------------------------------------------- */
#define MIDI_NOTE_KEY     0
#define MIDI_NOTE_ON      0x90
#define MIDI_NOTE_OFF     0x80

/* ------------------------------------------------------------------------
 * Shared state between the cores
 * ---------------------------------------------------------------------- */
static volatile bool key_down;          /* written by core 0, read by core 1 */
static volatile bool app_ready;
static volatile bool core1_ready;

void usb_key_event(bool down) {
    key_down = down;
}

bool usb_key_state(void) {
    return key_down;
}

void usb_app_ready(void) {
    app_ready = true;
}

/* ------------------------------------------------------------------------
 * CDC helpers (WinKeyer)
 * ---------------------------------------------------------------------- */
int usb_cdc_getc_timeout_us(uint32_t timeout_us) {
    absolute_time_t until = make_timeout_time_us(timeout_us);
    while (true) {
        if (tud_cdc_available()) {
            return (int)(uint8_t)tud_cdc_read_char();
        }
        if (time_reached(until)) {
            return PICO_ERROR_TIMEOUT;
        }
        tud_task();
    }
}

void usb_cdc_putc(uint8_t c) {
    /* Like the previous stdio-based version, drop output while no program has
     * the port open, so stale replies are not delivered on the next connect. */
    if (!tud_cdc_connected()) {
        return;
    }
    tud_cdc_write_char((char)c);
    tud_cdc_write_flush();
}

/* ------------------------------------------------------------------------
 * Core 1 main loop
 * ---------------------------------------------------------------------- */
static bool midi_key_sent;      /* last key state delivered over MIDI */

static void midi_service(void) {
    /* Discard anything the host sends us (e.g. Vail mode / keyer-type
     * messages). The keyer is configured from its own menu or WinKeyer. */
    while (tud_midi_available()) {
        uint8_t packet[4];
        tud_midi_packet_read(packet);
    }

    bool down = key_down;
    if (down == midi_key_sent) {
        return;
    }
    if (!tud_midi_mounted()) {
        midi_key_sent = down;   /* nobody listening: stay in sync, send nothing */
        return;
    }
    uint8_t msg[3] = {
        (uint8_t)(down ? MIDI_NOTE_ON : MIDI_NOTE_OFF),
        MIDI_NOTE_KEY,
        (uint8_t)(down ? 0x7F : 0x00),
    };
    /* If the TX FIFO is full the write is refused and we retry next pass,
     * so a note on/off is never lost and the host never sees a stuck key. */
    if (tud_midi_stream_write(0, msg, sizeof(msg)) == sizeof(msg)) {
        midi_key_sent = down;
    }
}

static void core1_main(void) {
    /* Allow core 0 to pause this core while it writes flash. */
    flash_safe_execute_core_init();

    usb_audio_init();

    tusb_init();

    core1_ready = true;

    while (true) {
        tud_task();
        midi_service();
        if (app_ready) {
            winkeyer_process();
        }
    }
}

/* 4 KB: TinyUSB plus the WinKeyer parser (deepest call chain measured well
 * under 1 KB). 8-byte aligned, as the ARM calling convention requires. */
static uint32_t core1_stack[1024] __attribute__((aligned(8)));

void usb_start(void) {
    multicore_launch_core1_with_stack(core1_main, core1_stack, sizeof(core1_stack));
    while (!core1_ready) {
        tight_loop_contents();
    }
}
