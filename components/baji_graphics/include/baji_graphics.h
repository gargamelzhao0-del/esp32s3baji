#pragma once

#include <stdint.h>

/* A half-open rectangle in display pixels. */
typedef struct { uint16_t x, y, width, height; } baji_rect_t;

/* Fill every pixel of band in row-major RGB565, high byte first. The stride
 * is band->width * 2 bytes. The output is borrowed for this callback only. */
typedef void (*baji_paint_fn)(const baji_rect_t *band, uint8_t *pixels, void *scene);
