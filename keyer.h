#ifndef KEYER_H
#define KEYER_H

#include "config.h"

void keyer_init(void);
void keyer_tick(void);
void keyer_set_sidetone(bool active);
void keyer_update_sidetone_freq(void);
bool keyer_is_busy(void);

#endif
