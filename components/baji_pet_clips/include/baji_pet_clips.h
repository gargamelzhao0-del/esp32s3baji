#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "baji_graphics.h"

typedef struct pet_clips pet_clips_t;
typedef struct pet_clip pet_clip_t;
typedef enum {
    PET_HIT_NONE, PET_HIT_NOSE, PET_HIT_CHEEK, PET_HIT_HEAD, PET_HIT_WING_LEFT, PET_HIT_WING_RIGHT,
    PET_HIT_TAIL, PET_HIT_BODY, PET_HIT_PILLOW, PET_HIT_BUBBLE, PET_HIT_BOOK, PET_HIT_KEYS,
    PET_HIT_LENS, PET_HIT_PALM
} pet_hit_t;

/* Open the GPT-specific BPC1 data in the shared role partition. */
esp_err_t pet_clips_open(pet_clips_t **out);
/* Caller owns the memory for the lifetime of the bank. */
esp_err_t pet_clips_open_data(pet_clips_t **out, const void *data, size_t size);
void pet_clips_close(pet_clips_t *bank);
const pet_clip_t *pet_clips_lookup(const pet_clips_t *bank, const char *action, const char *scene);
unsigned pet_clip_frames(const pet_clip_t *clip);
unsigned pet_clip_fps(const pet_clip_t *clip);
/* Select an original web drawing and return its changed physical screen region.
 * The application marks the whole screen when returning from another painter. */
esp_err_t pet_clips_select(pet_clips_t *bank,const pet_clip_t *clip,unsigned frame,baji_rect_t *damage);
void pet_clips_paint(const baji_rect_t *band,uint8_t *pixels,void *context);
/* All coordinates are physical display pixels on the 466 x 466 panel. */
void pet_clip_anchors(const pet_clip_t *clip,unsigned frame,int xy[10]);
bool pet_clip_palms(const pet_clip_t *clip,unsigned frame,int xy[4]);
bool pet_clip_prop(const pet_clip_t *clip,unsigned frame,int xy[2],int *radius);
bool pet_clip_pillow(const pet_clip_t *clip,unsigned frame,baji_rect_t *bounds);
bool pet_clip_pillow_contains(const pet_clips_t *bank,const pet_clip_t *clip,unsigned frame,int x,int y);
pet_hit_t pet_clip_hit(const pet_clips_t *bank,const pet_clip_t *clip,unsigned frame,int x,int y,int *side);
