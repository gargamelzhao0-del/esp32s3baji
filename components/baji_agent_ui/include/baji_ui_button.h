#pragma once
#include "baji_graphics.h"
#include <stdbool.h>

/* Draw a centered pixel label in a clipped, opaque control. */
void baji_ui_button_paint(const baji_rect_t *band, uint8_t *pixels, baji_rect_t rect,
                          const char *label, unsigned scale, bool accent);
