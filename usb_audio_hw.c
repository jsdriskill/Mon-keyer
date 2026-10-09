#include "usb_audio_hw.h"

#include "hardware/structs/usb.h"

#include "usb_device.h"

#define AUDIO_EP_NUM   (USB_EP_AUDIO_IN & 0x0F)

bool usb_audio_ep_armed(void) {
    return (usb_dpram->ep_buf_ctrl[AUDIO_EP_NUM].in & USB_BUF_CTRL_AVAIL) != 0;
}
