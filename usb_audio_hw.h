#ifndef USB_AUDIO_HW_H
#define USB_AUDIO_HW_H

#include <stdbool.h>

/*
 * Read-only view of the RP2040 USB hardware state of the audio IN endpoint.
 *
 * Why this exists: on RP2040, TinyUSB's audio driver never closes the
 * isochronous endpoint (the DCD pre-allocates ISO buffers), and the RP2040
 * DCD does not clear the endpoint's buffer-control register when the host
 * selects the streaming interface again. If a packet is still queued
 * (AVAIL bit set) when the driver queues the next one, the DCD calls
 * panic() and core 1 locks up, after which every control request from the
 * host times out (-110 in the Linux log). Hosts do select alternate
 * setting 1 more than once, or 1 -> 0 -> 1, while probing the device, so the
 * audio code checks this before queuing a packet.
 */

/* true while a packet is queued in hardware and has not been sent yet */
bool usb_audio_ep_armed(void);

#endif
