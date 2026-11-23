#pragma once

#include "baji_board.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { BAJI_DAMAGE_CAPACITY = 16 };

/* One display owner accumulates changes since its last successful paint.
 * Pass regions[0..count) directly to baji_board_paint_round_regions().
 * A zero count means the owner can skip the submission. */
typedef struct {
    baji_rect_t regions[BAJI_DAMAGE_CAPACITY];
    size_t count;
    uint16_t width, height;
} baji_damage_t;

void baji_damage_init(baji_damage_t *damage, uint16_t width, uint16_t height);

/* Half-open physical pixel bounds. Mark both old and new bounds of a moving
 * layer. Empty and fully off-screen bounds cost no transfers. */
void baji_damage_mark(baji_damage_t *damage, int x, int y, int width, int height);
void baji_damage_add(baji_damage_t *damage, baji_rect_t rect);

/* Clear only after every region has reached the display. If painting fails,
 * keep the regions and repaint them with the latest composed scene. */
void baji_damage_clear(baji_damage_t *damage);

#ifdef __cplusplus
}
#endif
