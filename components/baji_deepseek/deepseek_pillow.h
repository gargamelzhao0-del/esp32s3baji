#pragma once
#include <stdbool.h>
#include "baji_graphics.h"

/* The ellipse covers only exposed cloth in the flattened nap artwork. */
typedef struct { int16_t x, y, rx, ry, bottom; } dsk_cloth_t;
typedef struct {
    dsk_cloth_t cloth;
    int16_t x, y;
    int8_t depth;
} dsk_pillow_t;

bool dsk_pillow_contains(dsk_cloth_t cloth, int x, int y);
baji_rect_t dsk_pillow_bounds(dsk_pillow_t pillow);
void dsk_pillow_paint(dsk_pillow_t pillow, const baji_rect_t *band, uint8_t *pixels,
                      baji_paint_fn base, void *canvas);
