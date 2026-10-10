# Mon Keyer (Raspberry Pi Pico)

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

Flash `build/mon_keyer.uf2` by holding BOOTSEL, plugging USB, and copying the UF2.

## USB

The keyer enumerates as one composite device ("Mon Keyer"):

| Function | What the host sees |
| --- | --- |
| Serial (CDC) | WinKeyer v2-style port |
| Audio | A mono 48 kHz, 16-bit USB microphone ("Mon Keyer Sidetone", UAC2). It carries the same sidetone as the buzzer: same frequency, silent when sidetone is disabled in the menu, 4 ms attack/release ramps. Windows needs 10 (1703) or later; macOS and Linux work out of the box. |
| MIDI | A class-compliant MIDI device ("Mon Keyer MIDI") |

### MIDI messages

MIDI follows the [Vail adapter MIDI spec](https://github.com/Vail-CW/vail-adapter/blob/master/docs/MIDI_INTEGRATION_SPEC.md), which web CW tools such as Vail and VBand-style adapters use. Channel 1:

| Event | Message |
| --- | --- |
| Key down (each dit/dah element, or straight key down) | Note On, note 0, velocity 127 (`90 00 7F`) |
| Key up | Note Off, note 0 (`80 00 00`) |

The keyer does its own iambic/ultimatic/bug timing, so it reports keyed elements on note 0 (as Vail adapters do in their keyer modes) rather than raw paddle contacts on notes 1 and 2. Incoming MIDI (mode, speed, keyer-type messages) is ignored: configure the keyer from its menu or over WinKeyer.

### Keyer behaviour (K3NG-compatible)

Paddle element generation follows the [K3NG keyer](https://github.com/k3ng/k3ng_cw_keyer): one memory per paddle; while an element and its gap are timed only the *opposite* paddle is sampled, and both paddles are sampled by level when they end. The next element is the remembered opposite one, else a repeat. Iambic A drops the memory when a squeeze is gone by the end of the element. Ultimatic mode uses K3NG's last-touch closure logic. Weighting changes key-down time without changing the spacing (dit + gap = 2 units, dah + gap = 4 units at a 3:1 ratio, as in K3NG), and autospace adds 2 units so the character gap is 3. Differences: paddle contacts are debounced (2 ms), and bug mode only keys down for a dah that is still held. The behaviour is checked against a literal port of K3NG's logic on random paddle input (all four modes) and against its timing formulas.


## WinKeyer serial port

Open the CDC port (`/dev/ttyACM0`, any baud rate), send the host-open command (`00 02`; the reply is the revision byte, 23), then:

- **Text** (ASCII `0x20`-`0x7F`) is sent as Morse through the same timing as the paddles (speed, weighting, dash ratio, sidetone, key output, USB audio and MIDI). It goes through a 128 character buffer. Touching a paddle cancels whatever is queued (break-in).
- **Status byte** (`11` + `BUSY`/`BREAKIN`/`XOFF`) is sent whenever it changes, and on request (`15`). `XOFF` is set when the buffer is more than two thirds full; the `WAIT` and `KEYED` bits are never set.
- **Echo**: characters are echoed as they start sending when the mode register's serial-echo bit (bit 2) is set, and characters sent on the paddles are decoded and echoed when paddle echo (bit 6) is set.
- **Setup commands implemented**: sidetone (`01`, 4000/n Hz), speed (`02`), weighting (`03`), speed pot read (`07`, always minimum), clear buffer (`0A`), mode register (`0E`: keyer mode, paddle swap, autospace, echo bits), status request (`15`), and admin open/close/reset/echo test.
- **Accepted but ignored** (their parameter bytes are consumed so the stream stays in sync): PTT timing, speed pot setup, pause, pin config, key immediate, HSCW, Farnsworth, load defaults, first extension, key compensation, paddle switchpoint, software paddle, buffer pointer, dah ratio, PTT, and the buffered commands (key, wait, merge letters, speed change, HSCW, NOP).
- **Not implemented**: admin calibrate, paddle and speed A2D, get values and EEPROM commands produce no reply.

### Notes

- All USB runs on core 1, so the display and encoder cannot disturb the audio. Core 0 only passes the key state across.
- Saving settings pauses core 1 for a few tens of milliseconds, so the audio may click once when you leave the setup menu.
- The device uses the TinyUSB test vendor id (`0xCafe`). Replace `idVendor`/`idProduct` in `usb_descriptors.c` with your own allocation if you distribute it.
- USB stdio (`printf` over USB) is no longer available; the USB port belongs to the composite device.

## Controls

- Encoder: WPM on the home screen; current menu value in setup
- Click: enter setup, then advance items; last click saves to flash and returns home
