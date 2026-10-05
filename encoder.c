#include "encoder.h"
#include "config.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"

static volatile int8_t enc_delta = 0;
static uint32_t last_button_time = 0;

static void gpio_callback(uint gpio, uint32_t events) {
    (void)events;
    static uint8_t last_state = 0;
    if (gpio == PIN_ENC_A || gpio == PIN_ENC_B) {
        uint8_t current_state = (gpio_get(PIN_ENC_A) << 1) | gpio_get(PIN_ENC_B);
        uint8_t combined = (last_state << 2) | current_state;
        if (combined == 0b1101 || combined == 0b0100 || combined == 0b0010 || combined == 0b1011) {
            enc_delta++;
        } else if (combined == 0b1110 || combined == 0b1000 || combined == 0b0001 || combined == 0b0111) {
            enc_delta--;
        }
        last_state = current_state;
    }
}

void encoder_init(void) {
    gpio_init(PIN_ENC_A);
    gpio_set_dir(PIN_ENC_A, GPIO_IN);
    gpio_pull_up(PIN_ENC_A);

    gpio_init(PIN_ENC_B);
    gpio_set_dir(PIN_ENC_B, GPIO_IN);
    gpio_pull_up(PIN_ENC_B);

    gpio_init(PIN_ENC_SW);
    gpio_set_dir(PIN_ENC_SW, GPIO_IN);
    gpio_pull_up(PIN_ENC_SW);

    gpio_set_irq_enabled_with_callback(PIN_ENC_A, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &gpio_callback);
    gpio_set_irq_enabled(PIN_ENC_B, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
}

int8_t encoder_get_delta(void) {
    uint32_t ints = save_and_disable_interrupts();
    int8_t d = enc_delta;
    enc_delta = 0;
    restore_interrupts(ints);
    return d;
}

bool encoder_button_pressed(void) {
    if (!gpio_get(PIN_ENC_SW)) {
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now - last_button_time > 200) {
            last_button_time = now;
            return true;
        }
    }
    return false;
}
