#ifndef DISPLAY_H
#define DISPLAY_H

#include "config.h"

void display_init(void);
void display_update_ui(bool menu_active, uint8_t menu_item);
void display_render_text(const char *str, uint8_t x, uint8_t y);

#endif
