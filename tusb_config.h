#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

/*
 * TinyUSB configuration: one composite full-speed device with
 *   - CDC serial   (WinKeyer protocol)
 *   - UAC2 audio   (mono 48 kHz 16-bit microphone: the mirrored sidetone)
 *   - USB MIDI     (key events for VBand / Vail style web apps)
 */

#define CFG_TUSB_RHPORT0_MODE       (OPT_MODE_DEVICE)

#define CFG_TUD_ENDPOINT0_SIZE      64

#define CFG_TUD_CDC                 1
#define CFG_TUD_MSC                 0
#define CFG_TUD_HID                 0
#define CFG_TUD_MIDI                1
#define CFG_TUD_AUDIO               1
#define CFG_TUD_VENDOR              0

/* CDC */
#define CFG_TUD_CDC_RX_BUFSIZE      64
#define CFG_TUD_CDC_TX_BUFSIZE      64
#define CFG_TUD_CDC_EP_BUFSIZE      64

/* MIDI */
#define CFG_TUD_MIDI_RX_BUFSIZE     64
#define CFG_TUD_MIDI_TX_BUFSIZE     64

/* Audio: UAC2 microphone, 1 channel, 16-bit, 48 kHz (device -> host only) */
#define CFG_TUD_AUDIO_FUNC_1_SAMPLE_RATE                48000
#define CFG_TUD_AUDIO_FUNC_1_DESC_LEN                   TUD_AUDIO_MIC_ONE_CH_DESC_LEN
#define CFG_TUD_AUDIO_FUNC_1_N_AS_INT                   1
#define CFG_TUD_AUDIO_FUNC_1_CTRL_BUF_SZ                64

#define CFG_TUD_AUDIO_ENABLE_EP_IN                      1
#define CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_TX      2
#define CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX              1
#define CFG_TUD_AUDIO_EP_SZ_IN                          TUD_AUDIO_EP_SIZE(CFG_TUD_AUDIO_FUNC_1_SAMPLE_RATE, CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_TX, CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX)
#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX               CFG_TUD_AUDIO_EP_SZ_IN
/* Full speed: one packet per 1 ms frame */
#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SW_BUF_SZ            (CFG_TUD_AUDIO_EP_SZ_IN)

#endif /* TUSB_CONFIG_H */
