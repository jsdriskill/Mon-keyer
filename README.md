# KA Keyer (Raspberry Pi Pico)

CW paddle keyer for RP2040 with SSD1306 UI, rotary encoder setup, flash-backed settings, and a USB composite device: K1EL WinKeyer v2-style serial port, a USB audio source that mirrors the sidetone, and USB MIDI key events.

## Pins

| Function | GPIO |
| --- | --- |
| Dit paddle (active low, pull-up) | 2 |
| Dash paddle | 3 |
| TX key out | 4 |
| Sidetone PWM | 5 |
| Encoder A | 6 |
| Encoder B | 7 |
| Encoder switch | 8 |
| OLED SDA (I2C0) | 12 |
| OLED SCL | 13 |
| SSD1306 address | 0x3C |

## Build

Uses Pico SDK 2.3.1 from `~/.pico-sdk` when the VS Code Pico extension CMake snippet is present.

```bash
export PICO_SDK_PATH="$HOME/.pico-sdk/sdk/2.3.1"
cmake -S . -B build -DPICO_BOARD=pico
cmake --build build
```

For Pico 2, pass `-DPICO_BOARD=pico2`.

Flash `build/ka_keyer.uf2` by holding BOOTSEL, plugging USB, and copying the UF2.

## USB

The keyer enumerates as one composite device ("KA Keyer"):

| Function | What the host sees |
| --- | --- |
| Serial (CDC) | WinKeyer v2-style port |
| Audio | A mono 48 kHz, 16-bit USB microphone ("KA Keyer Sidetone", UAC2). It carries the same sidetone as the buzzer: same frequency, silent when sidetone is disabled in the menu, 4 ms attack/release ramps. Windows needs 10 (1703) or later; macOS and Linux work out of the box. |
| MIDI | A class-compliant MIDI device ("KA Keyer MIDI") |

### MIDI messages

MIDI follows the [Vail adapter MIDI spec](https://github.com/Vail-CW/vail-adapter/blob/master/docs/MIDI_INTEGRATION_SPEC.md), which web CW tools such as Vail and VBand-style adapters use. Channel 1:

| Event | Message |
| --- | --- |
| Key down (each dit/dah element, or straight key down) | Note On, note 0, velocity 127 (`90 00 7F`) |
| Key up | Note Off, note 0 (`80 00 00`) |

The keyer does its own iambic/ultimatic/bug timing, so it reports keyed elements on note 0 (as Vail adapters do in their keyer modes) rather than raw paddle contacts on notes 1 and 2. Incoming MIDI (mode, speed, keyer-type messages) is ignored: configure the keyer from its menu or over WinKeyer.

### Notes

- All USB runs on core 1, so the display and encoder cannot disturb the audio. Core 0 only passes the key state across.
- Saving settings pauses core 1 for a few tens of milliseconds, so the audio may click once when you leave the setup menu.
- The device uses the TinyUSB test vendor id (`0xCafe`). Replace `idVendor`/`idProduct` in `usb_descriptors.c` with your own allocation if you distribute it.
- USB stdio (`printf` over USB) is no longer available; the USB port belongs to the composite device.

## Controls

- Encoder: WPM on the home screen; current menu value in setup
- Click: enter setup, then advance items; last click saves to flash and returns home
