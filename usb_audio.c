/*
 * USB audio source: the keyer's sidetone, mirrored as a UAC2 microphone.
 * Runs on core 1, inside tud_task() (see usb_device.c).
 */

#include "usb_audio.h"

#include <math.h>

#include "tusb.h"

#include "config.h"
#include "usb_audio_hw.h"
#include "usb_device.h"

/* ------------------------------------------------------------------------
 * Sidetone synthesis
 * ---------------------------------------------------------------------- */
#define AUDIO_RATE          CFG_TUD_AUDIO_FUNC_1_SAMPLE_RATE
#define AUDIO_FRAME_SAMPLES (AUDIO_RATE / 1000)               /* one USB frame */
#define ENV_MAX             32767
#define ENV_RAMP_MS         4                                  /* click-free attack/release */
#define ENV_STEP            (ENV_MAX / (AUDIO_RATE * ENV_RAMP_MS / 1000))

static int16_t sine_table[256];
static uint32_t phase;
static int32_t envelope;

/* Audio control state (feature unit). Only mute is honoured. */
static bool mute[CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX + 1];
static int16_t volume[CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX + 1];
static uint32_t samp_freq = CFG_TUD_AUDIO_FUNC_1_SAMPLE_RATE;
static uint8_t clk_valid = 1;
static audio_control_range_2_n_t(1) volume_range[CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX + 1];
static audio_control_range_4_n_t(1) sample_freq_range;

/* UAC2 volume is in units of 1/256 dB. Offer 0 dB down to -90 dB in 1 dB
 * steps: hosts then see a normal range instead of warning about a tiny one. */
#define VOLUME_MIN_DB256   (-90 * 256)
#define VOLUME_MAX_DB256   0
#define VOLUME_RES_DB256   256

void usb_audio_init(void) {
    for (int i = 0; i < 256; i++) {
        sine_table[i] = (int16_t)(32767.0f * sinf(6.2831853f * (float)i / 256.0f));
    }

    for (int ch = 0; ch <= CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX; ch++) {
        volume_range[ch].wNumSubRanges = 1;
        volume_range[ch].subrange[0].bMin = VOLUME_MIN_DB256;
        volume_range[ch].subrange[0].bMax = VOLUME_MAX_DB256;
        volume_range[ch].subrange[0].bRes = VOLUME_RES_DB256;
        volume[ch] = 0;   /* 0 dB */
    }

    sample_freq_range.wNumSubRanges = 1;
    sample_freq_range.subrange[0].bMin = CFG_TUD_AUDIO_FUNC_1_SAMPLE_RATE;
    sample_freq_range.subrange[0].bMax = CFG_TUD_AUDIO_FUNC_1_SAMPLE_RATE;
    sample_freq_range.subrange[0].bRes = 0;
}

/* Fill n samples of the sidetone. The tone follows the local sidetone
 * settings (frequency, and the enable flag), so it mirrors what the buzzer does. */
static void synth_block(int16_t *out, int n) {
    bool on = usb_key_state() && sys_config.sidetone_en && !mute[0];

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
 * TinyUSB audio callbacks
 * ---------------------------------------------------------------------- */

/* Called before each 1 ms isochronous packet is loaded: provide the next
 * frame of sidetone. */
bool tud_audio_tx_done_pre_load_cb(uint8_t rhport, uint8_t func_id, uint8_t ep_in, uint8_t cur_alt_setting) {
    (void)rhport; (void)func_id; (void)ep_in; (void)cur_alt_setting;

    /* A packet is still queued in hardware. That happens when the host
     * selects alternate setting 1 again (or 1 -> 0 -> 1) before the queued
     * packet was sent, and when a completion event that was already in
     * TinyUSB's queue is handled after such a request. Queuing another packet
     * on top would make the RP2040 driver panic and hang this core (see
     * usb_audio_hw.h), so skip it. Returning false makes TinyUSB answer a
     * SET_INTERFACE itself, which succeeds, and the queued packet's own
     * completion keeps the stream going. */
    if (usb_audio_ep_armed()) {
        return false;
    }

    int16_t frame[AUDIO_FRAME_SAMPLES];
    synth_block(frame, AUDIO_FRAME_SAMPLES);
    tud_audio_write(frame, sizeof(frame));
    return true;
}

/* The host stopped streaming (or re-selected the interface): restart from
 * silence. */
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
                volume[channel] = (int16_t)((audio_control_cur_2_t *)pBuff)->bCur;
                return true;
            default:
                return false;
        }
    }

    /* Clock source: the rate is fixed. Accept a request for that rate (hosts
     * often write the rate they just read) and refuse anything else. */
    if (entity == 4 && ctrl_sel == AUDIO_CS_CTRL_SAM_FREQ) {
        TU_VERIFY(p_request->wLength == sizeof(audio_control_cur_4_t));
        return ((audio_control_cur_4_t *)pBuff)->bCur == (int32_t)CFG_TUD_AUDIO_FUNC_1_SAMPLE_RATE;
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
