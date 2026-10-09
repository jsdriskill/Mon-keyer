/*
 * USB descriptors for the composite device: UAC2 microphone (sidetone mirror),
 * CDC serial (WinKeyer) and USB MIDI (key events).
 *
 * Structure follows the TinyUSB audio_test / cdc_uac2 / midi_test examples
 * (MIT licensed, Copyright (c) 2019 Ha Thach, tinyusb.org).
 *
 * The audio function must come before the MIDI function: TinyUSB offers each
 * interface to its class drivers in a fixed order, and the UAC2 driver has to
 * claim the UAC2 control interface before the MIDI driver sees it.
 */

#include <string.h>

#include "pico/unique_id.h"
#include "tusb.h"

#include "usb_device.h"

/*
 * A combination of interfaces must have a unique product id, since the PC
 * remembers the driver binding after the first plug-in.
 * Auto ProductID layout bitmap:   AUDIO | MIDI | HID | MSC | CDC
 *
 * 0xCafe is the TinyUSB test vendor id. Replace VID/PID with your own
 * allocation if you distribute this device.
 */
#define _PID_MAP(itf, n)  ((CFG_TUD_##itf) << (n))
#define USB_PID           (0x4000 | _PID_MAP(CDC, 0) | _PID_MAP(MSC, 1) | _PID_MAP(HID, 2) | \
                           _PID_MAP(MIDI, 3) | _PID_MAP(AUDIO, 4) | _PID_MAP(VENDOR, 5))

//--------------------------------------------------------------------+
// Device descriptor
//--------------------------------------------------------------------+
static tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,

    /* Interface Association Descriptors are used (audio, CDC) */
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor           = 0xCafe,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0100,

    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,

    .bNumConfigurations = 0x01
};

uint8_t const *tud_descriptor_device_cb(void) {
    return (uint8_t const *)&desc_device;
}

//--------------------------------------------------------------------+
// Configuration descriptor
//--------------------------------------------------------------------+
enum {
    ITF_NUM_AUDIO_CONTROL = 0,   /* audio uses 2 interfaces: 0, 1 */
    ITF_NUM_AUDIO_STREAMING,
    ITF_NUM_CDC,                 /* CDC uses 2 interfaces: 2, 3 */
    ITF_NUM_CDC_DATA,
    ITF_NUM_MIDI,                /* MIDI uses 2 interfaces: 4, 5 */
    ITF_NUM_MIDI_STREAMING,
    ITF_NUM_TOTAL
};

#define EPNUM_AUDIO_IN    USB_EP_AUDIO_IN
#define EPNUM_CDC_NOTIF   0x82
#define EPNUM_CDC_OUT     0x03
#define EPNUM_CDC_IN      0x83
#define EPNUM_MIDI_OUT    0x04
#define EPNUM_MIDI_IN     0x84

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_AUDIO_MIC_ONE_CH_DESC_LEN + \
                           TUD_CDC_DESC_LEN + TUD_MIDI_DESC_LEN)

static uint8_t const desc_configuration[] = {
    /* Config number, interface count, string index, total length, attribute, power in mA */
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),

    /* Audio: first interface, string index, bytes/sample, bits/sample, EP in, EP size */
    TUD_AUDIO_MIC_ONE_CH_DESCRIPTOR(ITF_NUM_AUDIO_CONTROL, 4,
                                    CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_TX,
                                    CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_TX * 8,
                                    EPNUM_AUDIO_IN, CFG_TUD_AUDIO_EP_SZ_IN),

    /* CDC: interface, string index, EP notification + size, EP out, EP in, size */
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 5, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),

    /* MIDI: interface, string index, EP out, EP in, size */
    TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, 6, EPNUM_MIDI_OUT, EPNUM_MIDI_IN, 64),
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_configuration;
}

//--------------------------------------------------------------------+
// String descriptors
//--------------------------------------------------------------------+
enum {
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
    STRID_AUDIO,
    STRID_CDC,
    STRID_MIDI,
};

static char const *const string_desc_arr[] = {
    (const char[]){0x09, 0x04},   /* 0: English (0x0409) */
    "Mon",                        /* 1: manufacturer */
    "Keyer",                      /* 2: product (Linux shows "<manufacturer> <product>") */
    NULL,                         /* 3: serial, from the flash unique id */
    "Mon Keyer Sidetone",         /* 4: audio function */
    "Mon Keyer WinKeyer",         /* 5: CDC serial */
    "Mon Keyer MIDI",             /* 6: MIDI */
};

static uint16_t desc_str[32 + 1];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    size_t chr_count;

    switch (index) {
        case STRID_LANGID:
            memcpy(&desc_str[1], string_desc_arr[0], 2);
            chr_count = 1;
            break;

        case STRID_SERIAL: {
            char serial[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
            pico_get_unique_board_id_string(serial, sizeof(serial));
            chr_count = strlen(serial);
            for (size_t i = 0; i < chr_count && i < 32; i++) {
                desc_str[1 + i] = serial[i];
            }
            break;
        }

        default: {
            if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) {
                return NULL;
            }
            const char *str = string_desc_arr[index];
            chr_count = strlen(str);
            size_t const max_count = sizeof(desc_str) / sizeof(desc_str[0]) - 1;
            if (chr_count > max_count) {
                chr_count = max_count;
            }
            for (size_t i = 0; i < chr_count; i++) {
                desc_str[1 + i] = str[i];
            }
            break;
        }
    }

    /* first word: length in bytes (incl. header) and descriptor type */
    desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return desc_str;
}
