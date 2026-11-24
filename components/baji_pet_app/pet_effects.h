#pragma once

#include "baji_scene_damage.h"
#include <stdbool.h>

/* All positions and radii are physical display pixels (466 x 466).  An all-zero
 * snapshot is empty.  Animators quantize the age/phase before handing it off. */
typedef struct { int16_t x, y; } pet_fx_point_t;
typedef enum {
    PET_FX_NONE, PET_FX_HEART, PET_FX_BLUSH, PET_FX_NOTE, PET_FX_SPARKLE,
    PET_FX_QUESTION, PET_FX_SWEAT, PET_FX_POUT, PET_FX_ELLIPSIS
} pet_fx_mark_t;
typedef enum {
    PET_STAR_OFF, PET_STAR_FLY, PET_STAR_HELD, PET_STAR_CAUGHT, PET_STAR_DEPART
} pet_fx_star_t;
typedef struct {
    pet_fx_star_t star;
    pet_fx_point_t star_at, palms[2], trail[4];
    uint8_t wing, catch_frame, trail_count;
    int8_t glow; /* -1 left, +1 right */
    pet_fx_point_t hair[3];
    uint16_t hair_age_ms[3];
    uint8_t hair_count;
    pet_fx_mark_t mark;
    pet_fx_point_t mark_at;
    uint8_t mark_step;
    pet_fx_point_t finger;
    uint8_t contact, contact_step; /* 1 down, 2 release. step = min(age/55ms,3) */
    bool tool;
    uint8_t pillow_depth; /* 0..3 */
    pet_fx_point_t pillow_at;
    bool burst;
    uint8_t burst_frame;
    pet_fx_point_t bubble, nose;
    uint16_t bubble_radius;
} pet_effects_t;

/* Only the visible, unobscured fabric may deform; the source painter must draw
 * the untouched current scene. A NULL mask disables only the deformation. */
typedef bool (*pet_fx_fabric_fn)(void *context, int x, int y);
void pet_effects_paint(const pet_effects_t *fx, const baji_rect_t *band, uint8_t *pixels,
                       baji_paint_fn base, pet_fx_fabric_fn fabric, void *context);
void pet_effects_damage(const pet_effects_t *before, const pet_effects_t *after, baji_damage_t *damage);
