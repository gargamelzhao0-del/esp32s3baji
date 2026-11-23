#pragma once

#include <stdbool.h>
#include "baji_board.h"

typedef struct baji_pet baji_pet_t;
typedef enum { BAJI_PET_IDLE, BAJI_PET_PAT } baji_pet_clip_t;
typedef struct {
    baji_pet_clip_t clip;
    uint8_t frame;
    int8_t gaze_x, gaze_y;
} baji_pet_state_t;

/* Opens the bundled artwork, or a complete BPZ asset supplied by the caller.
 * open_data owns a decoded copy and releases all references to packed on return. */
esp_err_t baji_pet_open(baji_pet_t **out);
esp_err_t baji_pet_open_data(baji_pet_t **out, const void *packed, size_t size);
void baji_pet_close(baji_pet_t *pet);
unsigned baji_pet_frame_count(baji_pet_clip_t clip);
unsigned baji_pet_fps(baji_pet_clip_t clip);

/* Frames wrap in their clip. Gaze saturates at +/-2 horizontal, +/-1 vertical. */
void baji_pet_select(baji_pet_t *pet, baji_pet_state_t state);
bool baji_pet_forehead(const baji_pet_t *pet, baji_pet_state_t state, uint16_t x, uint16_t y);
/* old == NULL requests an initial full frame. The caller supplies at least one
 * slot. The result also covers nonconsecutive frames, wraps and clip changes. */
size_t baji_pet_damage(baji_pet_t *pet, const baji_pet_state_t *old,
                       const baji_pet_state_t *next, baji_rect_t *regions, size_t capacity);
void baji_pet_paint(const baji_rect_t *band, uint8_t *pixels, void *pet);
