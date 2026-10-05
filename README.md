# KA Keyer (Raspberry Pi Pico)

CW paddle keyer for RP2040 with SSD1306 UI, rotary encoder setup, flash-backed settings, and K1EL WinKeyer v2-style USB CDC.

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

## Controls

- Encoder: WPM on the home screen; current menu value in setup
- Click: enter setup, then advance items; last click saves to flash and returns home
