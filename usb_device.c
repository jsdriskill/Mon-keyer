#include "usb_device.h"

#include <math.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/flash.h"
#include "tusb.h"

#include "config.h"
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
 * Sidetone synthesis for the USB audio source
 * ---------------------------------------------------------------------- */
#define AUDIO_RATE        CFG_TUD_AUDIO_FUNC_1_SAMPLE_RATE
#define AUDIO_FRAME_SAMPLES (AUDIO_RATE / 1000)               /* one USB frame */
#define ENV_MAX           32767
#define ENV_RAMP_MS       4                                    /* click-free attack/release */
#define ENV_STEP          (ENV_MAX / (AUDIO_RATE * ENV_RAMP_MS / 1000))

static int16_t sine_table[256];
static uint32_t phase;
static int32_t envelope;

static volatile bool key_down;          /* written by core 0, read by core 1 */
static volatile bool app_ready;
static volatile bool core1_ready;

/* Audio control state (feature unit). Only mute is honoured. */
static bool mute[CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX + 1];
static uint16_t volume[CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX + 1];
static uint32_t samp_freq = CFG_TUD_AUDIO_FUNC_1_SAMPLE_RATE;
static uint8_t clk_valid = 1;
static audio_control_range_2_n_t(1) volume_range[CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX + 1];
static audio_control_range_4_n_t(1) sample_freq_range;

void usb_key_event(bool down) {
    key_down = down;
}

void usb_app_ready(void) {
    app_ready = true;
}

static void synth_init(void) {
    for (int i = 0; i < 256; i++) {
        sine_table[i] = (int16_t)(32767.0f * sinf(6.2831853f * (float)i / 256.0f));
    }
}

/* Fill n samples of the sidetone. The tone follows the local sidetone
 * settings (frequency, and the enable flag), so it mirrors what the buzzer does. */
static void synth_block(int16_t *out, int n) {
    bool on = key_down && sys_config.sidetone_en && !mute[0];

    uint32_t freq = sys_config.sidetone_freq;
    if (freq < 100 || freq > 4000) {
        freq = 700;
    }
    uint32_t inc = (uint32_t)(((uint64_t)freq << 32) / AUDIO_RATE);

    for (int i = 0; i < n; i++) {
        if (on) {
            envelope += ENV_STEP;
            if (envelope > ENV_MAX) {
                envelope = ENV_MAX;
            }
        } else {
            envelope -= ENV_STEP;
            if (envelope < 0) {
                envelope = 0;
            }
        }
        /* half scale, to leave headroom for host-side gain */
        out[i] = (int16_t)(((int32_t)sine_table[phase >> 24] * envelope) >> 16);
        phase += inc;
    }
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

    synth_init();

    volume_range[0].wNumSubRanges = 1;
    volume_range[0].subrange[0].bMin = -90;
    volume_range[0].subrange[0].bMax = 90;
    volume_range[0].subrange[0].bRes = 1;
    sample_freq_range.wNumSubRanges = 1;
    sample_freq_range.subrange[0].bMin = CFG_TUD_AUDIO_FUNC_1_SAMPLE_RATE;
    sample_freq_range.subrange[0].bMax = CFG_TUD_AUDIO_FUNC_1_SAMPLE_RATE;
    sample_freq_range.subrange[0].bRes = 0;

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

static uint32_t core1_stack[1024];   /* 4 KB: TinyUSB plus the WinKeyer parser */

void usb_start(void) {
    multicore_launch_core1_with_stack(core1_main, core1_stack, sizeof(core1_stack));
    while (!core1_ready) {
        tight_loop_contents();
    }
}

/* ------------------------------------------------------------------------
 * TinyUSB audio callbacks
 * ---------------------------------------------------------------------- */

/* Called before each 1 ms isochronous packet is loaded: provide the next
 * frame of sidetone. */
bool tud_audio_tx_done_pre_load_cb(uint8_t rhport, uint8_t itf, uint8_t ep_in, uint8_t cur_alt_setting) {
    (void)rhport; (void)itf; (void)ep_in; (void)cur_alt_setting;
    int16_t frame[AUDIO_FRAME_SAMPLES];
    synth_block(frame, AUDIO_FRAME_SAMPLES);
    tud_audio_write(frame, sizeof(frame));
    return true;
}

/* The host stopped streaming: restart from silence. */
bool tud_audio_set_itf_close_EP_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
    (void)rhport; (void)p_request;
    envelope = 0;
    return true;
}

bool tud_audio_set_req_ep_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *pBuff) {
    (void)rhport; (void)p_request; (void)pBuff;
    return false;
}

bool tud_audio_set_req_itf_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *pBuff) {
    (void)rhport; (void)p_request; (void)pBuff;
    return false;
}

/* Entity ids come from the descriptor in usbd.h: 1 input terminal,
 * 2 feature unit, 4 clock source. */
bool tud_audio_set_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *pBuff) {
    (void)rhport;
    uint8_t channel = TU_U16_LOW(p_request->wValue);
    uint8_t ctrl_sel = TU_U16_HIGH(p_request->wValue);
    uint8_t entity = TU_U16_HIGH(p_request->wIndex);

    TU_VERIFY(p_request->bRequest == AUDIO_CS_REQ_CUR);
    TU_VERIFY(channel <= CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX);

    if (entity == 2) {
        switch (ctrl_sel) {
            case AUDIO_FU_CTRL_MUTE:
                TU_VERIFY(p_request->wLength == sizeof(audio_control_cur_1_t));
                mute[channel] = ((audio_control_cur_1_t *)pBuff)->bCur;
                return true;
            case AUDIO_FU_CTRL_VOLUME:
                TU_VERIFY(p_request->wLength == sizeof(audio_control_cur_2_t));
                volume[channel] = (uint16_t)((audio_control_cur_2_t *)pBuff)->bCur;
                return true;
            default:
                return false;
        }
    }
    return false;
}

bool tud_audio_get_req_ep_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
    (void)rhport; (void)p_request;
    return false;
}

bool tud_audio_get_req_itf_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
    (void)rhport; (void)p_request;
    return false;
}

bool tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
    uint8_t channel = TU_U16_LOW(p_request->wValue);
    uint8_t ctrl_sel = TU_U16_HIGH(p_request->wValue);
    uint8_t entity = TU_U16_HIGH(p_request->wIndex);

    if (channel > CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX) {
        return false;
    }

    /* Input terminal (the "microphone") */
    if (entity == 1 && ctrl_sel == AUDIO_TE_CTRL_CONNECTOR) {
        audio_desc_channel_cluster_t ret;
        ret.bNrChannels = 1;
        ret.bmChannelConfig = (audio_channel_config_t)0;
        ret.iChannelNames = 0;
        return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, (void *)&ret, sizeof(ret));
    }

    /* Feature unit: mute and volume */
    if (entity == 2) {
        switch (ctrl_sel) {
            case AUDIO_FU_CTRL_MUTE:
                return tud_control_xfer(rhport, p_request, &mute[channel], 1);
            case AUDIO_FU_CTRL_VOLUME:
                switch (p_request->bRequest) {
                    case AUDIO_CS_REQ_CUR:
                        return tud_control_xfer(rhport, p_request, &volume[channel], sizeof(volume[channel]));
                    case AUDIO_CS_REQ_RANGE:
                        return tud_control_xfer(rhport, p_request, &volume_range[0], sizeof(volume_range[0]));
                    default:
                        return false;
                }
            default:
                return false;
        }
    }

    /* Clock source: fixed sample rate */
    if (entity == 4) {
        switch (ctrl_sel) {
            case AUDIO_CS_CTRL_SAM_FREQ:
                switch (p_request->bRequest) {
                    case AUDIO_CS_REQ_CUR:
                        return tud_control_xfer(rhport, p_request, &samp_freq, sizeof(samp_freq));
                    case AUDIO_CS_REQ_RANGE:
                        return tud_control_xfer(rhport, p_request, &sample_freq_range, sizeof(sample_freq_range));
                    default:
                        return false;
                }
            case AUDIO_CS_CTRL_CLK_VALID:
                return tud_control_xfer(rhport, p_request, &clk_valid, sizeof(clk_valid));
            default:
                return false;
        }
    }

    return false;
}
