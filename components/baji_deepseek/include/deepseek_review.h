#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "baji_graphics.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    DSK_SIDE = 466, DSK_SOURCE = 368, DSK_TILE = 16, DSK_COLUMNS = 23,
    DSK_CLIPS = 6, DSK_ITEMS = 18, DSK_PHASES = 4, DSK_MAX_BUBBLE_STEPS = 65
};

/* item follows the review page button order (clips 0..5, states 6..17).
 * State items have frame 0. bubble 0 is the low point of the CSS float. */
typedef struct { uint8_t item, frame, phase, close, bubble; } dsk_frame_t;

typedef struct {
    const uint8_t *maps, *rows, *quad16, *quad8, *quad4, *quad2;
    const uint8_t *row_triples, *quad_pairs, *palette, *sprites, *mesh;
    const uint8_t *motion_masks, *plates, *mask_maps, *mask_tiles;
    unsigned bubble_steps, clear_tile;
} dsk_art_t;

/* Validate an immutable DSK2 anchor pack or DSK3 anchor + sprite mesh pack.
 * The caller keeps these bytes alive until the last paint returns. */
bool dsk_art_open(dsk_art_t *out, const void *data, size_t size);
void dsk_art_close(dsk_art_t *art);
unsigned dsk_art_motion_steps(const dsk_art_t *art);

typedef struct {
    const dsk_art_t *art;
    const uint8_t *map, *upper;
    unsigned mix;
    struct dsk_motion_work *motion;
    dsk_frame_t selected;
    uint8_t subframe;
    uint8_t blend_tile[DSK_TILE][DSK_TILE * 2];
    uint16_t source[DSK_SIDE], tile_edges[DSK_COLUMNS + 1];
    uint8_t tile_x[DSK_SIDE];
} dsk_canvas_t;

void dsk_canvas_init(dsk_canvas_t *canvas, const dsk_art_t *art);
bool dsk_canvas_select(dsk_canvas_t *canvas, dsk_frame_t frame);
/* mix/8 interpolates to the next captured bubble pose. Zero selects the
 * unmodified Chromium capture. Only differing source tiles need two reads. */
bool dsk_canvas_select_blend(dsk_canvas_t *canvas, dsk_frame_t frame, unsigned mix);
/* subframe/8 advances the original character rig towards its next key. A
 * value of zero always paints the unmodified anchor. DSK2 and still poses
 * accept zero only. The moving canvas lazily expands just its current pose. */
bool dsk_canvas_select_motion(dsk_canvas_t *canvas, dsk_frame_t frame, unsigned mix,
                              unsigned subframe);
/* Release the pixel texture and tile cache before switching to another view
 * or destroying the canvas. A later selection may allocate it again. */
void dsk_canvas_release_motion(dsk_canvas_t *canvas);
void dsk_paint(const baji_rect_t *band, uint8_t *pixels, void *canvas);

/* Reports changed display-space tile runs without reading pixel payloads.
 * Select the new frame after collecting damage. A fresh canvas dirties the screen. */
typedef void (*dsk_damage_fn)(void *context, baji_rect_t area);
bool dsk_canvas_damage_motion(const dsk_canvas_t *canvas, dsk_frame_t next, unsigned mix,
                              unsigned subframe, dsk_damage_fn mark, void *context);
bool dsk_canvas_damage(const dsk_canvas_t *canvas, dsk_frame_t next,
                       dsk_damage_fn mark, void *context);
bool dsk_canvas_damage_blend(const dsk_canvas_t *canvas, dsk_frame_t next, unsigned mix,
                             dsk_damage_fn mark, void *context);

typedef enum {
    DSK_SELECT_CLIP, DSK_SELECT_STATE, DSK_SET_PHASE, DSK_SET_VIEW,
    DSK_TOGGLE_PLAY, DSK_STEP_BACK, DSK_STEP_FORWARD
} dsk_command_t;

typedef struct {
    dsk_frame_t shown;
    uint8_t last_clip, last_pose, bubble_steps, bubble_mix;
    uint8_t motion_steps, subframe;
    unsigned start_sample;
    bool smooth_bubble;
    bool playing;
    int64_t float_origin, last_tick, clock_us;
    uint32_t float_edges[DSK_MAX_BUBBLE_STEPS];
} dsk_model_t;

unsigned dsk_clip_frames(unsigned clip);
unsigned dsk_clip_fps(unsigned clip);
void dsk_model_init(dsk_model_t *model, unsigned bubble_steps, int64_t now_us);
void dsk_model_smooth_bubble(dsk_model_t *model);
bool dsk_command(dsk_model_t *model, dsk_command_t command, unsigned value);
void dsk_advance(dsk_model_t *model, int64_t now_us);
int64_t dsk_next_update(const dsk_model_t *model, int64_t now_us);

/* A press counter preserves taps completed between two render callbacks.
 * Coordinates are already rotated into physical (466x466) display space. */
typedef struct {
    uint32_t presses;
    uint16_t start_x, start_y;
    bool active;
} dsk_gesture_t;

void dsk_touch(dsk_gesture_t *gesture, dsk_model_t *model, uint32_t presses,
               uint16_t start_x, uint16_t start_y, bool down, uint16_t x, uint16_t y);

#ifdef __cplusplus
}
#endif
