#ifndef USB_AUDIO_H
#define USB_AUDIO_H

/* One-time setup of the sidetone synth and the audio control ranges.
 * Call on core 1 before tusb_init(). */
void usb_audio_init(void);

#endif
