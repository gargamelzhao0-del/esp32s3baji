#pragma once
#include <stdint.h>

/* Discrete samples of CSS ease-in-out, symmetric over a complete cycle. */
void dsk_curve_edges(uint32_t *edges, unsigned steps, unsigned half_us);
unsigned dsk_curve_at(const uint32_t *edges, unsigned steps, unsigned half_us,
                      int64_t elapsed, uint32_t *next_delta);
