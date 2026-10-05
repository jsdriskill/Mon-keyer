#ifndef ENCODER_H
#define ENCODER_H

#include <stdbool.h>
#include <stdint.h>

void encoder_init(void);
int8_t encoder_get_delta(void);
bool encoder_button_pressed(void);

#endif
