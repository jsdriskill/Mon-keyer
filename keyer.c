#include "keyer.h"

#include "pico/stdlib.h"
#include "pico/util/queue.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "hardware/pwm.h"
#include "usb_device.h"

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

/* ------------------------------------------------------------------------
 * Text sender (used by the WinKeyer serial port)
 *
 * Core 1 (WinKeyer parser) feeds characters through tx_q; this core turns them
 * into Morse. Sent characters, and characters decoded from the paddles, go back
 * to core 1 through echo_q. Paddle input always wins: touching a paddle drops
 * everything queued ("break-in").
 * ---------------------------------------------------------------------- */
#define TX_Q_DEPTH     160          /* host stops at the XOFF level, well below this */
#define TX_LOCAL_SIZE  128          /* the WinKeyer buffer size */
#define TX_CMD_CLEAR   0x100u
#define ECHO_Q_DEPTH   32

typedef struct {
    char    ch;
    uint8_t len;
    uint8_t bits;       /* MSB first of the low 'len' bits; 1 = dash */
} MorseEntry;

static const MorseEntry morse_table[] = {
    {'A',2,0x01},{'B',4,0x08},{'C',4,0x0A},{'D',3,0x04},{'E',1,0x00},{'F',4,0x02},
    {'G',3,0x06},{'H',4,0x00},{'I',2,0x00},{'J',4,0x07},{'K',3,0x05},{'L',4,0x04},
    {'M',2,0x03},{'N',2,0x02},{'O',3,0x07},{'P',4,0x06},{'Q',4,0x0D},{'R',3,0x02},
    {'S',3,0x00},{'T',1,0x01},{'U',3,0x01},{'V',4,0x01},{'W',3,0x03},{'X',4,0x09},
    {'Y',4,0x0B},{'Z',4,0x0C},
    {'1',5,0x0F},{'2',5,0x07},{'3',5,0x03},{'4',5,0x01},{'5',5,0x00},
    {'6',5,0x10},{'7',5,0x18},{'8',5,0x1C},{'9',5,0x1E},{'0',5,0x1F},
    {'.',6,0x15},{',',6,0x33},{'?',6,0x0C},{'/',5,0x12},{'=',5,0x11},{'+',5,0x0A},
    {'-',6,0x21},{'(',5,0x16},{')',6,0x2D},{'\'',6,0x1E},{'!',6,0x2B},{'&',5,0x08},
    {':',6,0x38},{';',6,0x2A},{'"',6,0x12},{'$',7,0x09},{'_',6,0x0D},{'@',6,0x1A},
};
#define MORSE_COUNT (sizeof(morse_table) / sizeof(morse_table[0]))

static const MorseEntry *morse_lookup(char c) {
    if (c >= 'a' && c <= 'z') {
        c = (char)(c - 'a' + 'A');
    }
    for (unsigned i = 0; i < MORSE_COUNT; i++) {
        if (morse_table[i].ch == c) {
            return &morse_table[i];
        }
    }
    return NULL;
}

typedef enum { TX_IDLE, TX_KEYDOWN, TX_GAP } TxState;

static queue_t tx_q;
static queue_t echo_q;
static uint8_t tx_buf[TX_LOCAL_SIZE];       /* characters waiting, core 0 only */
static uint8_t tx_head, tx_tail;
static volatile uint8_t tx_count;           /* mirrored for core 1 */
static TxState tx_state = TX_IDLE;
static const MorseEntry *tx_cur;
static uint8_t tx_idx;                      /* next element of tx_cur */
static uint32_t tx_timer;
static volatile uint8_t tx_put_count;       /* chars queued: written by core 1 only */
static volatile uint8_t tx_pull_count;      /* chars taken from the queue: written by core 0 only */
static volatile bool breakin_flag;
static volatile bool echo_serial, echo_paddle;

/* paddle decoder for paddle echo */
static uint8_t pe_len, pe_bits;
static bool pe_space_pending;
static uint32_t pe_last_off;
static uint32_t tick_now;

static void echo_put(char c) {
    uint8_t v = (uint8_t)c;
    queue_try_add(&echo_q, &v);
}

static void tx_local_flush(void) {
    tx_head = tx_tail = 0;
    tx_count = 0;
}

static void tx_pull(void) {
    uint16_t item;
    while (queue_try_remove(&tx_q, &item)) {
        if (item == TX_CMD_CLEAR) {
            tx_local_flush();
        } else {
            tx_pull_count++;
            if (tx_count >= TX_LOCAL_SIZE) {
                continue;
            }
            tx_buf[tx_head] = (uint8_t)item;
            tx_head = (uint8_t)((tx_head + 1) % TX_LOCAL_SIZE);
            tx_count++;
        }
    }
}

bool keyer_tx_put(uint8_t c) {
    uint16_t item = c;
    breakin_flag = false;
    if (!queue_try_add(&tx_q, &item)) {
        return false;
    }
    tx_put_count++;
    return true;
}

void keyer_tx_clear(void) {
    uint16_t item = TX_CMD_CLEAR;
    breakin_flag = false;
    queue_try_add(&tx_q, &item);
}

uint8_t keyer_tx_pending(void) {
    unsigned n = (uint8_t)(tx_put_count - tx_pull_count) + tx_count;
    return n > 255 ? 255 : (uint8_t)n;
}

bool keyer_tx_busy(void) {
    return tx_state != TX_IDLE || keyer_tx_pending() > 0;
}

bool keyer_breakin(void) {
    return breakin_flag;
}

int keyer_echo_get(void) {
    uint8_t v;
    return queue_try_remove(&echo_q, &v) ? (int)v : -1;
}

void keyer_set_echo(bool serial, bool paddle) {
    echo_serial = serial;
    echo_paddle = paddle;
}

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
    usb_key_event(true);    /* mirrored sidetone (USB audio) + MIDI note on */
}

static void key_off(void) {
    pe_last_off = tick_now;
    gpio_put(PIN_TX_KEY, 0);
    keyer_set_sidetone(false);
    usb_key_event(false);
}

/*
 * Sidetone output: PWM square wave on PIN_SIDETONE_PWM.
 *
 * The pin's PWM channel comes from the pin number (GPIO n -> slice n/2,
 * channel A for even n, B for odd n); GPIO5 is channel B. The slice is shared
 * with GPIO4 (PIN_TX_KEY) but that pin is plain SIO, so it is not affected.
 * While the sidetone is off the pin is parked low as a normal output:
 * stopping the PWM slice alone would freeze the pin at whatever level it had,
 * leaving DC across a buzzer.
 */
static uint chan_num;
static bool sidetone_on;

static void sidetone_pin_low(void) {
    gpio_put(PIN_SIDETONE_PWM, 0);
    gpio_set_dir(PIN_SIDETONE_PWM, GPIO_OUT);
    gpio_set_function(PIN_SIDETONE_PWM, GPIO_FUNC_SIO);
}

void keyer_update_sidetone_freq(void) {
    uint32_t freq = sys_config.sidetone_freq;
    if (freq < 100) {
        return;
    }
    if (freq > 4000) {
        freq = 4000;
    }

    /* Smallest integer divider that keeps the 16 bit counter in range, which
     * gives the finest pitch resolution (about 0.002 %). */
    uint32_t clk = clock_get_hz(clk_sys);
    uint32_t divider = clk / (freq * 65536u) + 1;
    uint32_t wrap = clk / (freq * divider) - 1;

    pwm_set_clkdiv(slice_num, (float)divider);
    pwm_set_wrap(slice_num, (uint16_t)wrap);
    pwm_set_chan_level(slice_num, chan_num, (wrap + 1) / 2);     /* 50 % duty */
}

void keyer_set_sidetone(bool active) {
    bool want = sys_config.sidetone_en && active;
    if (want == sidetone_on) {
        return;                     /* called every tick in some modes: do nothing if unchanged */
    }
    sidetone_on = want;
    if (want) {
        pwm_set_counter(slice_num, 0);
        pwm_set_enabled(slice_num, true);
        gpio_set_function(PIN_SIDETONE_PWM, GPIO_FUNC_PWM);
    } else {
        pwm_set_enabled(slice_num, false);
        sidetone_pin_low();
    }
}

void keyer_init(void) {
    queue_init(&tx_q, sizeof(uint16_t), TX_Q_DEPTH);
    queue_init(&echo_q, sizeof(uint8_t), ECHO_Q_DEPTH);

    gpio_init(PIN_PADDLE_DIT);
    gpio_set_dir(PIN_PADDLE_DIT, GPIO_IN);
    gpio_pull_up(PIN_PADDLE_DIT);

    gpio_init(PIN_PADDLE_DASH);
    gpio_set_dir(PIN_PADDLE_DASH, GPIO_IN);
    gpio_pull_up(PIN_PADDLE_DASH);

    gpio_init(PIN_TX_KEY);
    gpio_set_dir(PIN_TX_KEY, GPIO_OUT);
    gpio_put(PIN_TX_KEY, 0);

    slice_num = pwm_gpio_to_slice_num(PIN_SIDETONE_PWM);
    chan_num = pwm_gpio_to_channel(PIN_SIDETONE_PWM);
    gpio_init(PIN_SIDETONE_PWM);
    sidetone_pin_low();
    pwm_set_enabled(slice_num, false);
    keyer_update_sidetone_freq();
    sidetone_on = false;
}

bool keyer_is_busy(void) {
    return current_state != STATE_IDLE;
}

static void pe_record(bool dash) {
    pe_space_pending = false;
    if (pe_len >= 7) {
        pe_len = 0;                 /* too long to be a character: start over */
    }
    pe_bits = (uint8_t)((pe_bits << 1) | (dash ? 1 : 0));
    pe_len++;
}

static void start_dit(uint32_t now) {
    pe_record(false);
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
    pe_record(true);
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

/* ---- sender state machine (core 0, 1 ms resolution like the paddle logic) ---- */
static void tx_abort(void) {
    if (tx_state == TX_KEYDOWN) {
        key_off();
    }
    tx_state = TX_IDLE;
    tx_cur = NULL;
    tx_local_flush();
}

static void tx_start_element(uint32_t now) {
    bool dash = (tx_cur->bits >> (tx_cur->len - 1 - tx_idx)) & 1;
    key_on();
    tx_timer = now + (dash ? dash_len() : weighted_dit());
    tx_idx++;
    tx_state = TX_KEYDOWN;
}

static void tx_start_next(uint32_t now) {
    while (tx_count > 0) {
        char c = (char)tx_buf[tx_tail];
        tx_tail = (uint8_t)((tx_tail + 1) % TX_LOCAL_SIZE);
        tx_count--;

        if (c == ' ') {
            /* the preceding character already ended with a 3 unit gap */
            tx_timer = now + 4 * dit_unit();
            tx_cur = NULL;
            tx_state = TX_GAP;
            if (echo_serial) {
                echo_put(' ');
            }
            return;
        }
        const MorseEntry *m = morse_lookup(c);
        if (m == NULL) {
            continue;               /* not sendable: skip silently */
        }
        tx_cur = m;
        tx_idx = 0;
        if (echo_serial) {
            echo_put(m->ch);
        }
        tx_start_element(now);
        return;
    }
}

/* Called every tick while the sender is active. */
static void tx_run(uint32_t now) {
    switch (tx_state) {
        case TX_KEYDOWN:
            if (now >= tx_timer) {
                key_off();
                bool last = (tx_cur == NULL) || tx_idx >= tx_cur->len;
                tx_timer = now + (last ? 3 : 1) * dit_unit();   /* character gap / element gap */
                tx_state = TX_GAP;
            }
            break;
        case TX_GAP:
            if (now >= tx_timer) {
                if (tx_cur != NULL && tx_idx < tx_cur->len) {
                    tx_start_element(now);
                } else {
                    tx_cur = NULL;
                    tx_state = TX_IDLE;
                }
            }
            break;
        default:
            break;
    }
}

/* Decode what the paddles just sent and report it (paddle echo). */
static void paddle_echo_service(uint32_t now, bool paddle_down) {
    if (sys_config.mode == KEYER_MODE_STRAIGHT || sys_config.mode == KEYER_MODE_BUG) {
        pe_len = 0;
        pe_space_pending = false;
        return;
    }
    if (current_state != STATE_IDLE || paddle_down) {
        return;
    }
    uint32_t idle = now - pe_last_off;
    if (pe_len > 0 && idle >= 2 * dit_unit()) {
        if (echo_paddle) {
            for (unsigned i = 0; i < MORSE_COUNT; i++) {
                if (morse_table[i].len == pe_len && morse_table[i].bits == pe_bits) {
                    echo_put(morse_table[i].ch);
                    break;
                }
            }
        }
        pe_len = 0;
        pe_bits = 0;
        pe_space_pending = true;
    } else if (pe_space_pending && idle >= 6 * dit_unit()) {
        if (echo_paddle) {
            echo_put(' ');
        }
        pe_space_pending = false;
    }
}

void keyer_tick(void) {
    uint32_t now = to_ms_since_boot(get_absolute_time());

    bool raw_dit = !gpio_get(sys_config.paddle_swap ? PIN_PADDLE_DASH : PIN_PADDLE_DIT);
    bool raw_dash = !gpio_get(sys_config.paddle_swap ? PIN_PADDLE_DIT : PIN_PADDLE_DASH);
    bool paddle_down = raw_dit || raw_dash;

    tick_now = now;
    tx_pull();

    /* Break-in: touching a paddle cancels whatever the host has queued. */
    if (paddle_down && (tx_state != TX_IDLE || tx_count > 0)) {
        tx_abort();
        breakin_flag = true;
    }

    if (tx_state != TX_IDLE) {
        tx_run(now);
    }
    /* Start the next queued character once the paddle side is completely quiet.
     * (Checked after tx_run so a character follows its gap without losing a tick.) */
    if (tx_state == TX_IDLE && tx_count > 0 && !paddle_down &&
        current_state == STATE_IDLE && !dit_latched && !dash_latched) {
        tx_start_next(now);
    }
    if (tx_state != TX_IDLE) {
        return;
    }

    paddle_echo_service(now, paddle_down);

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
