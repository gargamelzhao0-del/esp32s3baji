#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "baji_graphics.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct baji_pet baji_pet_t;
typedef enum { BAJI_PET_IDLE, BAJI_PET_PAT, BAJI_PET_CHEEK_LEFT, BAJI_PET_CHEEK_RIGHT, BAJI_PET_TICKLE } baji_pet_clip_t;
typedef struct {
    baji_pet_clip_t clip;
    uint8_t frame;
    int8_t gaze_x, gaze_y;
    uint8_t phase; /* In-between samples within one original frame. */
} baji_pet_state_t;

/* Opens the bundled artwork, or a complete BPZ asset supplied by the caller.
 * open_data owns a decoded copy and releases all references to packed on return. */
esp_err_t baji_pet_open(baji_pet_t **out);
esp_err_t baji_pet_open_data(baji_pet_t **out, const void *packed, size_t size);
/* Attach a 4-byte aligned BPR1 companion to BPH5. Retain its bytes until close. */
esp_err_t baji_pet_attach_reactions(baji_pet_t *pet, const void *data, size_t size);
/* Physical display coordinates. Returns -1 for the left cheek, +1 for right. */
int baji_pet_cheek(const baji_pet_t *pet, baji_pet_state_t state, uint16_t x, uint16_t y);
void baji_pet_close(baji_pet_t *pet);
unsigned baji_pet_frame_count(baji_pet_clip_t clip);
unsigned baji_pet_fps(baji_pet_clip_t clip);
/* Actual sample rate is authored fps times the substeps in this artwork. */
unsigned baji_pet_substeps(const baji_pet_t *pet, baji_pet_clip_t clip);

/* Frames wrap in their clip. Gaze saturates at +/-2 horizontal, +/-1 vertical. */
void baji_pet_select(baji_pet_t *pet, baji_pet_state_t state);
bool baji_pet_forehead(const baji_pet_t *pet, baji_pet_state_t state, uint16_t x, uint16_t y);
/* old == NULL requests an initial full frame. The caller supplies at least one
 * slot. The result also covers nonconsecutive frames, wraps and clip changes. */
size_t baji_pet_damage(baji_pet_t *pet, const baji_pet_state_t *old,
                       const baji_pet_state_t *next, baji_rect_t *regions, size_t capacity);
void baji_pet_paint(const baji_rect_t *band, uint8_t *pixels, void *pet);

#ifdef __cplusplus
}
#endif
