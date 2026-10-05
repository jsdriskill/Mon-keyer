#include "keyer.h"

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"

typedef enum {
    STATE_IDLE,
    STATE_DIT,
    STATE_DASH,
    STATE_ELEMENT_SPACE
} KeyerState;

static KeyerState current_state = STATE_IDLE;
static uint32_t state_timer = 0;
static bool dit_latched = false;
static bool dash_latched = false;
static bool last_was_dit = false;
static bool first_element = true;
static uint slice_num;

static uint32_t dit_unit(void) {
    uint8_t wpm = sys_config.wpm;
    if (wpm < 5) {
        wpm = 5;
    }
    return 1200 / wpm;
}

static uint32_t weighted_dit(void) {
    uint32_t unit = dit_unit();
    uint8_t w = sys_config.weighting;
    if (w < 10) {
        w = 10;
    }
    if (w > 90) {
        w = 90;
    }
    return (unit * w) / 50;
}

static uint32_t dash_len(void) {
    uint32_t unit = dit_unit();
    uint8_t ratio = sys_config.dash_ratio;
    if (ratio < 20) {
        ratio = 20;
    }
    if (ratio > 40) {
        ratio = 40;
    }
    return (unit * ratio) / 10;
}

static void key_on(void) {
    gpio_put(PIN_TX_KEY, 1);
    keyer_set_sidetone(true);
}

static void key_off(void) {
    gpio_put(PIN_TX_KEY, 0);
    keyer_set_sidetone(false);
}

void keyer_update_sidetone_freq(void) {
    if (sys_config.sidetone_freq < 100) {
        return;
    }
    uint32_t clock_freq = 125000000;
    uint32_t divider = 16;
    uint32_t wrap = clock_freq / (sys_config.sidetone_freq * divider) - 1;

    pwm_set_clkdiv(slice_num, (float)divider);
    pwm_set_wrap(slice_num, wrap);
    pwm_set_chan_level(slice_num, PWM_CHAN_A, wrap / 2);
}

void keyer_set_sidetone(bool active) {
    if (sys_config.sidetone_en && active) {
        pwm_set_enabled(slice_num, true);
    } else {
        pwm_set_enabled(slice_num, false);
    }
}

void keyer_init(void) {
    gpio_init(PIN_PADDLE_DIT);
    gpio_set_dir(PIN_PADDLE_DIT, GPIO_IN);
    gpio_pull_up(PIN_PADDLE_DIT);

    gpio_init(PIN_PADDLE_DASH);
    gpio_set_dir(PIN_PADDLE_DASH, GPIO_IN);
    gpio_pull_up(PIN_PADDLE_DASH);

    gpio_init(PIN_TX_KEY);
    gpio_set_dir(PIN_TX_KEY, GPIO_OUT);
    gpio_put(PIN_TX_KEY, 0);

    gpio_set_function(PIN_SIDETONE_PWM, GPIO_FUNC_PWM);
    slice_num = pwm_gpio_to_slice_num(PIN_SIDETONE_PWM);
    keyer_update_sidetone_freq();
    keyer_set_sidetone(false);
}

bool keyer_is_busy(void) {
    return current_state != STATE_IDLE;
}

static void start_dit(uint32_t now) {
    dit_latched = false;
    last_was_dit = true;
    key_on();
    uint32_t len = weighted_dit();
    if (first_element) {
        len += sys_config.first_extension;
        first_element = false;
    }
    state_timer = now + len;
    current_state = STATE_DIT;
}

static void start_dash(uint32_t now) {
    dash_latched = false;
    last_was_dit = false;
    key_on();
    uint32_t len = dash_len();
    if (first_element) {
        len += sys_config.first_extension;
        first_element = false;
    }
    state_timer = now + len;
    current_state = STATE_DASH;
}

void keyer_tick(void) {
    uint32_t now = to_ms_since_boot(get_absolute_time());

    bool raw_dit = !gpio_get(sys_config.paddle_swap ? PIN_PADDLE_DASH : PIN_PADDLE_DIT);
    bool raw_dash = !gpio_get(sys_config.paddle_swap ? PIN_PADDLE_DIT : PIN_PADDLE_DASH);

    if (sys_config.mode == KEYER_MODE_STRAIGHT) {
        if (raw_dit || raw_dash) {
            key_on();
            current_state = STATE_DIT;
        } else {
            key_off();
            current_state = STATE_IDLE;
            first_element = true;
        }
        return;
    }

    if (sys_config.mode == KEYER_MODE_BUG) {
        if (raw_dash) {
            key_on();
            current_state = STATE_DASH;
            dit_latched = false;
            return;
        }
        if (current_state == STATE_DASH && !raw_dash) {
            key_off();
            current_state = STATE_IDLE;
            first_element = true;
        }
    }

    if (raw_dit) {
        dit_latched = true;
    }
    if (raw_dash) {
        dash_latched = true;
    }

    uint32_t unit = dit_unit();

    switch (current_state) {
        case STATE_IDLE:
            first_element = true;
            if (dit_latched) {
                start_dit(now);
            } else if (dash_latched) {
                start_dash(now);
            }
            break;

        case STATE_DIT:
        case STATE_DASH:
            if (now >= state_timer) {
                key_off();
                state_timer = now + unit;
                current_state = STATE_ELEMENT_SPACE;
            }
            break;

        case STATE_ELEMENT_SPACE:
            if (now < state_timer) {
                break;
            }
            if (sys_config.mode == KEYER_MODE_ULTIMATE) {
                if (raw_dit && !raw_dash) {
                    start_dit(now);
                } else if (raw_dash && !raw_dit) {
                    start_dash(now);
                } else if (raw_dit && raw_dash) {
                    if (last_was_dit) {
                        start_dit(now);
                    } else {
                        start_dash(now);
                    }
                } else if (dit_latched) {
                    start_dit(now);
                } else if (dash_latched) {
                    start_dash(now);
                } else {
                    current_state = STATE_IDLE;
                    dit_latched = false;
                    dash_latched = false;
                }
                break;
            }

            if (sys_config.mode == KEYER_MODE_IAMBIC_A) {
                dit_latched = raw_dit;
                dash_latched = raw_dash;
            }

            bool squeeze = raw_dit && raw_dash;
            if (squeeze || (dit_latched && dash_latched) ||
                (last_was_dit && dash_latched) || (!last_was_dit && dit_latched)) {
                if (last_was_dit) {
                    start_dash(now);
                } else {
                    start_dit(now);
                }
            } else if (dit_latched || raw_dit) {
                start_dit(now);
            } else if (dash_latched || raw_dash) {
                start_dash(now);
            } else if (sys_config.autospace) {
                current_state = STATE_IDLE;
            } else {
                current_state = STATE_IDLE;
            }
            if (current_state == STATE_IDLE) {
                dit_latched = false;
                dash_latched = false;
            }
            break;

        default:
            current_state = STATE_IDLE;
            break;
    }
}
