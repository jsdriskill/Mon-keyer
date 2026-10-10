#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include "hardware/i2c.h"

#define PIN_PADDLE_DIT     2
#define PIN_PADDLE_DASH    3
#define PIN_TX_KEY         4
#define PIN_SIDETONE_PWM   5

#define PIN_ENC_A          6
#define PIN_ENC_B          7
#define PIN_ENC_SW         8

/* Quadrature transitions per physical detent (click). Most mechanical
 * encoders (e.g. EC11 with 20 detents) give 4; some give 2 (rests at both
 * 00 and 11) or 1. If one click moves the value by N, set this to N. */
#ifndef ENC_STEPS_PER_DETENT
#define ENC_STEPS_PER_DETENT 4
#endif

#define I2C_PORT           i2c0
#define PIN_I2C_SDA        12
#define PIN_I2C_SCL        13
#define SSD1306_ADDR       0x3C

typedef enum {
    KEYER_MODE_IAMBIC_B = 0,
    KEYER_MODE_IAMBIC_A,
    KEYER_MODE_ULTIMATE,
    KEYER_MODE_BUG,
    KEYER_MODE_STRAIGHT
} KeyerMode;

typedef struct __attribute__((packed)) {
    uint8_t  magic;
    uint8_t  wpm;
    uint8_t  sidetone_en;
    uint16_t sidetone_freq;
    uint8_t  weighting;
    uint8_t  dash_ratio;
    uint8_t  mode;
    uint8_t  paddle_swap;
    uint8_t  autospace;
    uint8_t  first_extension;
    uint8_t  sidetone_vol;      /* 1..10 (10 = full); appended so older saved settings stay valid */
} ConfigSettings;

#define SIDETONE_VOL_MIN      1
#define SIDETONE_VOL_MAX      10
#define SIDETONE_VOL_DEFAULT  SIDETONE_VOL_MAX

extern ConfigSettings sys_config;

#endif
